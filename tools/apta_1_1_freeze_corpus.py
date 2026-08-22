#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Freeze a manually labelled APTA 1.1 DJ validation corpus.

The staging CSV may contain local source paths. Output never contains those
paths: track IDs are derived from full audio SHA-256 digests and labels are
rewritten to opaque IDs accepted by the frozen DJ evaluator.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path

FORMAT = "apta-1.1-dj-validation-1"
MIN_ACCEPTANCE_TRACKS = 48
LABEL_FIELDS = [
    "track",
    "key_tonic",
    "key_mode",
    "meter_numerator",
    "meter_denominator",
    "downbeat_frame",
    "beat_period_frames",
]
STAGING_FIELDS = ["source", *LABEL_FIELDS[1:]]


class FreezeError(ValueError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def opaque_id(audio_sha256: str) -> str:
    return "track-" + audio_sha256[:24]


def validate_label(row: dict[str, str], line: int) -> None:
    mode = row["key_mode"].strip().casefold()
    if mode not in {"major", "minor"}:
        raise FreezeError(f"line {line}: key_mode must be major or minor")
    try:
        tonic = int(row["key_tonic"])
        numerator = int(row["meter_numerator"])
        denominator = int(row["meter_denominator"])
        downbeat = int(row["downbeat_frame"])
        period = float(row["beat_period_frames"])
    except ValueError as exc:
        raise FreezeError(f"line {line}: invalid numeric label") from exc
    if not 0 <= tonic <= 11:
        raise FreezeError(f"line {line}: key_tonic must be 0..11")
    if numerator not in {3, 4} or denominator != 4:
        raise FreezeError(f"line {line}: meter must be 3/4 or 4/4")
    if downbeat < 0 or period <= 0.0:
        raise FreezeError(f"line {line}: invalid frame labels")


def freeze(
    corpus_root: Path,
    staging_csv: Path,
    labels_out: Path,
    manifest_out: Path,
    frozen_utc: str,
    reference_source: str,
    verification_procedure: str,
    allow_diagnostic: bool,
) -> dict[str, object]:
    rows: list[dict[str, str]] = []
    seen_ids: set[str] = set()
    seen_hashes: set[str] = set()

    with staging_csv.open("r", encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source)
        missing = set(STAGING_FIELDS) - set(reader.fieldnames or [])
        if missing:
            raise FreezeError(f"staging CSV missing columns: {sorted(missing)}")
        for line, raw in enumerate(reader, start=2):
            source_name = (raw.get("source") or "").strip()
            if not source_name:
                raise FreezeError(f"line {line}: empty source")
            path = (corpus_root / source_name).resolve()
            try:
                path.relative_to(corpus_root.resolve())
            except ValueError as exc:
                raise FreezeError(f"line {line}: source escapes corpus root") from exc
            if not path.is_file():
                raise FreezeError(f"line {line}: missing source file {source_name!r}")
            validate_label(raw, line)
            audio_hash = sha256_file(path)
            track = opaque_id(audio_hash)
            if audio_hash in seen_hashes or track in seen_ids:
                raise FreezeError(f"line {line}: duplicate audio content")
            seen_hashes.add(audio_hash)
            seen_ids.add(track)
            rows.append({
                "track": track,
                "key_tonic": str(int(raw["key_tonic"])),
                "key_mode": raw["key_mode"].strip().casefold(),
                "meter_numerator": str(int(raw["meter_numerator"])),
                "meter_denominator": str(int(raw["meter_denominator"])),
                "downbeat_frame": str(int(raw["downbeat_frame"])),
                "beat_period_frames": format(float(raw["beat_period_frames"]), ".9g"),
            })

    rows.sort(key=lambda row: row["track"])
    if len(rows) < MIN_ACCEPTANCE_TRACKS and not allow_diagnostic:
        raise FreezeError(
            f"corpus has {len(rows)} tracks; at least {MIN_ACCEPTANCE_TRACKS} required"
        )

    labels_out.parent.mkdir(parents=True, exist_ok=True)
    with labels_out.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=LABEL_FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    labels_hash = sha256_file(labels_out)
    manifest = {
        "format": FORMAT,
        "track_count": len(rows),
        "track_ids": [row["track"] for row in rows],
        "labels_sha256": labels_hash,
        "frozen_utc": frozen_utc,
        "reference_source": reference_source,
        "verification_procedure": verification_procedure,
    }
    manifest_out.parent.mkdir(parents=True, exist_ok=True)
    manifest_out.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-root", type=Path, required=True)
    parser.add_argument("--staging-labels", type=Path, required=True)
    parser.add_argument("--labels-output", type=Path, required=True)
    parser.add_argument("--manifest-output", type=Path, required=True)
    parser.add_argument("--frozen-utc", required=True)
    parser.add_argument("--reference-source", required=True)
    parser.add_argument("--verification-procedure", required=True)
    parser.add_argument("--allow-diagnostic", action="store_true")
    args = parser.parse_args()
    try:
        manifest = freeze(
            args.corpus_root,
            args.staging_labels,
            args.labels_output,
            args.manifest_output,
            args.frozen_utc,
            args.reference_source,
            args.verification_procedure,
            args.allow_diagnostic,
        )
    except (OSError, FreezeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
