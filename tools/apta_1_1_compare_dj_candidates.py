#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare two APTA 1.1 DJ result sets against one frozen label set.

The report is development evidence, not an acceptance verdict. Inputs contain
opaque track IDs only. Missing result rows are reported as execution failures
and count as incorrect instead of silently shrinking the evaluated corpus.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Callable, Iterable

import apta_1_1_dj_acceptance_eval as acceptance


REPORT_FORMAT = "apta-1.1-dj-candidate-comparison-1"
EVIDENCE_LEVELS = ("diagnostic-only", "development", "holdout")
CORPUS_STATUSES = ("spent", "open-development", "holdout")


class ComparisonError(ValueError):
    pass


def _validate_revision(value: str, name: str) -> str:
    normalized = value.strip().casefold()
    if len(normalized) != 40 or any(ch not in "0123456789abcdef" for ch in normalized):
        raise ComparisonError(f"{name} revision must be full lowercase 40-hex")
    return normalized


def _index_rows(
    name: str,
    rows: Iterable[dict[str, object]],
    expected_ids: set[str],
) -> dict[str, dict[str, object]]:
    indexed: dict[str, dict[str, object]] = {}
    for row in rows:
        track = str(row["track"])
        if track in indexed:
            raise ComparisonError(f"{name} contains duplicate track ID {track!r}")
        if track not in expected_ids:
            raise ComparisonError(f"{name} contains track outside the manifest: {track!r}")
        indexed[track] = row
    return indexed


def _grid_correct(truth: dict[str, object], result: dict[str, object]) -> bool:
    return acceptance._period_correct(truth, result) and acceptance._downbeat_correct(
        truth, result
    )


def _key_signature(row: dict[str, object]) -> tuple[object, ...]:
    return (int(row["key_tonic"]), str(row["key_mode"]))


def _meter_signature(row: dict[str, object]) -> tuple[object, ...]:
    return (int(row["meter_numerator"]), int(row["meter_denominator"]))


def _downbeat_signature(row: dict[str, object]) -> tuple[object, ...]:
    return (int(row["downbeat_frame"]),)


def _grid_signature(row: dict[str, object]) -> tuple[object, ...]:
    return (float(row["beat_period_frames"]), int(row["downbeat_frame"]))


FamilyCorrect = Callable[[dict[str, object], dict[str, object]], bool]
FamilySignature = Callable[[dict[str, object]], tuple[object, ...]]


FAMILIES: dict[str, tuple[FamilyCorrect, FamilySignature, str]] = {
    "key": (acceptance._key_correct, _key_signature, "key_confidence"),
    "meter": (acceptance._meter_correct, _meter_signature, "meter_confidence"),
    "downbeat": (
        acceptance._downbeat_correct,
        _downbeat_signature,
        "downbeat_confidence",
    ),
    "beatgrid": (_grid_correct, _grid_signature, "grid_confidence"),
}


def _numeric_metrics(value: object, name: str) -> dict[str, float | int]:
    if not isinstance(value, dict):
        raise ComparisonError(f"{name} resource metrics must be a JSON object")
    metrics: dict[str, float | int] = {}
    for key, raw in value.items():
        if not isinstance(key, str) or not key:
            raise ComparisonError(f"{name} resource metric names must be non-empty strings")
        if isinstance(raw, bool) or not isinstance(raw, (int, float)):
            raise ComparisonError(f"{name} resource metric {key!r} must be numeric")
        if not math.isfinite(float(raw)):
            raise ComparisonError(f"{name} resource metric {key!r} must be finite")
        metrics[key] = raw
    return metrics


def _resource_comparison(
    baseline: dict[str, float | int] | None,
    candidate: dict[str, float | int] | None,
) -> dict[str, object]:
    if baseline is None and candidate is None:
        return {"available": False, "baseline": None, "candidate": None, "delta": None}
    if baseline is None or candidate is None:
        raise ComparisonError("baseline and candidate resource metrics must be supplied together")
    if set(baseline) != set(candidate):
        raise ComparisonError("baseline and candidate resource metric keys must match exactly")
    return {
        "available": True,
        "baseline": baseline,
        "candidate": candidate,
        "delta": {key: candidate[key] - baseline[key] for key in sorted(baseline)},
    }


