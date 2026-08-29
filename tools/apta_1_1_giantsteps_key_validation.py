#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare and evaluate the pre-registered GiantSteps-MTG WP4 key split.

Audio, transport IDs, source mappings and prepared labels are local-only.  The
tool prints only opaque track IDs after preparation and requires an explicit
frozen revision before the one-shot holdout can be materialized.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import wave
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable

FORMAT = "apta-1.1-giantsteps-mtg-key-validation-1"
REPORT_FORMAT = "apta-1.1-giantsteps-mtg-key-report-1"
COMPARISON_FORMAT = "apta-1.1-giantsteps-mtg-key-comparison-1"
MTG_REVISION = "fd7b8c584f7bd6d720d170c325a6d42c9bf75a6b"
ORIGINAL_REVISION = "6bcd492c825ac9b8597bc650a5f6fd18b6c43d2b"
SELECTION_SEED = "apta-1.1-giantsteps-mtg-key-v1"
EXPECTED_MTG_COUNT = 1486
EXPECTED_ORIGINAL_COUNT = 604
EXPECTED_ELIGIBLE_COUNT = 1159
DEVELOPMENT_PER_CLASS = 4
HOLDOUT_PER_CLASS = 2
SAMPLE_RATE = 48_000
CHANNELS = 2
SAMPLE_WIDTH = 2
HIGH_CONFIDENCE = 75
DEVELOPMENT_ACCURACY_GATE = 0.70

TONICS = {
    "c": 0,
    "c#": 1,
    "d": 2,
    "d#": 3,
    "e": 4,
    "f": 5,
    "f#": 6,
    "g": 7,
    "g#": 8,
    "a": 9,
    "a#": 10,
    "b": 11,
}
KEY_NAMES = tuple(
    f"{tonic} {mode}" for tonic in TONICS for mode in ("major", "minor")
)
PRIMARY_AUDIO_URL = "https://geo-samples.beatport.com/lofi/{filename}"
BACKUP_AUDIO_URL = (
    "https://www.cp.jku.at/datasets/giantsteps/mtg_key_backup/{filename}"
)


class ValidationError(ValueError):
    pass


@dataclass(frozen=True)
class Candidate:
    source_id: str
    key_name: str
    key_tonic: int
    key_mode: str
    transport_md5: str


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def md5_file(path: Path) -> str:
    # Upstream uses MD5 only as a transport-integrity checksum.
    digest = hashlib.md5()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _full_revision(value: str, name: str = "source revision") -> str:
    normalized = value.strip().casefold()
    if len(normalized) != 40 or any(char not in "0123456789abcdef" for char in normalized):
        raise ValidationError(f"{name} must be a full lowercase Git SHA")
    return normalized


def _require_checkout_revision(root: Path, expected: str, name: str) -> None:
    try:
        completed = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValidationError(f"{name} must be a Git checkout") from exc
    observed = completed.stdout.strip().casefold()
    if observed != expected:
        raise ValidationError(f"{name} revision must be {expected}, found {observed}")


def _stable_key(kind: str, value: str) -> str:
    return hashlib.sha256(f"{SELECTION_SEED}:{kind}:{value}".encode()).hexdigest()


def _read_transport_md5(path: Path) -> str:
    try:
        value = path.read_text(encoding="ascii").strip().casefold()
    except OSError as exc:
        raise ValidationError("cannot read an upstream transport checksum") from exc
    if len(value) != 32 or any(char not in "0123456789abcdef" for char in value):
        raise ValidationError("upstream transport checksum is not a lowercase MD5")
    return value


def read_key_annotation(path: Path) -> tuple[str, str]:
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except OSError as exc:
        raise ValidationError("cannot read an upstream key annotation") from exc
    if not lines:
        raise ValidationError("upstream key annotation is empty")
    fields = lines[0].split("\t")
    if len(fields) < 2:
        raise ValidationError("upstream key annotation requires key and confidence")
    return fields[0].strip().casefold(), fields[1].strip()


