#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preflight and score the disjoint FMAK semitone-band key split."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterable, Iterator

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apta_1_1_fmak_temporal_profile_key_development as second

transport = second.prior
FORMAT = "apta-1.1-fmak-semitone-band-key-development-1"
REPORT_FORMAT = "apta-1.1-fmak-semitone-band-key-report-1"
COMPARISON_FORMAT = "apta-1.1-fmak-semitone-band-key-comparison-1"
SELECTION_SEED = "apta-1.1-fmak-semitone-band-v1"
SELECTION_SHA256 = "87e62603ea8213056d1f5fd8ef8e8b50ff6fa914f795859ac840d55f48194c5f"
FIRST_SELECTION_SHA256 = second.PRIOR_SELECTION_SHA256
SECOND_SELECTION_SHA256 = second.SELECTION_SHA256
REMAINING_ELIGIBLE_COUNT = 503
PER_CLASS_QUOTA = 3
TRACK_COUNT = 72
STATE_DELTA_LIMIT = 1024
WORKSPACE_DELTA_LIMIT = 1024
RESONATOR_DELTA = 72

Candidate = transport.Candidate
_TRANSPORT_SELECT_CANDIDATES = transport.select_candidates
_TRANSPORT_TRACK_COUNT = transport.TRACK_COUNT
_TRANSPORT_PER_CLASS_QUOTA = transport.PER_CLASS_QUOTA


def _stable_key(row: Candidate) -> tuple[str, int]:
    digest = hashlib.sha256(
        f"{SELECTION_SEED}:track:{row.source_id}".encode()
    ).hexdigest()
    return digest, row.source_id


def _spent_selections(
    inventory: list[Candidate],
) -> tuple[list[Candidate], list[Candidate]]:
    # The transport context temporarily installs this module's third-split
    # selector so its prepare/run/evaluate machinery uses the frozen 72-track
    # geometry. Restore the original first-split selector while reconstructing
    # the two spent selections, including its 96-track / four-per-class
    # geometry; otherwise the patched 72-track / three-per-class globals alter
    # the supposedly spent selections.
    saved_selector = transport.select_candidates
    saved_track_count = transport.TRACK_COUNT
    saved_per_class_quota = transport.PER_CLASS_QUOTA
    try:
        transport.select_candidates = _TRANSPORT_SELECT_CANDIDATES
        transport.TRACK_COUNT = _TRANSPORT_TRACK_COUNT
        transport.PER_CLASS_QUOTA = _TRANSPORT_PER_CLASS_QUOTA
        first = second._select_prior_candidates(inventory)
        if transport.selection_sha256(first) != FIRST_SELECTION_SHA256:
            raise transport.shared.ValidationError("first spent FMAK selection changed")
        selected_second = second.select_candidates(inventory)
        if transport.selection_sha256(selected_second) != SECOND_SELECTION_SHA256:
            raise transport.shared.ValidationError("second spent FMAK selection changed")
        return first, selected_second
    finally:
        transport.select_candidates = saved_selector
        transport.TRACK_COUNT = saved_track_count
        transport.PER_CLASS_QUOTA = saved_per_class_quota


def select_candidates(rows: Iterable[Candidate]) -> list[Candidate]:
    inventory = list(rows)
    eligible = [row for row in inventory if row.source_id < 20_000]
    if len(eligible) != transport.ELIGIBLE_COUNT:
        raise transport.shared.ValidationError("FMAK 000-019 eligible inventory changed")
    first, selected_second = _spent_selections(inventory)
    spent_ids = {row.source_id for row in first}
    spent_ids.update(row.source_id for row in selected_second)
    remaining = [row for row in eligible if row.source_id not in spent_ids]
    if len(remaining) != REMAINING_ELIGIBLE_COUNT:
        raise transport.shared.ValidationError("third-split FMAK inventory changed")

    selected: list[Candidate] = []
    for mode in ("major", "minor"):
        for tonic in range(12):
            group = sorted(
                (
                    row
                    for row in remaining
                    if row.key_tonic == tonic and row.key_mode == mode
                ),
                key=_stable_key,
            )
            if len(group) < PER_CLASS_QUOTA:
                raise transport.shared.ValidationError(
                    "remaining FMAK class cannot satisfy frozen quota"
                )
            selected.extend(group[:PER_CLASS_QUOTA])
    selected.sort(key=lambda row: (row.key_tonic, row.key_mode, row.source_id))
    selected_ids = {row.source_id for row in selected}
    if len(selected) != TRACK_COUNT or len(selected_ids) != TRACK_COUNT:
        raise AssertionError("FMAK semitone-band selection is not 72 unique tracks")
    if selected_ids & spent_ids:
        raise AssertionError("FMAK semitone-band selection overlaps a spent split")
    return selected


