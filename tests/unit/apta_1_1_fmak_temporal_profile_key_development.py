#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location(
    "apta_1_1_fmak_temporal_profile_key_development",
    TOOLS / "apta_1_1_fmak_temporal_profile_key_development.py",
)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = module
SPEC.loader.exec_module(module)


def synthetic_inventory() -> list[module.Candidate]:
    rows: list[module.Candidate] = []
    source_id = 1
    classes = [(tonic, mode) for mode in ("major", "minor") for tonic in range(12)]
    for index, (tonic, mode) in enumerate(classes):
        count = 28 if index == len(classes) - 1 else 29
        for _unused in range(count):
            rows.append(module.Candidate(source_id, tonic, mode))
            source_id += 1
    assert len(rows) == module.prior.ELIGIBLE_COUNT
    return rows


class FmakTemporalProfileKeyDevelopmentTests(unittest.TestCase):
    def test_selection_is_disjoint_deterministic_and_class_balanced(self) -> None:
        rows = synthetic_inventory()
        prior_selection = module.prior.select_candidates(rows)
        prior_ids = {row.source_id for row in prior_selection}
        original = module.PRIOR_SELECTION_SHA256
        try:
            module.PRIOR_SELECTION_SHA256 = module.prior.selection_sha256(
                prior_selection
            )
            first = module.select_candidates(rows)
            second = module.select_candidates(reversed(rows))
        finally:
            module.PRIOR_SELECTION_SHA256 = original
        self.assertEqual(first, second)
        self.assertEqual(len(first), module.TRACK_COUNT)
        self.assertFalse(prior_ids & {row.source_id for row in first})
        for tonic in range(12):
            for mode in ("major", "minor"):
                self.assertEqual(
                    sum(
                        row.key_tonic == tonic and row.key_mode == mode
                        for row in first
                    ),
                    module.PER_CLASS_QUOTA,
                )

    def test_selection_rejects_archive_inventory_drift(self) -> None:
        rows = synthetic_inventory()
        with self.assertRaises(module.prior.shared.ValidationError):
            module.select_candidates(rows[:-1])

    def test_selection_rejects_changed_spent_split_seal(self) -> None:
        rows = synthetic_inventory()
        original = module.PRIOR_SELECTION_SHA256
        try:
            module.PRIOR_SELECTION_SHA256 = "0" * 64
            with self.assertRaises(module.prior.shared.ValidationError):
                module.select_candidates(rows)
        finally:
            module.PRIOR_SELECTION_SHA256 = original


if __name__ == "__main__":
    unittest.main()
