#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Evaluate the frozen APTA 1.1 DJ-analysis acceptance corpus.

The evaluator deliberately consumes opaque track IDs plus manually verified
labels.  It does not read audio, titles, artists, paths or library metadata.
The acceptance thresholds are constants in this file so they cannot be moved
after observing the fresh validation corpus.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path
from typing import Iterable

FORMAT = "apta-1.1-dj-validation-1"
REPORT_FORMAT = "apta-1.1-dj-acceptance-report-1"
MIN_ACCEPTANCE_TRACKS = 48
HIGH_CONFIDENCE = 75
KEY_MIN_ACCURACY = 0.75
METER_MIN_ACCURACY = 0.95
DOWNBEAT_MIN_ACCURACY = 0.90
GRID_MIN_ACCURACY = 0.90
HIGH_CONFIDENCE_MAX_ERROR_RATE = 0.05
BEAT_PERIOD_TOLERANCE = 0.01
DOWNBEAT_PHASE_TOLERANCE_BEATS = 0.10


class EvaluationError(ValueError):
    pass


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _load_manifest(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise EvaluationError(f"cannot read validation manifest: {exc}") from exc
    if not isinstance(value, dict):
        raise EvaluationError("validation manifest must be a JSON object")
    allowed = {
        "format",
        "track_count",
        "track_ids",
        "labels_sha256",
        "frozen_utc",
        "reference_source",
        "verification_procedure",
    }
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise EvaluationError(f"validation manifest has unsupported fields: {unknown}")
    if value.get("format") != FORMAT:
        raise EvaluationError(f"validation manifest format must be {FORMAT}")
    count = value.get("track_count")
    ids = value.get("track_ids")
    if not isinstance(count, int) or count <= 0:
        raise EvaluationError("track_count must be a positive integer")
    if not isinstance(ids, list) or any(not isinstance(item, str) or not item for item in ids):
        raise EvaluationError("track_ids must be a non-empty array of opaque IDs")
    if len(ids) != count or len(set(ids)) != count or ids != sorted(ids):
        raise EvaluationError("track_ids must be unique, sorted and match track_count")
    labels_hash = value.get("labels_sha256")
    if not isinstance(labels_hash, str) or len(labels_hash) != 64 or any(
        ch not in "0123456789abcdef" for ch in labels_hash
    ):
        raise EvaluationError("labels_sha256 must be lowercase SHA-256 hex")
    for field in ("frozen_utc", "reference_source", "verification_procedure"):
        if not isinstance(value.get(field), str) or not str(value[field]).strip():
            raise EvaluationError(f"{field} must be a non-empty string")
    return value


def _parse_mode(value: str) -> str:
    mode = value.strip().casefold()
    if mode not in {"major", "minor"}:
        raise EvaluationError(f"key mode must be major or minor, got {value!r}")
    return mode


def _parse_tonic(value: str) -> int:
    try:
        tonic = int(value)
    except ValueError as exc:
        raise EvaluationError(f"invalid key tonic: {value!r}") from exc
    if not 0 <= tonic <= 11:
        raise EvaluationError(f"key tonic must be 0..11, got {tonic}")
    return tonic


def _parse_meter(numerator: str, denominator: str) -> tuple[int, int]:
    try:
        num = int(numerator)
        den = int(denominator)
    except ValueError as exc:
        raise EvaluationError("invalid meter") from exc
    if num not in {3, 4} or den != 4:
        raise EvaluationError("APTA 1.1 native meter acceptance permits only 3/4 or 4/4")
    return num, den


def _read_labels(path: Path) -> list[dict[str, object]]:
    required = {
        "track",
        "key_tonic",
        "key_mode",
        "meter_numerator",
        "meter_denominator",
        "downbeat_frame",
        "beat_period_frames",
    }
    rows: list[dict[str, object]] = []
    try:
        with path.open("r", encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source)
            missing = required - set(reader.fieldnames or [])
            if missing:
                raise EvaluationError(f"labels are missing columns: {sorted(missing)}")
            for line, raw in enumerate(reader, start=2):
                track = (raw.get("track") or "").strip()
                if not track:
                    raise EvaluationError(f"labels:{line}: empty track ID")
                num, den = _parse_meter(raw["meter_numerator"], raw["meter_denominator"])
                try:
                    downbeat = int(raw["downbeat_frame"])
                    period = float(raw["beat_period_frames"])
                except ValueError as exc:
                    raise EvaluationError(f"labels:{line}: invalid frame field") from exc
                if downbeat < 0 or period <= 0.0:
                    raise EvaluationError(f"labels:{line}: frame values must be non-negative/positive")
                rows.append({
                    "track": track,
                    "key_tonic": _parse_tonic(raw["key_tonic"]),
                    "key_mode": _parse_mode(raw["key_mode"]),
                    "meter_numerator": num,
                    "meter_denominator": den,
                    "downbeat_frame": downbeat,
                    "beat_period_frames": period,
                })
    except OSError as exc:
        raise EvaluationError(f"cannot read labels: {exc}") from exc
    ids = [str(row["track"]) for row in rows]
    if len(ids) != len(set(ids)):
        raise EvaluationError("labels contain duplicate track IDs")
    return rows


def _read_results(path: Path) -> list[dict[str, object]]:
    required = {
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
    }
    rows: list[dict[str, object]] = []
    try:
        with path.open("r", encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source)
            missing = required - set(reader.fieldnames or [])
            if missing:
                raise EvaluationError(f"results are missing columns: {sorted(missing)}")
            for line, raw in enumerate(reader, start=2):
                track = (raw.get("track") or "").strip()
                if not track:
                    raise EvaluationError(f"results:{line}: empty track ID")
                num, den = _parse_meter(raw["meter_numerator"], raw["meter_denominator"])
                try:
                    key_conf = int(raw["key_confidence"])
                    meter_conf = int(raw["meter_confidence"])
                    downbeat_conf = int(raw["downbeat_confidence"])
                    grid_conf = int(raw["grid_confidence"])
                    downbeat = int(raw["downbeat_frame"])
                    period = float(raw["beat_period_frames"])
                except ValueError as exc:
                    raise EvaluationError(f"results:{line}: invalid numeric field") from exc
                for name, confidence in (
                    ("key", key_conf),
                    ("meter", meter_conf),
                    ("downbeat", downbeat_conf),
                    ("grid", grid_conf),
                ):
                    if not 0 <= confidence <= 100:
                        raise EvaluationError(f"results:{line}: {name} confidence must be 0..100")
                if downbeat < 0 or period <= 0.0:
                    raise EvaluationError(f"results:{line}: frame values must be non-negative/positive")
                rows.append({
                    "track": track,
                    "key_tonic": _parse_tonic(raw["key_tonic"]),
                    "key_mode": _parse_mode(raw["key_mode"]),
                    "key_confidence": key_conf,
                    "meter_numerator": num,
                    "meter_denominator": den,
                    "meter_confidence": meter_conf,
                    "downbeat_frame": downbeat,
                    "downbeat_confidence": downbeat_conf,
                    "beat_period_frames": period,
                    "grid_confidence": grid_conf,
                })
    except OSError as exc:
        raise EvaluationError(f"cannot read results: {exc}") from exc
    ids = [str(row["track"]) for row in rows]
    if len(ids) != len(set(ids)):
        raise EvaluationError("results contain duplicate track IDs")
    return rows


def _period_correct(truth: dict[str, object], result: dict[str, object]) -> bool:
    expected = float(truth["beat_period_frames"])
    reported = float(result["beat_period_frames"])
    return abs(reported - expected) <= expected * BEAT_PERIOD_TOLERANCE


def _downbeat_correct(truth: dict[str, object], result: dict[str, object]) -> bool:
    period = float(truth["beat_period_frames"])
    bar = period * int(truth["meter_numerator"])
    if bar <= 0.0:
        return False
    delta = abs(float(result["downbeat_frame"]) - float(truth["downbeat_frame"])) % bar
    cyclic = min(delta, bar - delta)
    return cyclic <= period * DOWNBEAT_PHASE_TOLERANCE_BEATS


def _key_correct(truth: dict[str, object], result: dict[str, object]) -> bool:
    return (
        int(truth["key_tonic"]) == int(result["key_tonic"])
        and str(truth["key_mode"]) == str(result["key_mode"])
    )


def _meter_correct(truth: dict[str, object], result: dict[str, object]) -> bool:
    return (
        int(truth["meter_numerator"]) == int(result["meter_numerator"])
        and int(truth["meter_denominator"]) == int(result["meter_denominator"])
    )


def _ratio(value: int, total: int) -> float:
    return value / total if total else 0.0


def evaluate(
    manifest: dict[str, object],
    labels: Iterable[dict[str, object]],
    results: Iterable[dict[str, object]],
) -> dict[str, object]:
    expected_ids = list(manifest["track_ids"])
    labels_by_id = {str(row["track"]): row for row in labels}
    results_by_id = {str(row["track"]): row for row in results}
    if sorted(labels_by_id) != expected_ids:
        raise EvaluationError("label track IDs do not exactly match the frozen manifest")
    if sorted(results_by_id) != expected_ids:
        raise EvaluationError("result track IDs do not exactly match the frozen manifest")

    key_ok = meter_ok = downbeat_ok = grid_ok = 0
    key_high_error = meter_high_error = downbeat_high_error = grid_high_error = 0
    for track in expected_ids:
        truth = labels_by_id[track]
        result = results_by_id[track]
        key_good = _key_correct(truth, result)
        meter_good = _meter_correct(truth, result)
        downbeat_good = _downbeat_correct(truth, result)
        grid_good = _period_correct(truth, result) and downbeat_good
        key_ok += key_good
        meter_ok += meter_good
        downbeat_ok += downbeat_good
        grid_ok += grid_good
        key_high_error += (not key_good) and int(result["key_confidence"]) >= HIGH_CONFIDENCE
        meter_high_error += (not meter_good) and int(result["meter_confidence"]) >= HIGH_CONFIDENCE
        downbeat_high_error += (not downbeat_good) and int(result["downbeat_confidence"]) >= HIGH_CONFIDENCE
        grid_high_error += (not grid_good) and int(result["grid_confidence"]) >= HIGH_CONFIDENCE

    total = len(expected_ids)
    metrics = {
        "tracks": total,
        "key_exact": key_ok,
        "key_accuracy": _ratio(key_ok, total),
        "meter_exact": meter_ok,
        "meter_accuracy": _ratio(meter_ok, total),
        "downbeat_phase_correct": downbeat_ok,
        "downbeat_accuracy": _ratio(downbeat_ok, total),
        "beatgrid_correct": grid_ok,
        "beatgrid_accuracy": _ratio(grid_ok, total),
        "key_high_confidence_errors": key_high_error,
        "key_high_confidence_error_rate": _ratio(key_high_error, total),
        "meter_high_confidence_errors": meter_high_error,
        "meter_high_confidence_error_rate": _ratio(meter_high_error, total),
        "downbeat_high_confidence_errors": downbeat_high_error,
        "downbeat_high_confidence_error_rate": _ratio(downbeat_high_error, total),
        "grid_high_confidence_errors": grid_high_error,
        "grid_high_confidence_error_rate": _ratio(grid_high_error, total),
    }
    enough = total >= MIN_ACCEPTANCE_TRACKS
    conditions = {
        "minimum_track_count": enough,
        "key_accuracy": metrics["key_accuracy"] >= KEY_MIN_ACCURACY,
        "meter_accuracy": metrics["meter_accuracy"] >= METER_MIN_ACCURACY,
        "downbeat_accuracy": metrics["downbeat_accuracy"] >= DOWNBEAT_MIN_ACCURACY,
        "beatgrid_accuracy": metrics["beatgrid_accuracy"] >= GRID_MIN_ACCURACY,
        "key_high_confidence_safety": metrics["key_high_confidence_error_rate"] <= HIGH_CONFIDENCE_MAX_ERROR_RATE,
        "meter_high_confidence_safety": metrics["meter_high_confidence_error_rate"] <= HIGH_CONFIDENCE_MAX_ERROR_RATE,
        "downbeat_high_confidence_safety": metrics["downbeat_high_confidence_error_rate"] <= HIGH_CONFIDENCE_MAX_ERROR_RATE,
        "grid_high_confidence_safety": metrics["grid_high_confidence_error_rate"] <= HIGH_CONFIDENCE_MAX_ERROR_RATE,
    }
    return {
        "format": REPORT_FORMAT,
        "evidence_level": "acceptance" if enough else "diagnostic-only",
        "thresholds": {
            "minimum_tracks": MIN_ACCEPTANCE_TRACKS,
            "key_min_accuracy": KEY_MIN_ACCURACY,
            "meter_min_accuracy": METER_MIN_ACCURACY,
            "downbeat_min_accuracy": DOWNBEAT_MIN_ACCURACY,
            "beatgrid_min_accuracy": GRID_MIN_ACCURACY,
            "high_confidence": HIGH_CONFIDENCE,
            "high_confidence_max_error_rate": HIGH_CONFIDENCE_MAX_ERROR_RATE,
            "beat_period_tolerance": BEAT_PERIOD_TOLERANCE,
            "downbeat_phase_tolerance_beats": DOWNBEAT_PHASE_TOLERANCE_BEATS,
        },
        "metrics": metrics,
        "conditions": conditions,
        "accepted": all(conditions.values()),
    }


def _self_test() -> int:
    ids = [f"track-{index:03d}" for index in range(MIN_ACCEPTANCE_TRACKS)]
    manifest: dict[str, object] = {
        "format": FORMAT,
        "track_count": len(ids),
        "track_ids": ids,
        "labels_sha256": "0" * 64,
        "frozen_utc": "self-test",
        "reference_source": "synthetic",
        "verification_procedure": "self-test",
    }
    labels = []
    results = []
    for index, track in enumerate(ids):
        truth = {
            "track": track,
            "key_tonic": index % 12,
            "key_mode": "major" if index % 2 == 0 else "minor",
            "meter_numerator": 4,
            "meter_denominator": 4,
            "downbeat_frame": index * 100000,
            "beat_period_frames": 24000.0,
        }
        labels.append(truth)
        results.append({
            **truth,
            "key_confidence": 80,
            "meter_confidence": 80,
            "downbeat_confidence": 80,
            "grid_confidence": 80,
        })
    report = evaluate(manifest, labels, results)
    if not report["accepted"]:
        raise EvaluationError("perfect synthetic acceptance set was rejected")

    broken = [dict(row) for row in results]
    for index in range(13):
        broken[index]["key_tonic"] = (int(broken[index]["key_tonic"]) + 1) % 12
    if evaluate(manifest, labels, broken)["accepted"]:
        raise EvaluationError("below-threshold key set was accepted")

    diagnostic_manifest = dict(manifest)
    diagnostic_manifest["track_count"] = 12
    diagnostic_manifest["track_ids"] = ids[:12]
    diagnostic = evaluate(diagnostic_manifest, labels[:12], results[:12])
    if diagnostic["accepted"] or diagnostic["evidence_level"] != "diagnostic-only":
        raise EvaluationError("undersized corpus was treated as acceptance evidence")
    print("APTA 1.1 DJ acceptance evaluator self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--labels", type=Path)
    parser.add_argument("--results", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.self_test:
        return _self_test()
    if None in (args.manifest, args.labels, args.results, args.output):
        parser.error("--manifest, --labels, --results and --output are required")

    try:
        manifest = _load_manifest(args.manifest)
        if _sha256(args.labels) != manifest["labels_sha256"]:
            raise EvaluationError("labels SHA-256 does not match the frozen manifest")
        labels = _read_labels(args.labels)
        results = _read_results(args.results)
        report = evaluate(manifest, labels, results)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(json.dumps(report, sort_keys=True))
        return 0 if report["accepted"] else 2
    except EvaluationError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
