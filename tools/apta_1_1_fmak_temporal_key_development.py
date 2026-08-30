#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare and evaluate the research-only FMAK temporal-key split."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apta_1_1_giantsteps_key_validation as shared

FORMAT = "apta-1.1-fmak-temporal-key-development-1"
REPORT_FORMAT = "apta-1.1-fmak-temporal-key-report-1"
SELECTION_SEED = "apta-1.1-fmak-temporal-chord-v1"
METADATA_COUNT = 5489
ELIGIBLE_COUNT = 695
PER_CLASS_QUOTA = 4
TRACK_COUNT = 96
METADATA_MD5 = "d80a03bc8659edc60e335bd7f6bdf12a"
METADATA_SHA256 = "7ec4bd22eb5ff7958fbf9d8c44869f955fc6b50b67896b229665b3b92e80190d"
ARCHIVE_SIZE = 4_150_442_299
ARCHIVE_MD5 = "b86f6414820c1422b2c6cdf87be1ef3a"
SELECTION_SHA256 = "44a78001a6fbdc92975eed0594e5693b8cfc0dbc54c4b31385ec63ec833772cc"
SOURCE_REVISION = "23dbccd73584af14c65528298b17f64f14ec11d4"

ENHARMONIC = {
    "c": 0,
    "b#": 0,
    "c#": 1,
    "db": 1,
    "d": 2,
    "d#": 3,
    "eb": 3,
    "e": 4,
    "fb": 4,
    "e#": 5,
    "f": 5,
    "f#": 6,
    "gb": 6,
    "g": 7,
    "g#": 8,
    "ab": 8,
    "a": 9,
    "a#": 10,
    "bb": 10,
    "b": 11,
    "cb": 11,
}


@dataclass(frozen=True)
class Candidate:
    source_id: int
    key_tonic: int
    key_mode: str


def normalize_key(value: str) -> tuple[int, str]:
    parts = value.strip().replace("♭", "b").replace("♯", "#").casefold().split()
    if len(parts) != 2 or parts[1] not in {"major", "minor"}:
        raise shared.ValidationError("FMAK label must contain tonic and mode")
    tonic = ENHARMONIC.get(parts[0])
    if tonic is None:
        raise shared.ValidationError("FMAK label uses an unsupported tonic")
    return tonic, parts[1]