def inventory(mtg_root: Path, original_root: Path) -> list[Candidate]:
    _require_checkout_revision(mtg_root, MTG_REVISION, "MTG dataset")
    _require_checkout_revision(original_root, ORIGINAL_REVISION, "original dataset")
    mtg_md5 = sorted((mtg_root / "md5").glob("*.md5"))
    original_ids = {path.stem for path in (original_root / "md5").glob("*.md5")}
    if len(mtg_md5) != EXPECTED_MTG_COUNT:
        raise ValidationError(
            f"MTG inventory must contain {EXPECTED_MTG_COUNT} IDs, found {len(mtg_md5)}"
        )
    if len(original_ids) != EXPECTED_ORIGINAL_COUNT:
        raise ValidationError(
            "original exclusion inventory must contain "
            f"{EXPECTED_ORIGINAL_COUNT} IDs, found {len(original_ids)}"
        )
    mtg_ids = {path.stem for path in mtg_md5}
    if mtg_ids & original_ids:
        raise ValidationError("MTG inventory overlaps the original GiantSteps test set")

    rows: list[Candidate] = []
    for checksum_path in mtg_md5:
        source_id = checksum_path.stem
        key_name, confidence = read_key_annotation(
            mtg_root / "annotations" / "key" / f"{source_id}.key"
        )
        parts = key_name.split()
        if len(parts) != 2 or parts[0] not in TONICS or parts[1] not in {"major", "minor"}:
            continue
        if confidence != "2":
            continue
        rows.append(
            Candidate(
                source_id=source_id,
                key_name=key_name,
                key_tonic=TONICS[parts[0]],
                key_mode=parts[1],
                transport_md5=_read_transport_md5(checksum_path),
            )
        )
    if len(rows) != EXPECTED_ELIGIBLE_COUNT:
        raise ValidationError(
            f"eligible inventory must contain {EXPECTED_ELIGIBLE_COUNT} rows, found {len(rows)}"
        )
    if {row.key_name for row in rows} != set(KEY_NAMES):
        raise ValidationError("eligible inventory does not cover all 24 key classes")
    return rows


def select_candidates(
    rows: Iterable[Candidate],
    development_per_class: int = DEVELOPMENT_PER_CLASS,
    holdout_per_class: int = HOLDOUT_PER_CLASS,
) -> dict[str, list[Candidate]]:
    selected: dict[str, list[Candidate]] = {"development": [], "holdout": []}
    by_key: dict[str, list[Candidate]] = {name: [] for name in KEY_NAMES}
    for row in rows:
        if row.key_name not in by_key:
            raise ValidationError(f"unsupported key class {row.key_name}")
        by_key[row.key_name].append(row)
    required = development_per_class + holdout_per_class
    for key_name in KEY_NAMES:
        candidates = sorted(
            by_key[key_name], key=lambda row: (_stable_key("track", row.source_id), row.source_id)
        )
        if len(candidates) < required:
            raise ValidationError(
                f"only {len(candidates)} eligible rows for {key_name}; {required} required"
            )
        selected["development"].extend(candidates[:development_per_class])
        selected["holdout"].extend(candidates[development_per_class:required])
    for split in selected:
        selected[split].sort(key=lambda row: (row.key_tonic, row.key_mode, row.source_id))
    development_ids = {row.source_id for row in selected["development"]}
    holdout_ids = {row.source_id for row in selected["holdout"]}
    if development_ids & holdout_ids:
        raise AssertionError("GiantSteps source leaked across splits")
    return selected


def selection_sha256(selected: dict[str, list[Candidate]]) -> str:
    seal = [
        {
            "source_id": row.source_id,
            "split": split,
            "key_name": row.key_name,
            "transport_md5": row.transport_md5,
        }
        for split in ("development", "holdout")
        for row in selected[split]
    ]
    encoded = (json.dumps(seal, separators=(",", ":"), sort_keys=True) + "\n").encode()
    return hashlib.sha256(encoded).hexdigest()


def _resolve_executable(value: str) -> Path:
    found = shutil.which(value)
    path = Path(found) if found else Path(value)
    if not path.is_file():
        raise ValidationError(f"executable not found: {value}")
    return path.resolve()


