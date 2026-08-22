#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Export frozen APTA 1.1 DJ acceptance rows from analyzed .apta files."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

OUTPUT_FIELDS = [
    "track",
    "key_tonic",
    "key_mode",
    "key_confidence",
    "meter_numerator",
    "meter_denominator",
    "meter_confidence",
    "downbeat_frame",
    "downbeat_confidence",
    "beat_period_frames",
    "grid_confidence",
]
MODE_NAMES = {1: "major", 2: "minor"}


class ExportError(ValueError):
    pass


def load_manifest_ids(path: Path) -> list[str]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ExportError(f"cannot read manifest: {exc}") from exc
    ids = value.get("track_ids") if isinstance(value, dict) else None
    if not isinstance(ids, list) or any(not isinstance(item, str) or not item for item in ids):
        raise ExportError("manifest track_ids must be a non-empty string array")
    if ids != sorted(ids) or len(ids) != len(set(ids)):
        raise ExportError("manifest track_ids must be sorted and unique")
    return ids


def read_mapping(path: Path) -> list[tuple[str, Path]]:
    rows: list[tuple[str, Path]] = []
    seen_tracks: set[str] = set()
    seen_paths: set[Path] = set()
    try:
        with path.open("r", encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source)
            if not {"track", "path"}.issubset(reader.fieldnames or []):
                raise ExportError("mapping CSV requires track,path columns")
            for line, raw in enumerate(reader, start=2):
                track = (raw.get("track") or "").strip()
                local = (raw.get("path") or "").strip()
                if not track or not local:
                    raise ExportError(f"mapping:{line}: empty track or path")
                file_path = Path(local)
                if track in seen_tracks:
                    raise ExportError(f"mapping:{line}: duplicate track {track}")
                if file_path in seen_paths:
                    raise ExportError(f"mapping:{line}: duplicate .apta path {local}")
                seen_tracks.add(track)
                seen_paths.add(file_path)
                rows.append((track, file_path))
    except OSError as exc:
        raise ExportError(f"cannot read mapping: {exc}") from exc
    return rows


def inspect_file(inspector: Path, path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise ExportError(f"missing analyzed file: {path}")
    try:
        completed = subprocess.run(
            [str(inspector), str(path), "--json"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "stderr", "") or str(exc)
        raise ExportError(f"apta-inspect failed for {path}: {detail.strip()}") from exc
    try:
        value = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise ExportError(f"invalid apta-inspect JSON for {path}") from exc
    if not isinstance(value, dict):
        raise ExportError(f"apta-inspect output for {path} is not an object")
    return value


def _required_object(value: dict[str, Any], name: str) -> dict[str, Any]:
    section = value.get(name)
    if not isinstance(section, dict):
        raise ExportError(f"required section {name} is unavailable")
    return section


def parse_inspection(track: str, value: dict[str, Any]) -> dict[str, str | int]:
    key = _required_object(value, "MKEY")
    meter = _required_object(value, "MTRD")
    grid = value.get("GGRD")
    if not isinstance(grid, dict):
        grid = value.get("LGRD")
    if not isinstance(grid, dict):
        raise ExportError("required global/local beatgrid is unavailable")

    try:
        tonic = int(key["tonic"])
        mode_value = int(key["mode"])
        key_confidence = int(key["confidence"])
        numerator = int(meter["numerator"])
        denominator = int(meter["denominator"])
        meter_confidence = int(meter["confidence"])
        downbeat = int(meter["downbeat_frame"])
        whole = int(grid["period_whole_frames"])
        fraction = int(grid["period_fraction_q32"])
        grid_confidence = int(grid["confidence"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ExportError("inspection JSON is missing required numeric fields") from exc

    mode = MODE_NAMES.get(mode_value)
    if mode is None:
        raise ExportError(f"unsupported key mode {mode_value}")
    if not 0 <= tonic <= 11:
        raise ExportError(f"invalid key tonic {tonic}")
    if numerator not in {3, 4} or denominator != 4:
        raise ExportError(f"unsupported meter {numerator}/{denominator}")
    for name, confidence in (
        ("key", key_confidence),
        ("meter", meter_confidence),
        ("grid", grid_confidence),
    ):
        if not 0 <= confidence <= 100:
            raise ExportError(f"{name} confidence is outside 0..100")
    if downbeat < 0 or whole <= 0 or not 0 <= fraction <= 0xFFFFFFFF:
        raise ExportError("invalid downbeat/grid frame values")

    period = whole + fraction / float(1 << 32)
    return {
        "track": track,
        "key_tonic": tonic,
        "key_mode": mode,
        "key_confidence": key_confidence,
        "meter_numerator": numerator,
        "meter_denominator": denominator,
        "meter_confidence": meter_confidence,
        "downbeat_frame": downbeat,
        "downbeat_confidence": meter_confidence,
        "beat_period_frames": format(period, ".12g"),
        "grid_confidence": grid_confidence,
    }


def export(inspector: Path, mapping: Path, manifest: Path, output: Path) -> int:
    expected_ids = load_manifest_ids(manifest)
    mapped = read_mapping(mapping)
    mapped_ids = sorted(track for track, _path in mapped)
    if mapped_ids != expected_ids:
        raise ExportError("mapping track IDs do not exactly match the frozen manifest")

    rows = [parse_inspection(track, inspect_file(inspector, path)) for track, path in mapped]
    rows.sort(key=lambda row: str(row["track"]))
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=OUTPUT_FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return len(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--mapping", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        count = export(args.inspector, args.mapping, args.manifest, args.output)
    except ExportError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(f"exported {count} APTA 1.1 acceptance rows")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