def inventory(metadata: Path) -> list[Candidate]:
    if not metadata.is_file() or shared.md5_file(metadata) != METADATA_MD5:
        raise shared.ValidationError("FMAK metadata MD5 does not match version 1.0")
    if shared.sha256_file(metadata) != METADATA_SHA256:
        raise shared.ValidationError("FMAK metadata SHA-256 does not match protocol")
    try:
        with metadata.open("r", encoding="utf-8-sig", newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ["track_id", "spotify_uri", "key_and_mode"]:
                raise shared.ValidationError("FMAK metadata headers changed")
            rows: list[Candidate] = []
            for source in reader:
                token = source["track_id"].strip()
                if not token.isdecimal() or int(token) <= 0:
                    raise shared.ValidationError("FMAK track ID must be a positive integer")
                tonic, mode = normalize_key(source["key_and_mode"])
                rows.append(Candidate(int(token), tonic, mode))
    except OSError as exc:
        raise shared.ValidationError("cannot read FMAK metadata") from exc
    if len(rows) != METADATA_COUNT or len({row.source_id for row in rows}) != len(rows):
        raise shared.ValidationError("FMAK version 1.0 inventory changed")
    if {(row.key_tonic, row.key_mode) for row in rows} != {
        (tonic, mode) for tonic in range(12) for mode in ("major", "minor")
    }:
        raise shared.ValidationError("FMAK key-class coverage changed")
    return rows


def _stable_key(row: Candidate) -> tuple[str, int]:
    digest = hashlib.sha256(
        f"{SELECTION_SEED}:track:{row.source_id}".encode()
    ).hexdigest()
    return digest, row.source_id


def select_candidates(rows: Iterable[Candidate]) -> list[Candidate]:
    eligible = [row for row in rows if row.source_id < 20_000]
    if len(eligible) != ELIGIBLE_COUNT:
        raise shared.ValidationError("FMAK 000-019 eligible inventory changed")
    selected: list[Candidate] = []
    for mode in ("major", "minor"):
        for tonic in range(12):
            group = sorted(
                (
                    row
                    for row in eligible
                    if row.key_tonic == tonic and row.key_mode == mode
                ),
                key=_stable_key,
            )
            if len(group) < PER_CLASS_QUOTA:
                raise shared.ValidationError("FMAK class cannot satisfy frozen quota")
            selected.extend(group[:PER_CLASS_QUOTA])
    selected.sort(key=lambda row: (row.key_tonic, row.key_mode, row.source_id))
    if len(selected) != TRACK_COUNT or len({row.source_id for row in selected}) != TRACK_COUNT:
        raise AssertionError("FMAK selection is not 96 unique tracks")
    return selected


def selection_sha256(rows: Iterable[Candidate]) -> str:
    seal = [
        {
            "source_id": str(row.source_id),
            "key_tonic": row.key_tonic,
            "key_mode": row.key_mode,
        }
        for row in rows
    ]
    encoded = (json.dumps(seal, separators=(",", ":"), sort_keys=True) + "\n").encode()
    return hashlib.sha256(encoded).hexdigest()


def preflight(metadata: Path) -> dict[str, Any]:
    selected = select_candidates(inventory(metadata))
    if selection_sha256(selected) != SELECTION_SHA256:
        raise shared.ValidationError("FMAK frozen selection seal changed")
    class_counts = {
        f"{tonic}:{mode}": sum(
            row.key_tonic == tonic and row.key_mode == mode for row in selected
        )
        for tonic in range(12)
        for mode in ("major", "minor")
    }
    return {
        "format": FORMAT,
        "operation": "preflight",
        "acceptance_claim": False,
        "research_only": True,
        "metadata_count": METADATA_COUNT,
        "eligible_count": ELIGIBLE_COUNT,
        "track_count": len(selected),
        "mode_counts": {
            mode: sum(row.key_mode == mode for row in selected)
            for mode in ("major", "minor")
        },
        "class_counts": class_counts,
        "selection_sha256": selection_sha256(selected),
        "source_revision": SOURCE_REVISION,
        "archive_size": ARCHIVE_SIZE,
        "archive_md5": ARCHIVE_MD5,
    }


def _archive_members(
    archive: zipfile.ZipFile, selected: Iterable[Candidate]
) -> dict[int, zipfile.ZipInfo]:
    wanted = {row.source_id: f"{row.source_id:06d}.mp3".casefold() for row in selected}
    by_name: dict[str, list[zipfile.ZipInfo]] = {}
    for info in archive.infolist():
        if info.is_dir():
            continue
        by_name.setdefault(PurePosixPath(info.filename).name.casefold(), []).append(info)
    result: dict[int, zipfile.ZipInfo] = {}
    for source_id, filename in wanted.items():
        matches = by_name.get(filename, [])
        if len(matches) != 1:
            raise shared.ValidationError("FMAK archive selected-member inventory changed")
        result[source_id] = matches[0]
    return result


def prepare(
    metadata: Path,
    archive_path: Path,
    output: Path,
    ffmpeg_value: str,
    frozen_utc: str,
) -> dict[str, Any]:
    if (output / "manifest.json").exists():
        raise shared.ValidationError("prepared FMAK output is already finalized")
    if not archive_path.is_file() or archive_path.stat().st_size != ARCHIVE_SIZE:
        raise shared.ValidationError("FMAK 000-019 archive size does not match protocol")
    if shared.md5_file(archive_path) != ARCHIVE_MD5:
        raise shared.ValidationError("FMAK 000-019 archive MD5 does not match protocol")
    selected = select_candidates(inventory(metadata))
    if selection_sha256(selected) != SELECTION_SHA256:
        raise shared.ValidationError("FMAK frozen selection seal changed")
    ffmpeg = shared._resolve_executable(ffmpeg_value)
    frozen = shared._validate_frozen_utc(frozen_utc)
    output.mkdir(parents=True, exist_ok=True)
    labels: list[dict[str, Any]] = []
    private_sources: list[dict[str, Any]] = []
    seen_tracks: set[str] = set()
    try:
        with zipfile.ZipFile(archive_path) as archive:
            members = _archive_members(archive, selected)
            for index, row in enumerate(selected, start=1):
                print(f"prepare {index}/{len(selected)} development", flush=True)
                source = output / "working" / f"source-{index:03d}.mp3"
                rendered = output / "working" / f"canonical-{index:03d}.wav"
                source.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(members[row.source_id]) as incoming, source.open("wb") as target:
                    shutil.copyfileobj(incoming, target)
                shared._canonicalize(ffmpeg, source, rendered)
                audio_hash = shared.sha256_file(rendered)
                track = "track-" + audio_hash[:24]
                if track in seen_tracks:
                    raise shared.ValidationError("duplicate canonical FMAK audio")
                seen_tracks.add(track)
                final_audio = output / "audio" / f"{track}.wav"
                final_audio.parent.mkdir(parents=True, exist_ok=True)
                rendered.replace(final_audio)
                source.unlink(missing_ok=True)
                labels.append(
                    {
                        "track": track,
                        "split": "development",
                        "key_tonic": row.key_tonic,
                        "key_mode": row.key_mode,
                    }
                )
                private_sources.append(
                    {
                        "track": track,
                        "source_id": str(row.source_id),
                        "canonical_sha256": audio_hash,
                    }
                )
    except (OSError, zipfile.BadZipFile) as exc:
        raise shared.ValidationError("cannot materialize FMAK archive") from exc
    labels.sort(key=lambda row: row["track"])
    private_sources.sort(key=lambda row: row["track"])
    shared._write_json(output / "labels.json", labels)
    shared._write_json(output / "private-sources.json", private_sources)
    manifest = {
        "format": FORMAT,
        "split": "development",
        "evidence_level": "local-research-development",
        "acceptance_claim": False,
        "research_only": True,
        "license_status": "per-track-artist-license-no-redistribution",
        "frozen_utc": frozen,
        "source_revision": SOURCE_REVISION,
        "metadata_md5": METADATA_MD5,
        "metadata_sha256": METADATA_SHA256,
        "archive_size": ARCHIVE_SIZE,
        "archive_md5": ARCHIVE_MD5,
        "selection_seed": SELECTION_SEED,
        "selection_sha256": selection_sha256(selected),
        "ffmpeg_version": shared._ffmpeg_version(ffmpeg),
        "sample_rate": shared.SAMPLE_RATE,
        "channels": shared.CHANNELS,
        "sample_width_bytes": shared.SAMPLE_WIDTH,
        "track_count": len(labels),
        "mode_counts": {
            mode: sum(row["key_mode"] == mode for row in labels)
            for mode in ("major", "minor")
        },
        "track_ids": [row["track"] for row in labels],
        "labels_sha256": shared.sha256_file(output / "labels.json"),
        "private_sources_sha256": shared.sha256_file(output / "private-sources.json"),
    }
    shared._write_json(output / "manifest.json", manifest)
    return manifest


def load_prepared(prepared: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    try:
        manifest = json.loads((prepared / "manifest.json").read_text(encoding="utf-8"))
        labels = json.loads((prepared / "labels.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise shared.ValidationError("cannot read prepared FMAK corpus") from exc
    if not isinstance(manifest, dict) or manifest.get("format") != FORMAT:
        raise shared.ValidationError(f"manifest format must be {FORMAT}")
    if manifest.get("selection_sha256") != SELECTION_SHA256:
        raise shared.ValidationError("prepared FMAK selection does not match protocol")
    if manifest.get("labels_sha256") != shared.sha256_file(prepared / "labels.json"):
        raise shared.ValidationError("prepared FMAK labels hash mismatch")
    if not isinstance(labels, list) or len(labels) != TRACK_COUNT:
        raise shared.ValidationError("prepared FMAK corpus must contain 96 tracks")
    ids = [row.get("track") for row in labels if isinstance(row, dict)]
    if ids != sorted(ids) or ids != manifest.get("track_ids") or len(ids) != len(set(ids)):
        raise shared.ValidationError("prepared FMAK IDs do not match manifest")
    return manifest, labels


def run_analysis(
    prepared: Path, analyzer: Path, output: Path, source_revision: str
) -> dict[str, Any]:
    _manifest, labels = load_prepared(prepared)
    revision = shared._full_revision(source_revision)
    if not analyzer.is_file():
        raise shared.ValidationError("analyzer executable not found")
    epoch = os.environ.get("SOURCE_DATE_EPOCH", "")
    if not epoch.isdigit():
        raise shared.ValidationError("SOURCE_DATE_EPOCH must be a non-negative integer")
    output.mkdir(parents=True, exist_ok=True)
    analyzed = output / "analyzed"
    analyzed.mkdir(parents=True, exist_ok=True)
    mappings: list[dict[str, str]] = []
    outputs: list[dict[str, str]] = []
    for index, row in enumerate(labels, start=1):
        track = str(row["track"])
        audio = prepared / "audio" / f"{track}.wav"
        if not audio.is_file() or "track-" + shared.sha256_file(audio)[:24] != track:
            raise shared.ValidationError(f"prepared audio hash does not match {track}")
        target = analyzed / f"{track}.apta"
        print(f"analyze {index}/{len(labels)} {track}", flush=True)
        try:
            subprocess.run(
                [str(analyzer), str(audio), "--output", str(target), "--features", "all"],
                check=True,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            raise shared.ValidationError(f"analysis failed for {track}") from exc
        mappings.append({"track": track, "path": str(target.resolve())})
        outputs.append({"track": track, "apta_sha256": shared.sha256_file(target)})
    mapping_path = output / "mapping.csv"
    with mapping_path.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=("track", "path"), lineterminator="\n")
        writer.writeheader()
        writer.writerows(mappings)
    value = {
        "format": FORMAT,
        "split": "development",
        "evidence_level": "local-research-development",
        "acceptance_claim": False,
        "source_revision": revision,
        "source_date_epoch": int(epoch),
        "analyzer_sha256": shared.sha256_file(analyzer),
        "manifest_sha256": shared.sha256_file(prepared / "manifest.json"),
        "mapping_sha256": shared.sha256_file(mapping_path),
        "track_count": len(labels),
        "complete": True,
        "outputs": outputs,
    }
    shared._write_json(output / "run.json", value)
    return value


def evaluate(prepared: Path, inspector: Path, mapping: Path, report: Path) -> dict[str, Any]:
    _manifest, labels = load_prepared(prepared)
    try:
        import apta_1_1_export_acceptance_results as exporter
    except ImportError as exc:
        raise shared.ValidationError("cannot import the APTA result exporter") from exc
    mapped = exporter.read_mapping(mapping)
    if sorted(track for track, _path in mapped) != [row["track"] for row in labels]:
        raise shared.ValidationError("mapping IDs do not exactly match prepared FMAK labels")
    try:
        results = [
            exporter.parse_inspection(track, exporter.inspect_file(inspector, path))
            for track, path in mapped
        ]
    except exporter.ExportError as exc:
        raise shared.ValidationError("cannot export analyzed FMAK key results") from exc
    scored = shared.score_rows(labels, results)
    tracks = scored["tracks"]
    by_mode = {}
    for mode in ("major", "minor"):
        subset = [row for row in tracks if row["expected_mode"] == mode]
        correct = sum(bool(row["key_correct"]) for row in subset)
        by_mode[mode] = {
            "track_count": len(subset),
            "key_correct": correct,
            "key_accuracy": correct / len(subset),
        }
    value = {
        "format": REPORT_FORMAT,
        "split": "development",
        "evidence_level": "local-research-development",
        "acceptance_claim": False,
        **scored,
        "by_mode": by_mode,
    }
    shared._write_json(report, value)
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    preflight_parser = subparsers.add_parser("preflight")
    preflight_parser.add_argument("--metadata", type=Path, required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--metadata", type=Path, required=True)
    prepare_parser.add_argument("--archive", type=Path, required=True)
    prepare_parser.add_argument("--output", type=Path, required=True)
    prepare_parser.add_argument("--ffmpeg", default="ffmpeg")
    prepare_parser.add_argument("--frozen-utc", required=True)
    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("--prepared", type=Path, required=True)
    run_parser.add_argument("--analyzer", type=Path, required=True)
    run_parser.add_argument("--output", type=Path, required=True)
    run_parser.add_argument("--source-revision", required=True)
    evaluate_parser = subparsers.add_parser("evaluate")
    evaluate_parser.add_argument("--prepared", type=Path, required=True)
    evaluate_parser.add_argument("--inspector", type=Path, required=True)
    evaluate_parser.add_argument("--mapping", type=Path, required=True)
    evaluate_parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "preflight":
            value = preflight(args.metadata)
        elif args.command == "prepare":
            value = prepare(
                args.metadata, args.archive, args.output, args.ffmpeg, args.frozen_utc
            )
        elif args.command == "run":
            value = run_analysis(
                args.prepared, args.analyzer, args.output, args.source_revision
            )
        else:
            value = evaluate(args.prepared, args.inspector, args.mapping, args.report)
    except (OSError, shared.ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(value, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
