#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preflight the disjoint FMAK temporal-profile key development split."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterable, Iterator

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apta_1_1_fmak_temporal_key_development as prior

FORMAT = "apta-1.1-fmak-temporal-profile-key-development-1"
REPORT_FORMAT = "apta-1.1-fmak-temporal-profile-key-report-1"
COMPARISON_FORMAT = "apta-1.1-fmak-temporal-profile-key-comparison-1"
SELECTION_SEED = "apta-1.1-fmak-temporal-profile-v1"
PRIOR_SELECTION_SEED = prior.SELECTION_SEED
PRIOR_SELECTION_SHA256 = prior.SELECTION_SHA256
SELECTION_SHA256 = "1c94629c8513fbab9d97e4c05e1394a9b9e1cd5070e126ddd0a0ce0ab1e89121"
REMAINING_ELIGIBLE_COUNT = prior.ELIGIBLE_COUNT - prior.TRACK_COUNT
PER_CLASS_QUOTA = 4
TRACK_COUNT = 96

Candidate = prior.Candidate
PRIOR_SELECT_CANDIDATES = prior.select_candidates


def _select_prior_candidates(rows: Iterable[Candidate]) -> list[Candidate]:
    saved_seed = prior.SELECTION_SEED
    try:
        prior.SELECTION_SEED = PRIOR_SELECTION_SEED
        return PRIOR_SELECT_CANDIDATES(rows)
    finally:
        prior.SELECTION_SEED = saved_seed


def _stable_key(row: Candidate) -> tuple[str, int]:
    digest = hashlib.sha256(
        f"{SELECTION_SEED}:track:{row.source_id}".encode()
    ).hexdigest()
    return digest, row.source_id


def select_candidates(rows: Iterable[Candidate]) -> list[Candidate]:
    inventory = list(rows)
    eligible = [row for row in inventory if row.source_id < 20_000]
    if len(eligible) != prior.ELIGIBLE_COUNT:
        raise prior.shared.ValidationError("FMAK 000-019 eligible inventory changed")

    prior_selection = _select_prior_candidates(inventory)
    if prior.selection_sha256(prior_selection) != PRIOR_SELECTION_SHA256:
        raise prior.shared.ValidationError("prior spent FMAK selection changed")
    prior_ids = {row.source_id for row in prior_selection}
    remaining = [row for row in eligible if row.source_id not in prior_ids]
    if len(remaining) != REMAINING_ELIGIBLE_COUNT:
        raise prior.shared.ValidationError("remaining FMAK inventory changed")

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
                raise prior.shared.ValidationError(
                    "remaining FMAK class cannot satisfy frozen quota"
                )
            selected.extend(group[:PER_CLASS_QUOTA])
    selected.sort(key=lambda row: (row.key_tonic, row.key_mode, row.source_id))
    selected_ids = {row.source_id for row in selected}
    if len(selected) != TRACK_COUNT or len(selected_ids) != TRACK_COUNT:
        raise AssertionError("FMAK temporal-profile selection is not 96 unique tracks")
    if selected_ids & prior_ids:
        raise AssertionError("FMAK temporal-profile selection overlaps the spent split")
    return selected


def selection_sha256(rows: Iterable[Candidate]) -> str:
    return prior.selection_sha256(rows)


def preflight(metadata: Path) -> dict[str, Any]:
    inventory = prior.inventory(metadata)
    prior_selection = _select_prior_candidates(inventory)
    selected = select_candidates(inventory)
    seal = selection_sha256(selected)
    if seal != SELECTION_SHA256:
        raise prior.shared.ValidationError(
            "FMAK temporal-profile frozen selection seal changed"
        )
    prior_ids = {row.source_id for row in prior_selection}
    selected_ids = {row.source_id for row in selected}
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
        "metadata_count": prior.METADATA_COUNT,
        "archive_eligible_count": prior.ELIGIBLE_COUNT,
        "remaining_eligible_count": REMAINING_ELIGIBLE_COUNT,
        "track_count": len(selected),
        "mode_counts": {
            mode: sum(row.key_mode == mode for row in selected)
            for mode in ("major", "minor")
        },
        "class_counts": class_counts,
        "prior_selection_sha256": prior.selection_sha256(prior_selection),
        "selection_sha256": seal,
        "prior_overlap_count": len(prior_ids & selected_ids),
    }


@contextmanager
def _configured_prior_protocol() -> Iterator[None]:
    """Reuse the frozen FMAK transport/evaluator with this protocol identity."""
    replacements = {
        "FORMAT": FORMAT,
        "REPORT_FORMAT": REPORT_FORMAT,
        "COMPARISON_FORMAT": COMPARISON_FORMAT,
        "SELECTION_SEED": SELECTION_SEED,
        "SELECTION_SHA256": SELECTION_SHA256,
        "select_candidates": select_candidates,
    }
    saved = {name: getattr(prior, name) for name in replacements}
    try:
        for name, value in replacements.items():
            setattr(prior, name, value)
        yield
    finally:
        for name, value in saved.items():
            setattr(prior, name, value)


def prepare(
    metadata: Path,
    archive: Path,
    output: Path,
    ffmpeg: str,
    frozen_utc: str,
) -> dict[str, Any]:
    with _configured_prior_protocol():
        return prior.prepare(metadata, archive, output, ffmpeg, frozen_utc)


def load_prepared(prepared: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    with _configured_prior_protocol():
        return prior.load_prepared(prepared)


def run_analysis(
    prepared: Path, analyzer: Path, output: Path, source_revision: str
) -> dict[str, Any]:
    with _configured_prior_protocol():
        return prior.run_analysis(prepared, analyzer, output, source_revision)


def evaluate(
    prepared: Path, inspector: Path, mapping: Path, report: Path
) -> dict[str, Any]:
    with _configured_prior_protocol():
        return prior.evaluate(prepared, inspector, mapping, report)


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
    with _configured_prior_protocol():
        return prior.compare_reports(
            baseline_path,
            candidate_path,
            baseline_revision,
            candidate_revision,
            candidate_flag,
            werror_pass,
            sanitizer_pass,
            default_bytes_unchanged,
            state_delta_bytes,
            workspace_delta_bytes,
            result_pool_delta_bytes,
            resonator_delta,
            output,
        )


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
    except (OSError, prior.shared.ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(value, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
