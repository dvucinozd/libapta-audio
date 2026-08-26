#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Convert the local listening-workbench export to corpus-freezer staging.

The input may contain titles and artists. Output deliberately retains only the
canonical WAV name and the labels required by apta_1_1_freeze_corpus.py.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path, PurePosixPath

FIELDS = [
    "source",
    "key_tonic",
    "key_mode",
    "meter_numerator",
    "meter_denominator",
    "downbeat_frame",
    "beat_period_frames",
]
REQUIRED = {
    "track_id",
    "file",
    "key_tonic",
    "key_mode",
    "meter",
    "bpm",
    "downbeat_seconds",
    "exclude",
}


class ImportError(ValueError):
    pass


def convert(
    verification: Path,
    preparation_manifest: Path,
    output: Path,
) -> int:
    preparation = json.loads(preparation_manifest.read_text(encoding="utf-8"))
    sample_rate = int(preparation["canonical_audio"]["sample_rate"])
    prepared = {
        str(row["canonical"]): row for row in preparation.get("tracks", [])
    }
    if not prepared:
        raise ImportError("preparation manifest contains no tracks")

    converted: list[dict[str, str]] = []
    seen_tracks: set[str] = set()
    seen_files: set[str] = set()
    with verification.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        missing = REQUIRED - set(reader.fieldnames or [])
        if missing:
            raise ImportError(f"verification CSV missing columns: {sorted(missing)}")
        for line, row in enumerate(reader, start=2):
            if row["exclude"].strip() not in {"0", "false", "False"}:
                continue
            track_id = row["track_id"].strip()
            canonical = PurePosixPath(row["file"].replace("\\", "/")).name
            if not track_id or track_id in seen_tracks:
                raise ImportError(f"line {line}: empty or duplicate track_id")
            if canonical in seen_files or canonical not in prepared:
                raise ImportError(f"line {line}: unknown or duplicate canonical WAV")
            if track_id not in canonical:
                raise ImportError(f"line {line}: track_id does not match canonical WAV")
            try:
                tonic = int(row["key_tonic"])
                mode = row["key_mode"].strip().casefold()
                meter = int(row["meter"])
                bpm = float(row["bpm"])
                downbeat_seconds = float(row["downbeat_seconds"])
            except ValueError as exc:
                raise ImportError(f"line {line}: invalid label value") from exc
            if tonic not in range(12) or mode not in {"major", "minor"}:
                raise ImportError(f"line {line}: invalid key")
            if meter not in {3, 4} or bpm <= 0.0 or downbeat_seconds < 0.0:
                raise ImportError(f"line {line}: invalid rhythm label")

            seen_tracks.add(track_id)
            seen_files.add(canonical)
            converted.append(
                {
                    "source": canonical,
                    "key_tonic": str(tonic),
                    "key_mode": mode,
                    "meter_numerator": str(meter),
                    "meter_denominator": "4",
                    "downbeat_frame": str(round(downbeat_seconds * sample_rate)),
                    "beat_period_frames": format(sample_rate * 60.0 / bpm, ".9g"),
                }
            )

    if len(converted) != len(prepared):
        raise ImportError(
            f"verified rows ({len(converted)}) do not match prepared tracks ({len(prepared)})"
        )
    converted.sort(key=lambda row: row["source"])
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(converted)
    return len(converted)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verification", type=Path, required=True)
    parser.add_argument("--preparation-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        count = convert(args.verification, args.preparation_manifest, args.output)
    except (OSError, KeyError, TypeError, json.JSONDecodeError, ImportError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(f"wrote {count} verified staging rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
