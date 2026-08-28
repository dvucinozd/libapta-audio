#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Freeze opaque ASAP development key labels from performance annotations."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import platform
import subprocess
from pathlib import Path
from typing import Any


FORMAT = "apta-1.1-asap-annotated-key-development-1"
ASAP_REVISION = "afc815c75c42e83a79c03feb6da8a35e77d4c6b8"
MIN_TRACKS = 24
TONICS = {
    "C": 0,
    "B#": 0,
    "C#": 1,
    "Db": 1,
    "D": 2,
    "D#": 3,
    "Eb": 3,
    "E": 4,
    "Fb": 4,
    "E#": 5,
    "F": 5,
    "F#": 6,
    "Gb": 6,
    "G": 7,
    "G#": 8,
    "Ab": 8,
    "A": 9,
    "A#": 10,
    "Bb": 10,
    "B": 11,
    "Cb": 11,
}


class FreezeError(ValueError):
    """Raised when an input violates the frozen D2 protocol."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _require_source_revision(root: Path) -> str:
    try:
        completed = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise FreezeError("ASAP source must be the frozen Git checkout") from exc
    revision = completed.stdout.strip().casefold()
    if revision != ASAP_REVISION:
        raise FreezeError(
            f"ASAP revision must be {ASAP_REVISION}, found {revision or '<empty>'}"
        )
    return revision


def _normalized_key(token: str) -> tuple[int, str]:
    mode = "minor" if token.endswith("m") else "major"
    tonic = token[:-1] if mode == "minor" else token
    if tonic not in TONICS:
        raise FreezeError("unrecognized_key_signature")
    return TONICS[tonic], mode


def _key_events(value: Any) -> list[tuple[float, tuple[int, str]]]:
    if not isinstance(value, dict) or not value:
        raise FreezeError("missing_perf_key_signatures")
    events: list[tuple[float, tuple[int, str]]] = []
    seen: set[float] = set()
    previous = -math.inf
    for raw_time, raw_token in value.items():
        try:
            time = float(raw_time)
        except (TypeError, ValueError) as exc:
            raise FreezeError("invalid_annotation_time") from exc
        if not math.isfinite(time) or time < 0.0:
            raise FreezeError("invalid_annotation_time")
        if time in seen:
            raise FreezeError("duplicate_annotation_time")
        if time <= previous:
            raise FreezeError("non_increasing_annotation_times")
        if not isinstance(raw_token, str):
            raise FreezeError("non_string_key_signature")
        events.append((time, _normalized_key(raw_token)))
        seen.add(time)
        previous = time
    return events


def freeze(
    prepared: Path,
    asap_root: Path,
    labels_out: Path,
    manifest_out: Path,
) -> dict[str, object]:
    revision = _require_source_revision(asap_root)
    annotations_path = asap_root / "asap_annotations.json"
    rhythm_labels_path = prepared / "labels.json"
    sources_path = prepared / "sources.private.json"
    prepared_manifest_path = prepared / "manifest.json"
    annotations = json.loads(annotations_path.read_text(encoding="utf-8"))
    rhythm_labels = json.loads(rhythm_labels_path.read_text(encoding="utf-8"))
    sources = json.loads(sources_path.read_text(encoding="utf-8"))
    prepared_manifest = json.loads(
        prepared_manifest_path.read_text(encoding="utf-8")
    )
    if not isinstance(annotations, dict):
        raise FreezeError("ASAP annotations must be an object")
    if prepared_manifest.get("source_revision") != ASAP_REVISION:
        raise FreezeError("prepared corpus source revision mismatch")
    if prepared_manifest.get("labels_sha256") != _sha256(rhythm_labels_path):
        raise FreezeError("prepared rhythm-label hash mismatch")

    development_ids = {
        str(row["track"])
        for row in rhythm_labels
        if row.get("split") == "development"
    }
    if len(development_ids) != 40:
        raise FreezeError("expected exactly 40 frozen ASAP development IDs")
    sources_by_track = {str(row["track"]): row for row in sources}
    if len(sources_by_track) != len(sources):
        raise FreezeError("duplicate track in private source mapping")
    if not development_ids.issubset(sources_by_track):
        raise FreezeError("development IDs missing from private source mapping")
    window_seconds = float(prepared_manifest["window_seconds"])
    if not math.isfinite(window_seconds) or window_seconds <= 0.0:
        raise FreezeError("invalid prepared window duration")

    included: list[dict[str, object]] = []
    excluded: list[dict[str, str]] = []
    for track in sorted(development_ids):
        source_row = sources_by_track[track]
        audio_path = prepared / "audio" / f"{track}.wav"
        if not audio_path.is_file():
            excluded.append({"track": track, "reason": "missing_prepared_audio"})
            continue
        if _sha256(audio_path) != str(source_row.get("audio_sha256", "")):
            excluded.append({"track": track, "reason": "audio_hash_mismatch"})
            continue
        source_path = source_row.get("source_path")
        if not isinstance(source_path, str) or not source_path:
            excluded.append({"track": track, "reason": "invalid_source_path"})
            continue
        annotation = annotations.get(source_path)
        if not isinstance(annotation, dict):
            excluded.append({"track": track, "reason": "missing_annotation_row"})
            continue
        try:
            events = _key_events(annotation.get("perf_key_signatures"))
        except FreezeError as exc:
            excluded.append({"track": track, "reason": str(exc)})
            continue
        start = float(source_row["window_start_seconds"])
        if not math.isfinite(start) or start < 0.0:
            excluded.append({"track": track, "reason": "invalid_window_start"})
            continue
        end = start + window_seconds
        active = [key for time, key in events if time <= start]
        if not active:
            excluded.append({"track": track, "reason": "no_active_signature"})
            continue
        selected = active[-1]
        if any(key != selected for time, key in events if start < time < end):
            excluded.append({"track": track, "reason": "modulation"})
            continue
        included.append(
            {
                "track": track,
                "key_tonic": selected[0],
                "key_mode": selected[1],
            }
        )

    modes = {
        mode: sum(row["key_mode"] == mode for row in included)
        for mode in ("major", "minor")
    }
    viability_reasons = []
    if len(included) < MIN_TRACKS:
        viability_reasons.append("insufficient_track_count")
    if modes["major"] == 0 or modes["minor"] == 0:
        viability_reasons.append("missing_mode_coverage")
    exclusion_reason_counts: dict[str, int] = {}
    for row in excluded:
        reason = row["reason"]
        exclusion_reason_counts[reason] = exclusion_reason_counts.get(reason, 0) + 1

    labels_out.parent.mkdir(parents=True, exist_ok=True)
    with labels_out.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=("track", "key_tonic", "key_mode")
        )
        writer.writeheader()
        writer.writerows(included)
    report: dict[str, object] = {
        "format": FORMAT,
        "acceptance_claim": False,
        "evidence_level": "independent-development",
        "split": "development",
        "inputs": {
            "asap_revision": revision,
            "annotations_sha256": _sha256(annotations_path),
            "prepared_manifest_sha256": _sha256(prepared_manifest_path),
            "rhythm_labels_sha256": _sha256(rhythm_labels_path),
            "sources_private_sha256": _sha256(sources_path),
        },
        "runtime": {"python": platform.python_version()},
        "derivation_tool_sha256": _sha256(Path(__file__)),
        "labels_sha256": _sha256(labels_out),
        "development_input_count": len(development_ids),
        "included_count": len(included),
        "mode_counts": modes,
        "viable": not viability_reasons,
        "viability_reasons": viability_reasons,
        "exclusion_reason_counts": exclusion_reason_counts,
        "included_track_ids": [str(row["track"]) for row in included],
        "excluded": excluded,
    }
    encoded = json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n"
    manifest_out.parent.mkdir(parents=True, exist_ok=True)
    manifest_out.write_text(encoded, encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared", type=Path, required=True)
    parser.add_argument("--asap-root", type=Path, required=True)
    parser.add_argument("--labels-output", type=Path, required=True)
    parser.add_argument("--manifest-output", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = freeze(
            args.prepared,
            args.asap_root,
            args.labels_output,
            args.manifest_output,
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.exit(1, f"error: {exc}\n")
    print(json.dumps(report, sort_keys=True, separators=(",", ":")))
    return 0 if report["viable"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