def preflight(metadata: Path) -> dict[str, Any]:
    inventory = transport.inventory(metadata)
    first, selected_second = _spent_selections(inventory)
    selected = select_candidates(inventory)
    seal = transport.selection_sha256(selected)
    if seal != SELECTION_SHA256:
        raise transport.shared.ValidationError(
            "FMAK semitone-band frozen selection seal changed"
        )
    first_ids = {row.source_id for row in first}
    second_ids = {row.source_id for row in selected_second}
    selected_ids = {row.source_id for row in selected}
    return {
        "format": FORMAT,
        "operation": "preflight",
        "acceptance_claim": False,
        "research_only": True,
        "metadata_count": transport.METADATA_COUNT,
        "archive_eligible_count": transport.ELIGIBLE_COUNT,
        "remaining_eligible_count": REMAINING_ELIGIBLE_COUNT,
        "track_count": len(selected),
        "mode_counts": {
            mode: sum(row.key_mode == mode for row in selected)
            for mode in ("major", "minor")
        },
        "class_counts": {
            f"{tonic}:{mode}": sum(
                row.key_tonic == tonic and row.key_mode == mode
                for row in selected
            )
            for tonic in range(12)
            for mode in ("major", "minor")
        },
        "first_selection_sha256": transport.selection_sha256(first),
        "second_selection_sha256": transport.selection_sha256(selected_second),
        "selection_sha256": seal,
        "first_overlap_count": len(first_ids & selected_ids),
        "second_overlap_count": len(second_ids & selected_ids),
    }


@contextmanager
def _configured_transport() -> Iterator[None]:
    replacements = {
        "FORMAT": FORMAT,
        "REPORT_FORMAT": REPORT_FORMAT,
        "COMPARISON_FORMAT": COMPARISON_FORMAT,
        "SELECTION_SEED": SELECTION_SEED,
        "SELECTION_SHA256": SELECTION_SHA256,
        "TRACK_COUNT": TRACK_COUNT,
        "PER_CLASS_QUOTA": PER_CLASS_QUOTA,
        "select_candidates": select_candidates,
    }
    saved = {name: getattr(transport, name) for name in replacements}
    try:
        for name, value in replacements.items():
            setattr(transport, name, value)
        yield
    finally:
        for name, value in saved.items():
            setattr(transport, name, value)


def prepare(
    metadata: Path, archive: Path, output: Path, ffmpeg: str, frozen_utc: str
) -> dict[str, Any]:
    with _configured_transport():
        return transport.prepare(metadata, archive, output, ffmpeg, frozen_utc)


