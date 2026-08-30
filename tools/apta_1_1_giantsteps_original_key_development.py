#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare and evaluate the research-only GiantSteps centered-key split."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import subprocess
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable
from urllib.parse import urlparse
from xml.etree import ElementTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apta_1_1_giantsteps_key_validation as shared

FORMAT = "apta-1.1-giantsteps-original-key-development-1"
REPORT_FORMAT = "apta-1.1-giantsteps-original-key-report-1"
COMPARISON_FORMAT = "apta-1.1-giantsteps-original-key-comparison-1"
DATASET_REVISION = "6bcd492c825ac9b8597bc650a5f6fd18b6c43d2b"
SELECTION_SEED = "apta-1.1-giantsteps-original-centered-v1"
EXPECTED_COUNT = 604
MODE_TARGET = 48
PER_TONIC_QUOTA = 4
TRACK_COUNT = 2 * MODE_TARGET
DEVELOPMENT_ACCURACY_GATE = 0.70
MODE_ACCURACY_GATE = 0.60
PRIMARY_AUDIO_URL = "https://www.cp.jku.at/datasets/giantsteps/backup/{filename}"
BACKUP_AUDIO_URL = "https://geo-samples.beatport.com/lofi/{filename}"

TONIC_NAMES = tuple(shared.TONICS)
KEY_NAMES = tuple(
    f"{tonic} {mode}" for tonic in TONIC_NAMES for mode in ("major", "minor")
)
ENHARMONIC = {
    "c": "c",
    "b#": "c",
    "c#": "c#",
    "db": "c#",
    "d": "d",
    "d#": "d#",
    "eb": "d#",
    "e": "e",
    "fb": "e",
    "e#": "f",
    "f": "f",
    "f#": "f#",
    "gb": "f#",
    "g": "g",
    "g#": "g#",
    "ab": "g#",
    "a": "a",
    "a#": "a#",
    "bb": "a#",
    "b": "b",
    "cb": "b",
}


@dataclass(frozen=True)
class Candidate:
    source_id: str
    key_name: str
    key_tonic: int
    key_mode: str
    transport_md5: str


def normalize_key(value: str) -> str:
    parts = value.strip().replace("♭", "b").replace("♯", "#").casefold().split()
    if len(parts) != 2 or parts[1] not in {"major", "minor"}:
        raise shared.ValidationError("original key label must contain tonic and mode")
    tonic = ENHARMONIC.get(parts[0])
    if tonic is None:
        raise shared.ValidationError("original key label uses an unsupported tonic")
    return f"{tonic} {parts[1]}"