def compare(
    manifest: dict[str, object],
    labels: Iterable[dict[str, object]],
    baseline_results: Iterable[dict[str, object]],
    candidate_results: Iterable[dict[str, object]],
    *,
    baseline_name: str,
    candidate_name: str,
    baseline_revision: str,
    candidate_revision: str,
    baseline_flags: Iterable[str] = (),
    candidate_flags: Iterable[str] = (),
    evidence_level: str = "development",
    corpus_status: str = "spent",
    baseline_resources: dict[str, float | int] | None = None,
    candidate_resources: dict[str, float | int] | None = None,
) -> dict[str, object]:
    if evidence_level not in EVIDENCE_LEVELS:
        raise ComparisonError(f"unsupported evidence level: {evidence_level}")
    if corpus_status not in CORPUS_STATUSES:
        raise ComparisonError(f"unsupported corpus status: {corpus_status}")
    if not baseline_name.strip() or not candidate_name.strip():
        raise ComparisonError("candidate names must be non-empty")

    ordered_ids = [str(track) for track in manifest["track_ids"]]
    expected_ids = set(ordered_ids)
    labels_by_id = _index_rows("labels", labels, expected_ids)
    if set(labels_by_id) != expected_ids:
        raise ComparisonError("labels do not exactly cover the frozen manifest")
    baseline_by_id = _index_rows("baseline results", baseline_results, expected_ids)
    candidate_by_id = _index_rows("candidate results", candidate_results, expected_ids)

    family_reports: dict[str, object] = {}
    for family, (correct, signature, confidence_field) in FAMILIES.items():
        baseline_correct = 0
        candidate_correct = 0
        fixes: list[str] = []
        breaks: list[str] = []
        unchanged_correct: list[str] = []
        unchanged_wrong: list[str] = []
        changed_verdicts: list[str] = []
        baseline_high_errors: list[str] = []
        candidate_high_errors: list[str] = []

        for track in ordered_ids:
            truth = labels_by_id[track]
            baseline = baseline_by_id.get(track)
            candidate = candidate_by_id.get(track)
            baseline_good = baseline is not None and correct(truth, baseline)
            candidate_good = candidate is not None and correct(truth, candidate)
            baseline_correct += int(baseline_good)
            candidate_correct += int(candidate_good)

            if not baseline_good and candidate_good:
                fixes.append(track)
            elif baseline_good and not candidate_good:
                breaks.append(track)
            elif baseline_good:
                unchanged_correct.append(track)
            else:
                unchanged_wrong.append(track)

            if (
                baseline is not None
                and candidate is not None
                and signature(baseline) != signature(candidate)
            ):
                changed_verdicts.append(track)
            if (
                baseline is not None
                and not baseline_good
                and int(baseline[confidence_field]) >= acceptance.HIGH_CONFIDENCE
            ):
                baseline_high_errors.append(track)
            if (
                candidate is not None
                and not candidate_good
                and int(candidate[confidence_field]) >= acceptance.HIGH_CONFIDENCE
            ):
                candidate_high_errors.append(track)

        total = len(ordered_ids)
        family_reports[family] = {
            "baseline": {
                "correct": baseline_correct,
                "accuracy": acceptance._ratio(baseline_correct, total),
                "high_confidence_errors": len(baseline_high_errors),
                "high_confidence_error_rate": acceptance._ratio(
                    len(baseline_high_errors), total
                ),
                "high_confidence_error_track_ids": baseline_high_errors,
            },
            "candidate": {
                "correct": candidate_correct,
                "accuracy": acceptance._ratio(candidate_correct, total),
                "high_confidence_errors": len(candidate_high_errors),
                "high_confidence_error_rate": acceptance._ratio(
                    len(candidate_high_errors), total
                ),
                "high_confidence_error_track_ids": candidate_high_errors,
            },
            "transitions": {
                "fixes": len(fixes),
                "breaks": len(breaks),
                "net_fixes": len(fixes) - len(breaks),
                "changed_verdicts": len(changed_verdicts),
                "unchanged_correct": len(unchanged_correct),
                "unchanged_wrong": len(unchanged_wrong),
                "fix_track_ids": fixes,
                "break_track_ids": breaks,
                "changed_verdict_track_ids": changed_verdicts,
            },
        }

    baseline_missing = [track for track in ordered_ids if track not in baseline_by_id]
    candidate_missing = [track for track in ordered_ids if track not in candidate_by_id]
    return {
        "format": REPORT_FORMAT,
        "evidence_level": evidence_level,
        "corpus_status": corpus_status,
        "track_count": len(ordered_ids),
        "baseline": {
            "name": baseline_name.strip(),
            "revision": _validate_revision(baseline_revision, "baseline"),
            "flags": sorted(set(baseline_flags)),
            "result_count": len(baseline_by_id),
        },
        "candidate": {
            "name": candidate_name.strip(),
            "revision": _validate_revision(candidate_revision, "candidate"),
            "flags": sorted(set(candidate_flags)),
            "result_count": len(candidate_by_id),
        },
        "execution_failures": {
            "baseline": len(baseline_missing),
            "candidate": len(candidate_missing),
            "baseline_track_ids": baseline_missing,
            "candidate_track_ids": candidate_missing,
        },
        "families": family_reports,
        "resources": _resource_comparison(baseline_resources, candidate_resources),
        "acceptance_claim": False,
    }


