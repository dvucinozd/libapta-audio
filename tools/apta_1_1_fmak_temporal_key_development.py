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
COMPARISON_FORMAT = "apta-1.1-fmak-temporal-key-comparison-1"
SELECTION_SEED = "apta-1.1-fmak-temporal-chord-v1"
METADATA_COUNT = 5489
ELIGIBLE_COUNT = 695
PER_CLASS_QUOTA = 4
TRACK_COUNT = 96
DEVELOPMENT_ACCURACY_GATE = 0.70
MODE_ACCURACY_GATE = 0.60
KEY_STATE_DELTA_LIMIT = 128
WORKSPACE_DELTA_LIMIT = 128
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


def _mode_summary(tracks: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for mode in ("major", "minor"):
        subset = [row for row in tracks if row["expected_mode"] == mode]
        correct = sum(bool(row["key_correct"]) for row in subset)
        result[mode] = {
            "track_count": len(subset),
            "key_correct": correct,
            "key_accuracy": correct / len(subset),
        }
    return result


def _load_report(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise shared.ValidationError("cannot read FMAK temporal-key report") from exc
    if not isinstance(value, dict) or value.get("format") != REPORT_FORMAT:
        raise shared.ValidationError(f"report format must be {REPORT_FORMAT}")
    tracks = value.get("tracks")
    if not isinstance(tracks, list) or len(tracks) != TRACK_COUNT:
        raise shared.ValidationError("report must contain 96 track rows")
    ids: list[str] = []
    for row in tracks:
        if not isinstance(row, dict):
            raise shared.ValidationError("report track row must be an object")
        track = row.get("track")
        expected_tonic = row.get("expected_tonic")
        expected_mode = row.get("expected_mode")
        tonic = row.get("key_tonic")
        mode = row.get("key_mode")
        confidence = row.get("key_confidence")
        correct = tonic == expected_tonic and mode == expected_mode
        if not isinstance(track, str) or not track:
            raise shared.ValidationError("report track ID must be non-empty")
        if expected_tonic not in range(12) or tonic not in range(12):
            raise shared.ValidationError("report tonic is outside 0..11")
        if expected_mode not in {"major", "minor"} or mode not in {"major", "minor"}:
            raise shared.ValidationError("report mode is unsupported")
        if not isinstance(confidence, int) or not 0 <= confidence <= 100:
            raise shared.ValidationError("report confidence is outside 0..100")
        if row.get("key_correct") is not correct:
            raise shared.ValidationError("report correctness is inconsistent")
        ids.append(track)
    if ids != sorted(ids) or len(ids) != len(set(ids)):
        raise shared.ValidationError("report track IDs must be sorted and unique")
    if value.get("by_mode") != _mode_summary(tracks):
        raise shared.ValidationError("report mode summary is inconsistent")
    correct = sum(bool(row["key_correct"]) for row in tracks)
    overall = value.get("overall")
    if not isinstance(overall, dict) or overall.get("track_count") != TRACK_COUNT:
        raise shared.ValidationError("report overall summary is invalid")
    if overall.get("key_correct") != correct or abs(
        float(overall.get("key_accuracy", -1.0)) - correct / TRACK_COUNT
    ) > 1e-15:
        raise shared.ValidationError("report overall accuracy is inconsistent")
    return value


def compare_reports(
    baseline_path: Path,
    candidate_path: Path,
    baseline_revision: str,
    candidate_revision: str,
    candidate_flag: str,
    werror_pass: bool,
    sanitizer_pass: bool,
    default_bytes_unchanged: bool,
    state_delta_bytes: int,
    workspace_delta_bytes: int,
    result_pool_delta_bytes: int,
    resonator_delta: int,
    output: Path,
) -> dict[str, Any]:
    baseline = _load_report(baseline_path)
    candidate = _load_report(candidate_path)
    baseline_rows = {row["track"]: row for row in baseline["tracks"]}
    candidate_rows = {row["track"]: row for row in candidate["tracks"]}
    if set(baseline_rows) != set(candidate_rows):
        raise shared.ValidationError("baseline and candidate report IDs differ")
    fixes: list[str] = []
    breaks: list[str] = []
    changed: list[str] = []
    baseline_high_errors: set[str] = set()
    candidate_high_errors: set[str] = set()
    for track in sorted(baseline_rows):
        base = baseline_rows[track]
        cand = candidate_rows[track]
        if (base["key_tonic"], base["key_mode"]) != (
            cand["key_tonic"],
            cand["key_mode"],
        ):
            changed.append(track)
        if not base["key_correct"] and cand["key_correct"]:
            fixes.append(track)
        if base["key_correct"] and not cand["key_correct"]:
            breaks.append(track)
        if not base["key_correct"] and base["key_confidence"] >= shared.HIGH_CONFIDENCE:
            baseline_high_errors.add(track)
        if not cand["key_correct"] and cand["key_confidence"] >= shared.HIGH_CONFIDENCE:
            candidate_high_errors.add(track)
    baseline_accuracy = float(baseline["overall"]["key_accuracy"])
    candidate_accuracy = float(candidate["overall"]["key_accuracy"])
    major_accuracy = float(candidate["by_mode"]["major"]["key_accuracy"])
    minor_accuracy = float(candidate["by_mode"]["minor"]["key_accuracy"])
    new_high_errors = sorted(candidate_high_errors - baseline_high_errors)
    gates = {
        "development_accuracy_at_least_70_percent": candidate_accuracy
        >= DEVELOPMENT_ACCURACY_GATE,
        "major_accuracy_at_least_60_percent": major_accuracy >= MODE_ACCURACY_GATE,
        "minor_accuracy_at_least_60_percent": minor_accuracy >= MODE_ACCURACY_GATE,
        "accuracy_improved": candidate_accuracy > baseline_accuracy,
        "fixes_exceed_breaks": len(fixes) > len(breaks),
        "no_new_high_confidence_errors": not new_high_errors,
        "werror_pass": werror_pass,
        "sanitizer_pass": sanitizer_pass,
        "default_bytes_unchanged": default_bytes_unchanged,
        "key_state_delta_within_128_bytes": 0 <= state_delta_bytes <= KEY_STATE_DELTA_LIMIT,
        "workspace_delta_within_128_bytes": 0
        <= workspace_delta_bytes
        <= WORKSPACE_DELTA_LIMIT,
        "result_pool_unchanged": result_pool_delta_bytes == 0,
        "no_new_resonators": resonator_delta == 0,
    }
    normalized_flag = candidate_flag.strip()
    if not normalized_flag:
        raise shared.ValidationError("candidate flag must not be empty")
    value = {
        "format": COMPARISON_FORMAT,
        "split": "development",
        "evidence_level": "local-research-development",
        "acceptance_claim": False,
        "baseline_revision": shared._full_revision(
            baseline_revision, "baseline revision"
        ),
        "candidate_revision": shared._full_revision(
            candidate_revision, "candidate revision"
        ),
        "candidate_flags": [normalized_flag],
        "track_count": TRACK_COUNT,
        "baseline_key_accuracy": baseline_accuracy,
        "candidate_key_accuracy": candidate_accuracy,
        "candidate_major_accuracy": major_accuracy,
        "candidate_minor_accuracy": minor_accuracy,
        "fix_count": len(fixes),
        "break_count": len(breaks),
        "changed_verdict_count": len(changed),
        "new_high_confidence_error_count": len(new_high_errors),
        "resource_delta": {
            "key_state_bytes": state_delta_bytes,
            "workspace_bytes": workspace_delta_bytes,
            "result_pool_bytes": result_pool_delta_bytes,
            "resonator_count": resonator_delta,
        },
        "fixes": fixes,
        "breaks": breaks,
        "changed_verdicts": changed,
        "new_high_confidence_errors": new_high_errors,
        "gates": gates,
        "holdout_eligible": all(gates.values()),
    }
    shared._write_json(output, value)
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
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("--baseline-report", type=Path, required=True)
    compare_parser.add_argument("--candidate-report", type=Path, required=True)
    compare_parser.add_argument("--baseline-revision", required=True)
    compare_parser.add_argument("--candidate-revision", required=True)
    compare_parser.add_argument("--candidate-flag", required=True)
    compare_parser.add_argument("--werror-pass", action="store_true")
    compare_parser.add_argument("--sanitizer-pass", action="store_true")
    compare_parser.add_argument("--default-bytes-unchanged", action="store_true")
    compare_parser.add_argument("--state-delta-bytes", type=int, required=True)
    compare_parser.add_argument("--workspace-delta-bytes", type=int, required=True)
    compare_parser.add_argument("--result-pool-delta-bytes", type=int, required=True)
    compare_parser.add_argument("--resonator-delta", type=int, required=True)
    compare_parser.add_argument("--output", type=Path, required=True)
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
        elif args.command == "evaluate":
            value = evaluate(args.prepared, args.inspector, args.mapping, args.report)
        else:
            value = compare_reports(
                args.baseline_report,
                args.candidate_report,
                args.baseline_revision,
                args.candidate_revision,
                args.candidate_flag,
                args.werror_pass,
                args.sanitizer_pass,
                args.default_bytes_unchanged,
                args.state_delta_bytes,
                args.workspace_delta_bytes,
                args.result_pool_delta_bytes,
                args.resonator_delta,
                args.output,
            )
    except (OSError, shared.ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(value, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