def _clean_exact_checkout(root: Path) -> None:
    shared._require_checkout_revision(root, DATASET_REVISION, "original dataset")
    for cached in (False, True):
        command = [
            "git",
            "-c",
            "core.filemode=false",
            "-C",
            str(root),
            "diff",
        ]
        if cached:
            command.append("--cached")
        command.extend(
            ["--quiet", "--ignore-cr-at-eol", "--no-ext-diff", "--ignore-submodules", "--"]
        )
        try:
            completed = subprocess.run(
                command,
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        except OSError as exc:
            raise shared.ValidationError("cannot inspect original dataset checkout") from exc
        if completed.returncode == 1:
            raise shared.ValidationError("original dataset checkout has tracked modifications")
        if completed.returncode != 0:
            raise shared.ValidationError("cannot inspect original dataset checkout")


def _xlsx_cell_value(
    cell: ElementTree.Element,
    shared_strings: list[str],
    namespace: dict[str, str],
) -> str:
    if cell.get("t") == "inlineStr":
        return "".join(
            node.text or "" for node in cell.findall(".//main:t", namespace)
        )
    value = cell.find("main:v", namespace)
    text = "" if value is None else value.text or ""
    if cell.get("t") == "s":
        try:
            return shared_strings[int(text)]
        except (IndexError, ValueError) as exc:
            raise shared.ValidationError("sources workbook has an invalid shared string") from exc
    return text


def read_workbook_keys(path: Path) -> dict[str, str]:
    namespace = {"main": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
    try:
        with zipfile.ZipFile(path) as workbook:
            strings_root = ElementTree.fromstring(workbook.read("xl/sharedStrings.xml"))
            shared_strings = [
                "".join(node.text or "" for node in item.findall(".//main:t", namespace))
                for item in strings_root.findall("main:si", namespace)
            ]
            sheet = ElementTree.fromstring(workbook.read("xl/worksheets/sheet1.xml"))
    except (OSError, KeyError, zipfile.BadZipFile, ElementTree.ParseError) as exc:
        raise shared.ValidationError("cannot parse original sources.xlsx") from exc
    dimension = sheet.find("main:dimension", namespace)
    if dimension is None or dimension.get("ref") != "A1:H605":
        raise shared.ValidationError("sources workbook inventory geometry changed")
    rows = sheet.findall(".//main:sheetData/main:row", namespace)
    if len(rows) != EXPECTED_COUNT + 1:
        raise shared.ValidationError("sources workbook must contain 604 data rows")

    def cells(row: ElementTree.Element) -> dict[str, str]:
        result: dict[str, str] = {}
        for cell in row.findall("main:c", namespace):
            reference = cell.get("r", "")
            match = re.match(r"[A-Z]+", reference)
            if match is None:
                raise shared.ValidationError("sources workbook cell reference is invalid")
            result[match.group(0)] = _xlsx_cell_value(cell, shared_strings, namespace)
        return result

    header = cells(rows[0])
    if header.get("A") != "TRACK" or header.get("C") != "GLOBAL KEY" or header.get(
        "F"
    ) != "AUDIO LINK":
        raise shared.ValidationError("sources workbook headers changed")
    result: dict[str, str] = {}
    for row in rows[1:]:
        values = cells(row)
        track = values.get("A", "").strip()
        link = values.get("F", "").strip()
        key_name = normalize_key(values.get("C", ""))
        filename = PurePosixPath(urlparse(link).path).name
        expected_filename = f"{track}.LOFI.mp3"
        if not track.isdigit() or filename != expected_filename:
            raise shared.ValidationError("sources workbook audio link does not match track ID")
        source_id = filename[:-4]
        if source_id in result:
            raise shared.ValidationError("sources workbook contains a duplicate track ID")
        result[source_id] = key_name
    if len(result) != EXPECTED_COUNT:
        raise shared.ValidationError("sources workbook unique inventory changed")
    return result


def inventory(root: Path) -> list[Candidate]:
    _clean_exact_checkout(root)
    checksums = sorted((root / "md5").glob("*.md5"))
    annotations = sorted((root / "annotations" / "key").glob("*.key"))
    if len(checksums) != EXPECTED_COUNT or len(annotations) != EXPECTED_COUNT:
        raise shared.ValidationError("original dataset must contain 604 keys and checksums")
    workbook = read_workbook_keys(root / "sources.xlsx")
    annotation_ids = {path.stem for path in annotations}
    checksum_ids = {path.stem for path in checksums}
    if annotation_ids != checksum_ids or checksum_ids != set(workbook):
        raise shared.ValidationError("original dataset inventories do not exactly agree")
    rows: list[Candidate] = []
    for checksum in checksums:
        source_id = checksum.stem
        annotation_path = root / "annotations" / "key" / f"{source_id}.key"
        try:
            lines = annotation_path.read_text(encoding="utf-8-sig").splitlines()
        except OSError as exc:
            raise shared.ValidationError("cannot read original key annotation") from exc
        if len(lines) != 1 or not lines[0].strip():
            raise shared.ValidationError("original key annotation must contain one label")
        key_name = normalize_key(lines[0])
        if workbook[source_id] != key_name:
            raise shared.ValidationError("workbook and key annotation disagree")
        tonic_name, mode = key_name.split()
        rows.append(
            Candidate(
                source_id=source_id,
                key_name=key_name,
                key_tonic=shared.TONICS[tonic_name],
                key_mode=mode,
                transport_md5=shared._read_transport_md5(checksum),
            )
        )
    if len(rows) != EXPECTED_COUNT or {row.key_name for row in rows} != set(KEY_NAMES):
        raise shared.ValidationError("original key inventory or class coverage changed")
    return rows


def _stable_key(source_id: str) -> str:
    return hashlib.sha256(f"{SELECTION_SEED}:track:{source_id}".encode()).hexdigest()


def select_candidates(rows: Iterable[Candidate]) -> list[Candidate]:
    available = list(rows)
    if len({row.source_id for row in available}) != len(available):
        raise shared.ValidationError("candidate inventory contains duplicate source IDs")
    selected: list[Candidate] = []
    for mode in ("major", "minor"):
        mode_selected: list[Candidate] = []
        for tonic in range(12):
            group = sorted(
                (
                    row
                    for row in available
                    if row.key_mode == mode and row.key_tonic == tonic
                ),
                key=lambda row: (_stable_key(row.source_id), row.source_id),
            )
            mode_selected.extend(group[:PER_TONIC_QUOTA])
        chosen = {row.source_id for row in mode_selected}
        remaining = sorted(
            (
                row
                for row in available
                if row.key_mode == mode and row.source_id not in chosen
            ),
            key=lambda row: (_stable_key(row.source_id), row.source_id),
        )
        missing = MODE_TARGET - len(mode_selected)
        if missing < 0 or len(remaining) < missing:
            raise shared.ValidationError(f"cannot form frozen {mode} development quota")
        mode_selected.extend(remaining[:missing])
        if len(mode_selected) != MODE_TARGET:
            raise AssertionError("mode-balanced selection failed")
        selected.extend(mode_selected)
    selected.sort(key=lambda row: (row.key_tonic, row.key_mode, row.source_id))
    if len(selected) != TRACK_COUNT or len({row.source_id for row in selected}) != TRACK_COUNT:
        raise AssertionError("frozen selection is not unique")
    if {
        mode: sum(row.key_mode == mode for row in selected)
        for mode in ("major", "minor")
    } != {"major": MODE_TARGET, "minor": MODE_TARGET}:
        raise AssertionError("frozen selection is not mode-balanced")
    return selected


def selection_sha256(rows: Iterable[Candidate]) -> str:
    seal = [
        {
            "source_id": row.source_id,
            "key_name": row.key_name,
            "transport_md5": row.transport_md5,
        }
        for row in rows
    ]
    encoded = (json.dumps(seal, separators=(",", ":"), sort_keys=True) + "\n").encode()
    return hashlib.sha256(encoded).hexdigest()


def _download_audio(row: Candidate, destination: Path, curl: Path) -> None:
    if destination.is_file() and shared.md5_file(destination) == row.transport_md5:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".partial")
    filename = f"{row.source_id}.mp3"
    errors: list[str] = []
    for template in (PRIMARY_AUDIO_URL, BACKUP_AUDIO_URL):
        partial.unlink(missing_ok=True)
        try:
            subprocess.run(
                [
                    str(curl),
                    "--fail",
                    "--location",
                    "--silent",
                    "--show-error",
                    "--retry",
                    "2",
                    "--retry-all-errors",
                    "--retry-delay",
                    "1",
                    "--connect-timeout",
                    "20",
                    "--max-time",
                    "180",
                    "--user-agent",
                    "APTA-1.1-GiantSteps-original-development/1",
                    "--output",
                    str(partial),
                    template.format(filename=filename),
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            if shared.md5_file(partial) != row.transport_md5:
                errors.append("checksum mismatch")
                continue
            partial.replace(destination)
            return
        except (OSError, subprocess.CalledProcessError) as exc:
            errors.append(type(exc).__name__)
    partial.unlink(missing_ok=True)
    raise shared.ValidationError(
        "audio download failed through both dataset mirrors: " + ", ".join(errors)
    )


def prepare(
    dataset_root: Path,
    output: Path,
    ffmpeg_value: str,
    curl_value: str,
    frozen_utc: str,
) -> dict[str, Any]:
    if (output / "manifest.json").exists():
        raise shared.ValidationError("prepared output is already finalized")
    ffmpeg = shared._resolve_executable(ffmpeg_value)
    curl = shared._resolve_executable(curl_value)
    frozen = shared._validate_frozen_utc(frozen_utc)
    selected = select_candidates(inventory(dataset_root))
    seal_hash = selection_sha256(selected)
    output.mkdir(parents=True, exist_ok=True)
    labels: list[dict[str, Any]] = []
    private_sources: list[dict[str, Any]] = []
    seen_tracks: set[str] = set()
    for index, row in enumerate(selected, start=1):
        print(f"prepare {index}/{len(selected)} development", flush=True)
        source = output / "working" / f"source-{index:03d}.mp3"
        rendered = output / "working" / f"canonical-{index:03d}.wav"
        _download_audio(row, source, curl)
        shared._canonicalize(ffmpeg, source, rendered)
        audio_hash = shared.sha256_file(rendered)
        track = "track-" + audio_hash[:24]
        if track in seen_tracks:
            raise shared.ValidationError("duplicate canonical audio content in selection")
        seen_tracks.add(track)
        final_audio = output / "audio" / f"{track}.wav"
        final_audio.parent.mkdir(parents=True, exist_ok=True)
        rendered.replace(final_audio)
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
                "source_id": row.source_id,
                "transport_md5": row.transport_md5,
                "canonical_sha256": audio_hash,
            }
        )
    labels.sort(key=lambda row: row["track"])
    private_sources.sort(key=lambda row: row["track"])
    shared._write_json(output / "labels.json", labels)
    shared._write_json(output / "private-sources.json", private_sources)
    class_counts = {
        name: sum(
            row["key_tonic"] == shared.TONICS[name.split()[0]]
            and row["key_mode"] == name.split()[1]
            for row in labels
        )
        for name in KEY_NAMES
    }
    mode_counts = {
        mode: sum(row["key_mode"] == mode for row in labels)
        for mode in ("major", "minor")
    }
    if mode_counts != {"major": MODE_TARGET, "minor": MODE_TARGET}:
        raise AssertionError("prepared labels are not mode-balanced")
    manifest = {
        "format": FORMAT,
        "split": "development",
        "evidence_level": "local-research-development",
        "acceptance_claim": False,
        "research_only": True,
        "license_status": "not-explicit-in-source-repository",
        "frozen_utc": frozen,
        "dataset_revision": DATASET_REVISION,
        "selection_seed": SELECTION_SEED,
        "selection_sha256": seal_hash,
        "ffmpeg_version": shared._ffmpeg_version(ffmpeg),
        "curl_version": subprocess.run(
            [str(curl), "--version"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout.splitlines()[0],
        "sample_rate": shared.SAMPLE_RATE,
        "channels": shared.CHANNELS,
        "sample_width_bytes": shared.SAMPLE_WIDTH,
        "track_count": len(labels),
        "mode_counts": mode_counts,
        "class_counts": class_counts,
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
        raise shared.ValidationError("cannot read prepared original GiantSteps corpus") from exc
    if not isinstance(manifest, dict) or manifest.get("format") != FORMAT:
        raise shared.ValidationError(f"manifest format must be {FORMAT}")
    if manifest.get("dataset_revision") != DATASET_REVISION:
        raise shared.ValidationError("prepared dataset revision does not match protocol")
    if manifest.get("split") != "development" or manifest.get("research_only") is not True:
        raise shared.ValidationError("prepared corpus must remain research-only development")
    if manifest.get("labels_sha256") != shared.sha256_file(prepared / "labels.json"):
        raise shared.ValidationError("prepared labels hash does not match manifest")
    if not isinstance(labels, list) or len(labels) != TRACK_COUNT:
        raise shared.ValidationError("prepared development split must contain 96 tracks")
    ids = [row.get("track") for row in labels if isinstance(row, dict)]
    if ids != manifest.get("track_ids") or ids != sorted(ids) or len(ids) != len(set(ids)):
        raise shared.ValidationError("prepared labels do not exactly match sorted manifest IDs")
    mode_counts = {
        mode: sum(row.get("key_mode") == mode for row in labels)
        for mode in ("major", "minor")
    }
    if mode_counts != {"major": MODE_TARGET, "minor": MODE_TARGET}:
        raise shared.ValidationError("prepared labels are not mode-balanced")
    for row in labels:
        if row.get("split") != "development" or row.get("key_tonic") not in range(12):
            raise shared.ValidationError("prepared key label is invalid")
    return manifest, labels


def run_analysis(
    prepared: Path,
    analyzer: Path,
    output: Path,
    source_revision: str,
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
    run = {
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
    shared._write_json(output / "run.json", run)
    return run


def _by_mode(tracks: Iterable[dict[str, Any]]) -> dict[str, dict[str, int | float]]:
    rows = list(tracks)
    result: dict[str, dict[str, int | float]] = {}
    for mode in ("major", "minor"):
        subset = [row for row in rows if row["expected_mode"] == mode]
        correct = sum(bool(row["key_correct"]) for row in subset)
        result[mode] = {
            "track_count": len(subset),
            "key_correct": correct,
            "key_accuracy": correct / len(subset) if subset else 0.0,
        }
    return result


def evaluate(prepared: Path, inspector: Path, mapping: Path, report: Path) -> dict[str, Any]:
    _manifest, labels = load_prepared(prepared)
    try:
        import apta_1_1_export_acceptance_results as exporter
    except ImportError as exc:
        raise shared.ValidationError("cannot import the APTA result exporter") from exc
    mapped = exporter.read_mapping(mapping)
    if sorted(track for track, _path in mapped) != [row["track"] for row in labels]:
        raise shared.ValidationError("mapping IDs do not exactly match prepared labels")
    try:
        results = [
            exporter.parse_inspection(track, exporter.inspect_file(inspector, path))
            for track, path in mapped
        ]
    except exporter.ExportError as exc:
        raise shared.ValidationError("cannot export analyzed key results") from exc
    scored = shared.score_rows(labels, results)
    value = {
        "format": REPORT_FORMAT,
        "split": "development",
        "evidence_level": "local-research-development",
        "acceptance_claim": False,
        **scored,
        "by_mode": _by_mode(scored["tracks"]),
    }
    shared._write_json(report, value)
    return value


def _load_report(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise shared.ValidationError("cannot read original GiantSteps key report") from exc
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
    mode_summary = _by_mode(tracks)
    if value.get("by_mode") != mode_summary:
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
        "state_size_unchanged": state_delta_bytes == 0,
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
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--dataset-root", type=Path, required=True)
    prepare_parser.add_argument("--output", type=Path, required=True)
    prepare_parser.add_argument("--ffmpeg", default="ffmpeg")
    prepare_parser.add_argument("--curl", default="curl")
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
    compare_parser.add_argument("--result-pool-delta-bytes", type=int, required=True)
    compare_parser.add_argument("--resonator-delta", type=int, required=True)
    compare_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            value = prepare(
                args.dataset_root,
                args.output,
                args.ffmpeg,
                args.curl,
                args.frozen_utc,
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