def _read_resources(path: Path, name: str) -> dict[str, float | int]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ComparisonError(f"cannot read {name} resource metrics: {exc}") from exc
    return _numeric_metrics(value, name)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--baseline-results", type=Path, required=True)
    parser.add_argument("--candidate-results", type=Path, required=True)
    parser.add_argument("--baseline-name", required=True)
    parser.add_argument("--candidate-name", required=True)
    parser.add_argument("--baseline-revision", required=True)
    parser.add_argument("--candidate-revision", required=True)
    parser.add_argument("--baseline-flag", action="append", default=[])
    parser.add_argument("--candidate-flag", action="append", default=[])
    parser.add_argument("--evidence-level", choices=EVIDENCE_LEVELS, default="development")
    parser.add_argument("--corpus-status", choices=CORPUS_STATUSES, required=True)
    parser.add_argument("--baseline-resources", type=Path)
    parser.add_argument("--candidate-resources", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    try:
        manifest = acceptance._load_manifest(args.manifest)
        if acceptance._sha256(args.labels) != manifest["labels_sha256"]:
            raise ComparisonError("labels SHA-256 does not match the frozen manifest")
        if (args.baseline_resources is None) != (args.candidate_resources is None):
            raise ComparisonError(
                "--baseline-resources and --candidate-resources must be supplied together"
            )
        baseline_resources = (
            _read_resources(args.baseline_resources, "baseline")
            if args.baseline_resources is not None
            else None
        )
        candidate_resources = (
            _read_resources(args.candidate_resources, "candidate")
            if args.candidate_resources is not None
            else None
        )
        report = compare(
            manifest,
            acceptance._read_labels(args.labels),
            acceptance._read_results(args.baseline_results),
            acceptance._read_results(args.candidate_results),
            baseline_name=args.baseline_name,
            candidate_name=args.candidate_name,
            baseline_revision=args.baseline_revision,
            candidate_revision=args.candidate_revision,
            baseline_flags=args.baseline_flag,
            candidate_flags=args.candidate_flag,
            evidence_level=args.evidence_level,
            corpus_status=args.corpus_status,
            baseline_resources=baseline_resources,
            candidate_resources=candidate_resources,
        )
        report["inputs"] = {
            "manifest_sha256": acceptance._sha256(args.manifest),
            "labels_sha256": acceptance._sha256(args.labels),
            "baseline_results_sha256": acceptance._sha256(args.baseline_results),
            "candidate_results_sha256": acceptance._sha256(args.candidate_results),
            "baseline_resources_sha256": (
                acceptance._sha256(args.baseline_resources)
                if args.baseline_resources is not None
                else None
            ),
            "candidate_resources_sha256": (
                acceptance._sha256(args.candidate_resources)
                if args.candidate_resources is not None
                else None
            ),
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print(json.dumps(report, sort_keys=True))
        return 0
    except (acceptance.EvaluationError, ComparisonError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
