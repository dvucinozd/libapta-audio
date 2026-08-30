#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
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

    def test_comparison_uses_own_format_and_frozen_gates(self) -> None:
        def report(correct_count: int) -> dict[str, object]:
            tracks = []
            for index in range(module.TRACK_COUNT):
                expected_tonic = index % 12
                expected_mode = "major" if index < 48 else "minor"
                correct = index < correct_count
                tracks.append(
                    {
                        "track": f"track-{index:03d}",
                        "expected_tonic": expected_tonic,
                        "expected_mode": expected_mode,
                        "key_tonic": expected_tonic
                        if correct
                        else (expected_tonic + 1) % 12,
                        "key_mode": expected_mode,
                        "key_confidence": 50,
                        "key_correct": correct,
                    }
                )
            return {
                "format": module.REPORT_FORMAT,
                "overall": {
                    "track_count": module.TRACK_COUNT,
                    "key_correct": correct_count,
                    "key_accuracy": correct_count / module.TRACK_COUNT,
                },
                "tracks": tracks,
                "by_mode": module.prior._mode_summary(tracks),
            }

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / "baseline.json"
            candidate = root / "candidate.json"
            output = root / "comparison.json"
            baseline.write_text(json.dumps(report(68)), encoding="utf-8")
            candidate.write_text(json.dumps(report(80)), encoding="utf-8")
            value = module.compare_reports(
                baseline,
                candidate,
                "1" * 40,
                "2" * 40,
                "APTA_ENABLE_EXPERIMENTAL_TEMPORAL_PROFILE_KEY=ON",
                True,
                True,
                True,
                100,
                112,
                0,
                0,
                output,
            )
            self.assertEqual(value["format"], module.COMPARISON_FORMAT)
            self.assertTrue(value["holdout_eligible"])


if __name__ == "__main__":
    unittest.main()