def _ffmpeg_version(ffmpeg: Path) -> str:
    try:
        completed = subprocess.run(
            [str(ffmpeg), "-version"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValidationError("cannot query ffmpeg version") from exc
    lines = completed.stdout.splitlines()
    if not lines:
        raise ValidationError("ffmpeg version output is empty")
    return lines[0]


def _download_audio(row: Candidate, destination: Path) -> None:
    if destination.is_file() and md5_file(destination) == row.transport_md5:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".partial")
    filename = f"{row.source_id}.mp3"
    errors: list[str] = []
    for template in (PRIMARY_AUDIO_URL, BACKUP_AUDIO_URL):
        partial.unlink(missing_ok=True)
        request = urllib.request.Request(
            template.format(filename=filename),
            headers={"User-Agent": "APTA-1.1-GiantSteps-validation/1"},
        )
        try:
            with urllib.request.urlopen(request, timeout=120) as source, partial.open("wb") as target:
                shutil.copyfileobj(source, target, 1024 * 1024)
            if md5_file(partial) != row.transport_md5:
                errors.append("checksum mismatch")
                continue
            partial.replace(destination)
            return
        except (OSError, urllib.error.URLError) as exc:
            errors.append(type(exc).__name__)
    partial.unlink(missing_ok=True)
    raise ValidationError(
        "audio download failed through both official dataset mirrors: " + ", ".join(errors)
    )


def _canonicalize(ffmpeg: Path, source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(
            [
                str(ffmpeg),
                "-v",
                "error",
                "-y",
                "-i",
                str(source),
                "-map_metadata",
                "-1",
                "-ar",
                str(SAMPLE_RATE),
                "-ac",
                str(CHANNELS),
                "-c:a",
                "pcm_s16le",
                str(target),
            ],
            check=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValidationError("ffmpeg failed while canonicalizing a selected track") from exc
    try:
        with wave.open(str(target), "rb") as wav:
            geometry = (wav.getframerate(), wav.getnchannels(), wav.getsampwidth())
            frames = wav.getnframes()
    except (OSError, wave.Error) as exc:
        raise ValidationError("canonical output is not a valid PCM WAV") from exc
    if geometry != (SAMPLE_RATE, CHANNELS, SAMPLE_WIDTH) or frames <= 0:
        raise ValidationError("canonical output does not match PCM16 stereo 48 kHz")


def _validate_frozen_utc(value: str) -> str:
    if not value.endswith("Z"):
        raise ValidationError("frozen UTC must end in Z")
    try:
        parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as exc:
        raise ValidationError("frozen UTC is not valid ISO-8601") from exc
    if parsed.utcoffset() is None or parsed.microsecond != 0:
        raise ValidationError("frozen UTC must use whole seconds")
    return value


def _write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def prepare(
    mtg_root: Path,
    original_root: Path,
    output: Path,
    split: str,
    ffmpeg_value: str,
    frozen_utc: str,
    open_holdout: bool = False,
    candidate_revision: str | None = None,
) -> dict[str, Any]:
    if split == "holdout":
        if not open_holdout:
            raise ValidationError("holdout materialization requires --open-holdout")
        if candidate_revision is None:
            raise ValidationError("holdout materialization requires --candidate-revision")
        frozen_candidate = _full_revision(candidate_revision, "candidate revision")
    else:
        if open_holdout or candidate_revision is not None:
            raise ValidationError("development preparation cannot use holdout options")
        frozen_candidate = None
    if (output / "manifest.json").exists():
        raise ValidationError("prepared output is already finalized")

    ffmpeg = _resolve_executable(ffmpeg_value)
    frozen = _validate_frozen_utc(frozen_utc)
    selected = select_candidates(inventory(mtg_root, original_root))
    seal_hash = selection_sha256(selected)
    rows = selected[split]
    output.mkdir(parents=True, exist_ok=True)
    labels: list[dict[str, Any]] = []
    private_sources: list[dict[str, Any]] = []
    seen_tracks: set[str] = set()
    for index, row in enumerate(rows, start=1):
        print(f"prepare {index}/{len(rows)} {split}", flush=True)
        source = output / "working" / f"source-{index:03d}.mp3"
        rendered = output / "working" / f"canonical-{index:03d}.wav"
        _download_audio(row, source)
        _canonicalize(ffmpeg, source, rendered)
        audio_hash = sha256_file(rendered)
        track = "track-" + audio_hash[:24]
        if track in seen_tracks:
            raise ValidationError("duplicate canonical audio content in selected split")
        seen_tracks.add(track)
        final_audio = output / "audio" / f"{track}.wav"
        final_audio.parent.mkdir(parents=True, exist_ok=True)
        rendered.replace(final_audio)
        labels.append(
            {
                "track": track,
                "split": split,
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
    _write_json(output / "labels.json", labels)
    _write_json(output / "private-sources.json", private_sources)
    counts = {
        name: sum(
            row["key_tonic"] == TONICS[name.split()[0]] and row["key_mode"] == name.split()[1]
            for row in labels
        )
        for name in KEY_NAMES
    }
    expected_per_class = DEVELOPMENT_PER_CLASS if split == "development" else HOLDOUT_PER_CLASS
    if set(counts.values()) != {expected_per_class}:
        raise AssertionError("prepared key classes are not balanced")
    manifest = {
        "format": FORMAT,
        "split": split,
        "frozen_utc": frozen,
        "mtg_revision": MTG_REVISION,
        "original_exclusion_revision": ORIGINAL_REVISION,
        "selection_seed": SELECTION_SEED,
        "selection_sha256": seal_hash,
        "candidate_revision": frozen_candidate,
        "ffmpeg_version": _ffmpeg_version(ffmpeg),
        "sample_rate": SAMPLE_RATE,
        "channels": CHANNELS,
        "sample_width_bytes": SAMPLE_WIDTH,
        "track_count": len(labels),
        "class_counts": counts,
        "track_ids": [row["track"] for row in labels],
        "labels_sha256": sha256_file(output / "labels.json"),
        "private_sources_sha256": sha256_file(output / "private-sources.json"),
    }
    _write_json(output / "manifest.json", manifest)
    return manifest


def load_prepared(prepared: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    try:
        manifest = json.loads((prepared / "manifest.json").read_text(encoding="utf-8"))
        labels = json.loads((prepared / "labels.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError("cannot read prepared GiantSteps corpus") from exc
    if not isinstance(manifest, dict) or manifest.get("format") != FORMAT:
        raise ValidationError(f"manifest format must be {FORMAT}")
    if manifest.get("mtg_revision") != MTG_REVISION or manifest.get(
        "original_exclusion_revision"
    ) != ORIGINAL_REVISION:
        raise ValidationError("prepared corpus revisions do not match the protocol")
    if manifest.get("labels_sha256") != sha256_file(prepared / "labels.json"):
        raise ValidationError("prepared labels hash does not match the manifest")
    if not isinstance(labels, list) or not labels:
        raise ValidationError("prepared labels must be a non-empty array")
    ids = [row.get("track") for row in labels if isinstance(row, dict)]
    if ids != manifest.get("track_ids") or ids != sorted(ids) or len(ids) != len(set(ids)):
        raise ValidationError("prepared labels do not exactly match sorted manifest IDs")
    split = manifest.get("split")
    if split not in {"development", "holdout"}:
        raise ValidationError("prepared split must be development or holdout")
    expected_count = 24 * (
        DEVELOPMENT_PER_CLASS if split == "development" else HOLDOUT_PER_CLASS
    )
    if len(labels) != expected_count:
        raise ValidationError(f"prepared {split} split must contain {expected_count} tracks")
    if split == "holdout":
        _full_revision(str(manifest.get("candidate_revision", "")), "candidate revision")
    for row in labels:
        if row.get("split") != split:
            raise ValidationError("label split does not match manifest")
        if row.get("key_tonic") not in range(12) or row.get("key_mode") not in {
            "major",
            "minor",
        }:
            raise ValidationError("invalid prepared key label")
    return manifest, labels


def run_analysis(
    prepared: Path,
    analyzer: Path,
    output: Path,
    source_revision: str,
) -> dict[str, Any]:
    manifest, labels = load_prepared(prepared)
    revision = _full_revision(source_revision)
    if manifest["split"] == "holdout" and manifest["candidate_revision"] != revision:
        raise ValidationError("holdout source revision differs from frozen candidate")
    if not analyzer.is_file():
        raise ValidationError("analyzer executable not found")
    epoch = os.environ.get("SOURCE_DATE_EPOCH", "")
    if not epoch.isdigit():
        raise ValidationError("SOURCE_DATE_EPOCH must be a non-negative integer")
    output.mkdir(parents=True, exist_ok=True)
    analyzed = output / "analyzed"
    analyzed.mkdir(parents=True, exist_ok=True)
    mappings: list[dict[str, str]] = []
    outputs: list[dict[str, str]] = []
    for index, row in enumerate(labels, start=1):
        track = str(row["track"])
        audio = prepared / "audio" / f"{track}.wav"
        if not audio.is_file() or "track-" + sha256_file(audio)[:24] != track:
            raise ValidationError(f"prepared audio hash does not match {track}")
        target = analyzed / f"{track}.apta"
        print(f"analyze {index}/{len(labels)} {track}", flush=True)
        try:
            subprocess.run(
                [str(analyzer), str(audio), "--output", str(target), "--features", "all"],
                check=True,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            raise ValidationError(f"analysis failed for {track}") from exc
        mappings.append({"track": track, "path": str(target.resolve())})
        outputs.append({"track": track, "apta_sha256": sha256_file(target)})
    mapping_path = output / "mapping.csv"
    with mapping_path.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=("track", "path"), lineterminator="\n")
        writer.writeheader()
        writer.writerows(mappings)
    run = {
        "format": FORMAT,
        "split": manifest["split"],
        "source_revision": revision,
        "source_date_epoch": int(epoch),
        "analyzer_sha256": sha256_file(analyzer),
        "manifest_sha256": sha256_file(prepared / "manifest.json"),
        "mapping_sha256": sha256_file(mapping_path),
        "track_count": len(labels),
        "complete": True,
        "outputs": outputs,
    }
    _write_json(output / "run.json", run)
    return run


def _error_family(expected_tonic: int, expected_mode: str, tonic: int, mode: str) -> str:
    if tonic == expected_tonic and mode == expected_mode:
        return "exact"
    if tonic == expected_tonic and mode != expected_mode:
        return "parallel"
    relative = (expected_tonic + (9 if expected_mode == "major" else 3)) % 12
    if tonic == relative and mode != expected_mode:
        return "relative"
    if mode == expected_mode and tonic in {(expected_tonic + 5) % 12, (expected_tonic + 7) % 12}:
        return "fifth"
    return "other"


def score_rows(
    labels: Iterable[dict[str, Any]], results: Iterable[dict[str, Any]]
) -> dict[str, Any]:
    truth = {str(row["track"]): row for row in labels}
    observed = {str(row["track"]): row for row in results}
    if set(truth) != set(observed):
        raise ValidationError("result IDs do not exactly match prepared label IDs")
    details: list[dict[str, Any]] = []
    for track in sorted(truth):
        expected = truth[track]
        result = observed[track]
        tonic = int(result["key_tonic"])
        mode = str(result["key_mode"])
        confidence = int(result["key_confidence"])
        correct = tonic == expected["key_tonic"] and mode == expected["key_mode"]
        details.append(
            {
                "track": track,
                "expected_tonic": int(expected["key_tonic"]),
                "expected_mode": str(expected["key_mode"]),
                "key_tonic": tonic,
                "key_mode": mode,
                "key_confidence": confidence,
                "key_correct": correct,
                "error_family": _error_family(
                    int(expected["key_tonic"]), str(expected["key_mode"]), tonic, mode
                ),
            }
        )
    count = len(details)
    correct = sum(bool(row["key_correct"]) for row in details)
    high_errors = [
        row for row in details if row["key_confidence"] >= HIGH_CONFIDENCE and not row["key_correct"]
    ]
    by_class: dict[str, dict[str, int | float]] = {}
    for name in KEY_NAMES:
        tonic_name, mode = name.split()
        rows = [
            row
            for row in details
            if row["expected_tonic"] == TONICS[tonic_name] and row["expected_mode"] == mode
        ]
        class_correct = sum(bool(row["key_correct"]) for row in rows)
        by_class[name] = {
            "track_count": len(rows),
            "key_correct": class_correct,
            "key_accuracy": class_correct / len(rows) if rows else 0.0,
        }
    families = {
        name: sum(row["error_family"] == name for row in details)
        for name in ("exact", "parallel", "relative", "fifth", "other")
    }
    return {
        "overall": {
            "track_count": count,
            "key_correct": correct,
            "key_accuracy": correct / count if count else 0.0,
            "high_confidence_threshold": HIGH_CONFIDENCE,
            "high_confidence_key_errors": len(high_errors),
        },
        "error_families": families,
        "by_class": by_class,
        "tracks": details,
    }


def evaluate(
    prepared: Path,
    inspector: Path,
    mapping: Path,
    report: Path,
) -> dict[str, Any]:
    manifest, labels = load_prepared(prepared)
    try:
        import apta_1_1_export_acceptance_results as exporter
    except ImportError as exc:
        raise ValidationError("cannot import the APTA result exporter") from exc
    mapped = exporter.read_mapping(mapping)
    if sorted(track for track, _path in mapped) != [row["track"] for row in labels]:
        raise ValidationError("mapping IDs do not exactly match the prepared split")
    try:
        results = [
            exporter.parse_inspection(track, exporter.inspect_file(inspector, path))
            for track, path in mapped
        ]
    except exporter.ExportError as exc:
        raise ValidationError("cannot export analyzed key results") from exc
    value = {
        "format": REPORT_FORMAT,
        "split": manifest["split"],
        "evidence_level": "independent-development"
        if manifest["split"] == "development"
        else "one-shot-holdout",
        "acceptance_claim": False,
        **score_rows(labels, results),
    }
    _write_json(report, value)
    return value


def _load_report(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError("cannot read GiantSteps key report") from exc
    if not isinstance(value, dict) or value.get("format") != REPORT_FORMAT:
        raise ValidationError(f"report format must be {REPORT_FORMAT}")
    tracks = value.get("tracks")
    if not isinstance(tracks, list) or not tracks:
        raise ValidationError("report tracks must be an array")
    ids: list[str] = []
    correct = 0
    for row in tracks:
        if not isinstance(row, dict):
            raise ValidationError("report track row must be an object")
        track = row.get("track")
        expected_tonic = row.get("expected_tonic")
        expected_mode = row.get("expected_mode")
        tonic = row.get("key_tonic")
        mode = row.get("key_mode")
        confidence = row.get("key_confidence")
        recorded_correct = row.get("key_correct")
        if not isinstance(track, str) or not track:
            raise ValidationError("report track ID must be non-empty")
        if expected_tonic not in range(12) or tonic not in range(12):
            raise ValidationError("report tonic is outside 0..11")
        if expected_mode not in {"major", "minor"} or mode not in {"major", "minor"}:
            raise ValidationError("report mode is unsupported")
        if not isinstance(confidence, int) or not 0 <= confidence <= 100:
            raise ValidationError("report confidence is outside 0..100")
        observed_correct = tonic == expected_tonic and mode == expected_mode
        if recorded_correct is not observed_correct:
            raise ValidationError("report correctness is inconsistent with its verdict")
        ids.append(track)
        correct += observed_correct
    if ids != sorted(ids) or len(ids) != len(set(ids)):
        raise ValidationError("report track IDs must be sorted and unique")
    overall = value.get("overall")
    if not isinstance(overall, dict):
        raise ValidationError("report overall summary is missing")
    expected_accuracy = correct / len(tracks)
    if overall.get("track_count") != len(tracks) or overall.get("key_correct") != correct:
        raise ValidationError("report summary counts are inconsistent")
    try:
        recorded_accuracy = float(overall["key_accuracy"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValidationError("report accuracy is invalid") from exc
    if abs(recorded_accuracy - expected_accuracy) > 1e-15:
        raise ValidationError("report accuracy is inconsistent")
    return value


def compare_reports(
    baseline_path: Path,
    candidate_path: Path,
    baseline_revision: str,
    candidate_revision: str,
    candidate_flag: str,
    output: Path,
) -> dict[str, Any]:
    baseline = _load_report(baseline_path)
    candidate = _load_report(candidate_path)
    if baseline.get("split") != candidate.get("split"):
        raise ValidationError("baseline and candidate report splits differ")
    baseline_rows = {row["track"]: row for row in baseline["tracks"]}
    candidate_rows = {row["track"]: row for row in candidate["tracks"]}
    if set(baseline_rows) != set(candidate_rows):
        raise ValidationError("baseline and candidate report IDs differ")
    fixes: list[str] = []
    breaks: list[str] = []
    changed: list[str] = []
    baseline_high_errors: set[str] = set()
    candidate_high_errors: set[str] = set()
    for track in sorted(baseline_rows):
        base = baseline_rows[track]
        cand = candidate_rows[track]
        if (base["key_tonic"], base["key_mode"]) != (cand["key_tonic"], cand["key_mode"]):
            changed.append(track)
        if not base["key_correct"] and cand["key_correct"]:
            fixes.append(track)
        if base["key_correct"] and not cand["key_correct"]:
            breaks.append(track)
        if not base["key_correct"] and base["key_confidence"] >= HIGH_CONFIDENCE:
            baseline_high_errors.add(track)
        if not cand["key_correct"] and cand["key_confidence"] >= HIGH_CONFIDENCE:
            candidate_high_errors.add(track)
    baseline_accuracy = float(baseline["overall"]["key_accuracy"])
    candidate_accuracy = float(candidate["overall"]["key_accuracy"])
    new_high_errors = sorted(candidate_high_errors - baseline_high_errors)
    gates = {
        "development_accuracy_at_least_70_percent": candidate_accuracy
        >= DEVELOPMENT_ACCURACY_GATE,
        "accuracy_improved": candidate_accuracy > baseline_accuracy,
        "fixes_exceed_breaks": len(fixes) > len(breaks),
        "no_new_high_confidence_errors": not new_high_errors,
    }
    split = str(baseline["split"])
    normalized_flag = candidate_flag.strip()
    if not normalized_flag:
        raise ValidationError("candidate flag must not be empty")
    value = {
        "format": COMPARISON_FORMAT,
        "split": split,
        "acceptance_claim": False,
        "baseline_revision": _full_revision(baseline_revision, "baseline revision"),
        "candidate_revision": _full_revision(candidate_revision, "candidate revision"),
        "candidate_flags": [normalized_flag],
        "track_count": len(baseline_rows),
        "baseline_key_accuracy": baseline_accuracy,
        "candidate_key_accuracy": candidate_accuracy,
        "fix_count": len(fixes),
        "break_count": len(breaks),
        "changed_verdict_count": len(changed),
        "new_high_confidence_error_count": len(new_high_errors),
        "fixes": fixes,
        "breaks": breaks,
        "changed_verdicts": changed,
        "new_high_confidence_errors": new_high_errors,
        "gates": gates,
        "holdout_eligible": split == "development" and all(gates.values()),
    }
    _write_json(output, value)
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--mtg-root", type=Path, required=True)
    prepare_parser.add_argument("--original-root", type=Path, required=True)
    prepare_parser.add_argument("--output", type=Path, required=True)
    prepare_parser.add_argument("--split", choices=("development", "holdout"), required=True)
    prepare_parser.add_argument("--ffmpeg", default="ffmpeg")
    prepare_parser.add_argument("--frozen-utc", required=True)
    prepare_parser.add_argument("--open-holdout", action="store_true")
    prepare_parser.add_argument("--candidate-revision")
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
    compare_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            value = prepare(
                args.mtg_root,
                args.original_root,
                args.output,
                args.split,
                args.ffmpeg,
                args.frozen_utc,
                args.open_holdout,
                args.candidate_revision,
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
                args.output,
            )
    except (OSError, ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(value, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
