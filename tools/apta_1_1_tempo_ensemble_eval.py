#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Evaluate the frozen APTA 1.1 Task-5 tempo/grid ensemble candidate.

This tool intentionally operates on opaque track IDs and aggregate labels only.
It does not read audio, titles, artists or source paths.  It implements the
pre-registered acceptance boundary from
``docs/status/APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md``.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Iterable

FORMAT = "apta-1.1-tempo-ensemble-validation-1"
REPORT_FORMAT = "apta-1.1-tempo-ensemble-report-1"
MIN_ACCEPTANCE_TRACKS = 48
HIGH_CONFIDENCE = 75
EXACT_TOLERANCE = 0.01
METRICAL_RATIOS = (0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.5, 2.0, 3.0, 4.0)
HEX64 = re.compile(r"^[0-9a-f]{64}$")


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
        "format", "track_count", "track_ids", "manifest_sha256", "labels_sha256",
        "frozen_utc", "reference_source", "verification_procedure", "tempo_bins",
    }
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise EvaluationError(f"validation manifest has unsupported fields: {unknown}")
    if value.get("format") != FORMAT:
        raise EvaluationError(f"validation manifest format must be {FORMAT}")
    track_count = value.get("track_count")
    if not isinstance(track_count, int) or track_count <= 0:
        raise EvaluationError("track_count must be a positive integer")
    track_ids = value.get("track_ids")
    if not isinstance(track_ids, list) or any(not isinstance(item, str) or not item for item in track_ids):
        raise EvaluationError("track_ids must be a non-empty array of opaque IDs")
    if len(track_ids) != track_count or len(set(track_ids)) != track_count:
        raise EvaluationError("track_ids must be unique and match track_count")
    if track_ids != sorted(track_ids):
        raise EvaluationError("track_ids must be sorted for deterministic evidence")
    for field in ("manifest_sha256", "labels_sha256"):
        value_hash = value.get(field)
        if not isinstance(value_hash, str) or HEX64.fullmatch(value_hash) is None:
            raise EvaluationError(f"{field} must be a lowercase SHA-256 hex digest")
    for field in ("frozen_utc", "reference_source", "verification_procedure"):
        if not isinstance(value.get(field), str) or not str(value[field]).strip():
            raise EvaluationError(f"{field} must be a non-empty string")
    bins = value.get("tempo_bins")
    if not isinstance(bins, dict) or not bins:
        raise EvaluationError("tempo_bins must be a non-empty object")
    if any(not isinstance(count, int) or count < 0 for count in bins.values()):
        raise EvaluationError("tempo_bins counts must be non-negative integers")
    if sum(bins.values()) != track_count:
        raise EvaluationError("tempo_bins counts must sum to track_count")
    return value


def _parse_bool(value: str) -> bool:
    normalized = value.strip().casefold()
    if normalized in {"1", "true", "yes"}:
        return True
    if normalized in {"0", "false", "no", ""}:
        return False
    raise EvaluationError(f"invalid boolean value: {value!r}")


def _read_results(path: Path) -> list[dict[str, object]]:
    required = {"track", "truth_millibpm", "reported_millibpm", "confidence"}
    rows: list[dict[str, object]] = []
    try:
        with path.open("r", encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source)
            if reader.fieldnames is None or not required.issubset(reader.fieldnames):
                missing = sorted(required - set(reader.fieldnames or []))
                raise EvaluationError(f"{path} is missing required columns: {missing}")
            for line_number, raw in enumerate(reader, start=2):
                track = (raw.get("track") or "").strip()
                if not track:
                    raise EvaluationError(f"{path}:{line_number}: empty track ID")
                try:
                    truth = int(raw["truth_millibpm"])
                    reported = int(raw["reported_millibpm"])
                    confidence = int(raw["confidence"])
                except (TypeError, ValueError) as exc:
                    raise EvaluationError(f"{path}:{line_number}: invalid numeric field") from exc
                if truth <= 0 or reported <= 0:
                    raise EvaluationError(f"{path}:{line_number}: tempo must be positive")
                if not 0 <= confidence <= 100:
                    raise EvaluationError(f"{path}:{line_number}: confidence must be 0..100")
                actionable = _parse_bool(raw.get("actionable", "0"))
                rows.append({
                    "track": track,
                    "truth": truth,
                    "reported": reported,
                    "confidence": confidence,
                    "actionable": actionable,
                })
    except OSError as exc:
        raise EvaluationError(f"cannot read results {path}: {exc}") from exc
    ids = [str(row["track"]) for row in rows]
    if len(ids) != len(set(ids)):
        raise EvaluationError(f"{path} contains duplicate track IDs")
    return rows


def _within_ratio(reported: int, truth: int, ratio: float) -> bool:
    target = truth * ratio
    return abs(reported - target) <= target * EXACT_TOLERANCE


def _is_exact(row: dict[str, object]) -> bool:
    return _within_ratio(int(row["reported"]), int(row["truth"]), 1.0)


