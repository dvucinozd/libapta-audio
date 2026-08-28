#!/usr/bin/env python3
"""Freeze opaque key labels for the ASAP development split from MIDI metadata."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.metadata
import json
import platform
from pathlib import Path, PurePosixPath

try:
    import mido
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit(
        "error: apta_1_1_asap_key_development.py requires mido"
    ) from exc


FORMAT = "apta-1.1-asap-key-development-1"
MIDO_VERSION = "1.3.3"
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
    """Raised when an input violates the frozen key-development protocol."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _normalized_key(token: str) -> tuple[int, str]:
    mode = "minor" if token.endswith("m") else "major"
    tonic = token[:-1] if mode == "minor" else token
    if tonic not in TONICS:
        raise FreezeError("unrecognized_key_signature")
    return TONICS[tonic], mode


def _key_events(path: Path) -> list[tuple[float, tuple[int, str]]]:
    midi = mido.MidiFile(path)
    tempo = 500_000
    current = 0.0
    events: list[tuple[float, tuple[int, str]]] = []
    for message in mido.merge_tracks(midi.tracks):
        current += mido.tick2second(
            message.time, midi.ticks_per_beat, tempo
        )
        if message.type == "set_tempo":
            tempo = int(message.tempo)
        elif message.type == "key_signature":
            events.append((current, _normalized_key(str(message.key))))
    return events


def _resolve_source(root: Path, source_path: str) -> Path:
    relative = PurePosixPath(source_path)
    if relative.is_absolute() or ".." in relative.parts:
        raise FreezeError("source path escapes ASAP root")
    root_resolved = root.resolve(strict=True)
    source = root.joinpath(*relative.parts).resolve(strict=True)
    if not source.is_relative_to(root_resolved):
        raise FreezeError("source path escapes ASAP root")
    return source


def _source_set_sha256(rows: list[tuple[str, str]]) -> str:
    digest = hashlib.sha256()
    for track, source_hash in sorted(rows):
        digest.update(track.encode("ascii"))
        digest.update(b"\0")
        digest.update(source_hash.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def freeze(
    prepared: Path,
    asap_root: Path,
    labels_out: Path,
    manifest_out: Path,
) -> dict[str, object]:
    mido_version = importlib.metadata.version("mido")
    if mido_version != MIDO_VERSION:
        raise FreezeError(
            f"requires mido {MIDO_VERSION}, found {mido_version}"
        )
    rhythm_labels_path = prepared / "labels.json"
    sources_path = prepared / "sources.private.json"
    prepared_manifest_path = prepared / "manifest.json"
    rhythm_labels = json.loads(rhythm_labels_path.read_text(encoding="utf-8"))
    sources = json.loads(sources_path.read_text(encoding="utf-8"))
    prepared_manifest = json.loads(
        prepared_manifest_path.read_text(encoding="utf-8")
    )
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
    if window_seconds <= 0.0:
        raise FreezeError("invalid prepared window duration")

    included: list[dict[str, object]] = []
    excluded: list[dict[str, str]] = []
    source_hash_rows: list[tuple[str, str]] = []
    for track in sorted(development_ids):
        row = sources_by_track[track]
        audio_path = prepared / "audio" / f"{track}.wav"
        if _sha256(audio_path) != str(row["audio_sha256"]):
            raise FreezeError(f"{track}: prepared audio hash mismatch")
        source = _resolve_source(asap_root, str(row["source_path"]))
        source_hash_rows.append((track, _sha256(source)))
        try:
            events = _key_events(source)
        except FreezeError as exc:
            excluded.append({"track": track, "reason": str(exc)})
            continue
        start = float(row["window_start_seconds"])
        end = start + window_seconds
        active = [key for time, key in events if time <= start]
        if not active:
            excluded.append(
                {"track": track, "reason": "no_active_signature"}
            )
            continue
        selected = active[-1]
        if any(
            key != selected
            for time, key in events
            if start < time < end
        ):
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
    if len(included) < MIN_TRACKS:
        raise FreezeError(
            f"only {len(included)} eligible tracks, require {MIN_TRACKS}"
        )
    if modes["major"] == 0 or modes["minor"] == 0:
        raise FreezeError("eligible set must contain both major and minor")

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
            "prepared_manifest_sha256": _sha256(prepared_manifest_path),
            "rhythm_labels_sha256": _sha256(rhythm_labels_path),
            "sources_private_sha256": _sha256(sources_path),
            "source_set_sha256": _source_set_sha256(source_hash_rows),
        },
        "runtime": {
            "python": platform.python_version(),
            "mido": mido_version,
        },
        "derivation_tool_sha256": _sha256(Path(__file__)),
        "labels_sha256": _sha256(labels_out),
        "development_input_count": len(development_ids),
        "included_count": len(included),
        "mode_counts": modes,
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
