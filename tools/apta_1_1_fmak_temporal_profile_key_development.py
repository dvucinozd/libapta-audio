#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preflight the disjoint FMAK temporal-profile key development split."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any, Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apta_1_1_fmak_temporal_key_development as prior

FORMAT = "apta-1.1-fmak-temporal-profile-key-development-1"
SELECTION_SEED = "apta-1.1-fmak-temporal-profile-v1"
PRIOR_SELECTION_SHA256 = prior.SELECTION_SHA256
SELECTION_SHA256 = "1c94629c8513fbab9d97e4c05e1394a9b9e1cd5070e126ddd0a0ce0ab1e89121"
REMAINING_ELIGIBLE_COUNT = prior.ELIGIBLE_COUNT - prior.TRACK_COUNT
PER_CLASS_QUOTA = 4
TRACK_COUNT = 96

Candidate = prior.Candidate


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

    prior_selection = prior.select_candidates(inventory)
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
    prior_selection = prior.select_candidates(inventory)
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, required=True)
    args = parser.parse_args()
    try:
        value = preflight(args.metadata)
    except (OSError, prior.shared.ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(value, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