def _is_metrical_error(row: dict[str, object]) -> bool:
    if _is_exact(row):
        return False
    reported = int(row["reported"])
    truth = int(row["truth"])
    return any(_within_ratio(reported, truth, ratio) for ratio in METRICAL_RATIOS)


def _metrics(rows: Iterable[dict[str, object]]) -> dict[str, int]:
    materialized = list(rows)
    exact = sum(_is_exact(row) for row in materialized)
    high_errors = sum(
        int(row["confidence"]) >= HIGH_CONFIDENCE and not _is_exact(row)
        for row in materialized
    )
    high_metrical = sum(
        int(row["confidence"]) >= HIGH_CONFIDENCE and _is_metrical_error(row)
        for row in materialized
    )
    actionable = sum(bool(row["actionable"]) for row in materialized)
    return {
        "tracks": len(materialized),
        "exact_within_1_percent": exact,
        "errors": len(materialized) - exact,
        "high_confidence_errors": high_errors,
        "high_confidence_metrical_errors": high_metrical,
        "actionable": actionable,
    }


def evaluate(manifest: dict[str, object], baseline: list[dict[str, object]], candidate: list[dict[str, object]]) -> dict[str, object]:
    expected_ids = list(manifest["track_ids"])
    baseline_by_id = {str(row["track"]): row for row in baseline}
    candidate_by_id = {str(row["track"]): row for row in candidate}
    if sorted(baseline_by_id) != expected_ids:
        raise EvaluationError("baseline track IDs do not exactly match the frozen manifest")
    if sorted(candidate_by_id) != expected_ids:
        raise EvaluationError("candidate track IDs do not exactly match the frozen manifest")
    for track in expected_ids:
        if baseline_by_id[track]["truth"] != candidate_by_id[track]["truth"]:
            raise EvaluationError(f"reference truth differs between baseline and candidate for {track}")

    baseline_metrics = _metrics(baseline_by_id[track] for track in expected_ids)
    candidate_metrics = _metrics(candidate_by_id[track] for track in expected_ids)
    fixed_ids = [track for track in expected_ids if not _is_exact(baseline_by_id[track]) and _is_exact(candidate_by_id[track])]
    broken_ids = [track for track in expected_ids if _is_exact(baseline_by_id[track]) and not _is_exact(candidate_by_id[track])]

    conditions = {
        "no_exact_accuracy_regression": candidate_metrics["exact_within_1_percent"] >= baseline_metrics["exact_within_1_percent"],
        "no_promotion_regression": len(broken_ids) == 0,
        "no_high_confidence_safety_regression": candidate_metrics["high_confidence_errors"] <= baseline_metrics["high_confidence_errors"],
        "no_metrical_safety_regression": candidate_metrics["high_confidence_metrical_errors"] <= baseline_metrics["high_confidence_metrical_errors"],
    }
    benefits = {
        "exact_accuracy_improved": candidate_metrics["exact_within_1_percent"] > baseline_metrics["exact_within_1_percent"],
        "fixed_without_breaking": bool(fixed_ids) and not broken_ids,
        "high_confidence_errors_reduced": candidate_metrics["high_confidence_errors"] < baseline_metrics["high_confidence_errors"],
        "high_confidence_metrical_errors_reduced": candidate_metrics["high_confidence_metrical_errors"] < baseline_metrics["high_confidence_metrical_errors"],
    }
    enough_tracks = int(manifest["track_count"]) >= MIN_ACCEPTANCE_TRACKS
    accepted = enough_tracks and all(conditions.values()) and any(benefits.values())
    return {
        "format": REPORT_FORMAT,
        "track_count": int(manifest["track_count"]),
        "evidence_level": "acceptance" if enough_tracks else "diagnostic-only",
        "minimum_acceptance_tracks": MIN_ACCEPTANCE_TRACKS,
        "baseline": baseline_metrics,
        "candidate": candidate_metrics,
        "transitions": {
            "fixed_count": len(fixed_ids),
            "broken_count": len(broken_ids),
            "fixed_track_ids": fixed_ids,
            "broken_track_ids": broken_ids,
        },
        "conditions": conditions,
        "benefits": benefits,
        "accepted": accepted,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        manifest = _load_manifest(args.manifest)
        baseline = _read_results(args.baseline)
        candidate = _read_results(args.candidate)
        report = evaluate(manifest, baseline, candidate)
        report["inputs"] = {
            "manifest_sha256": _sha256(args.manifest),
            "baseline_sha256": _sha256(args.baseline),
            "candidate_sha256": _sha256(args.candidate),
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except EvaluationError as exc:
        print(f"evaluation error: {exc}", file=sys.stderr)
        return 1
    if report["accepted"]:
        print("Task-5 fresh-corpus acceptance: PASS")
        return 0
    if report["evidence_level"] == "diagnostic-only":
        print("Task-5 fresh-corpus acceptance: DIAGNOSTIC ONLY (<48 tracks)")
    else:
        print("Task-5 fresh-corpus acceptance: FAIL")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