def load_prepared(prepared: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    with _configured_transport():
        return transport.load_prepared(prepared)


def run_analysis(
    prepared: Path, analyzer: Path, output: Path, source_revision: str
) -> dict[str, Any]:
    with _configured_transport():
        return transport.run_analysis(prepared, analyzer, output, source_revision)


def evaluate(
    prepared: Path, inspector: Path, mapping: Path, report: Path
) -> dict[str, Any]:
    with _configured_transport():
        return transport.evaluate(prepared, inspector, mapping, report)


def compare_reports(
    baseline_path: Path,
    candidate_path: Path,
    baseline_revision: str,
    candidate_revision: str,
    candidate_flag: str,
    werror_pass: bool,
    sanitizer_pass: bool,
    runtime_pass: bool,
    default_bytes_unchanged: bool,
    state_delta_bytes: int,
    workspace_delta_bytes: int,
    result_pool_delta_bytes: int,
    resonator_delta: int,
    output: Path,
) -> dict[str, Any]:
    with _configured_transport():
        baseline = transport._load_report(baseline_path)
        candidate = transport._load_report(candidate_path)
    baseline_rows = {row["track"]: row for row in baseline["tracks"]}
    candidate_rows = {row["track"]: row for row in candidate["tracks"]}
    if set(baseline_rows) != set(candidate_rows):
        raise transport.shared.ValidationError("baseline and candidate report IDs differ")
    fixes: list[str] = []
    breaks: list[str] = []
    changed: list[str] = []
    baseline_high_errors: set[str] = set()
    candidate_high_errors: set[str] = set()
    for track in sorted(baseline_rows):
        base = baseline_rows[track]
        cand = candidate_rows[track]
        if (base["key_tonic"], base["key_mode"]) != (
            cand["key_tonic"], cand["key_mode"]
        ):
            changed.append(track)
        if not base["key_correct"] and cand["key_correct"]:
            fixes.append(track)
        if base["key_correct"] and not cand["key_correct"]:
            breaks.append(track)
        if not base["key_correct"] and base["key_confidence"] >= transport.shared.HIGH_CONFIDENCE:
            baseline_high_errors.add(track)
        if not cand["key_correct"] and cand["key_confidence"] >= transport.shared.HIGH_CONFIDENCE:
            candidate_high_errors.add(track)
    baseline_accuracy = float(baseline["overall"]["key_accuracy"])
    candidate_accuracy = float(candidate["overall"]["key_accuracy"])
    major_accuracy = float(candidate["by_mode"]["major"]["key_accuracy"])
    minor_accuracy = float(candidate["by_mode"]["minor"]["key_accuracy"])
    new_high_errors = sorted(candidate_high_errors - baseline_high_errors)
    gates = {
        "development_accuracy_at_least_70_percent": candidate_accuracy >= 0.70,
        "major_accuracy_at_least_60_percent": major_accuracy >= 0.60,
        "minor_accuracy_at_least_60_percent": minor_accuracy >= 0.60,
        "accuracy_improved": candidate_accuracy > baseline_accuracy,
        "fixes_exceed_breaks": len(fixes) > len(breaks),
        "no_new_high_confidence_errors": not new_high_errors,
        "werror_pass": werror_pass,
        "sanitizer_pass": sanitizer_pass,
        "runtime_pass": runtime_pass,
        "default_bytes_unchanged": default_bytes_unchanged,
        "key_state_delta_within_1024_bytes": 0 <= state_delta_bytes <= STATE_DELTA_LIMIT,
        "workspace_delta_within_1024_bytes": 0 <= workspace_delta_bytes <= WORKSPACE_DELTA_LIMIT,
        "result_pool_unchanged": result_pool_delta_bytes == 0,
        "exactly_72_new_resonators": resonator_delta == RESONATOR_DELTA,
    }
    flag = candidate_flag.strip()
    if not flag:
        raise transport.shared.ValidationError("candidate flag must not be empty")
    value = {
        "format": COMPARISON_FORMAT,
        "split": "development",
        "evidence_level": "local-research-development",
        "acceptance_claim": False,
        "baseline_revision": transport.shared._full_revision(
            baseline_revision, "baseline revision"
        ),
        "candidate_revision": transport.shared._full_revision(
            candidate_revision, "candidate revision"
        ),
        "candidate_flags": [flag],
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
    transport.shared._write_json(output, value)
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
    compare_parser.add_argument("--runtime-pass", action="store_true")
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
                args.runtime_pass,
                args.default_bytes_unchanged,
                args.state_delta_bytes,
                args.workspace_delta_bytes,
                args.result_pool_delta_bytes,
                args.resonator_delta,
                args.output,
            )
    except (OSError, transport.shared.ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(value, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
