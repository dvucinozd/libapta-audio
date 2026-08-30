#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import apta_1_1_fmak_semitone_band_key_development as module  # noqa: E402


def synthetic_inventory() -> list[module.Candidate]:
    rows: list[module.Candidate] = []
    source_id = 1
    classes = [(tonic, mode) for mode in ("major", "minor") for tonic in range(12)]
    for index, (tonic, mode) in enumerate(classes):
        count = 28 if index == len(classes) - 1 else 29
        for _unused in range(count):
            rows.append(module.Candidate(source_id, tonic, mode))
            source_id += 1
    return rows


class FmakSemitoneBandKeyDevelopmentTests(unittest.TestCase):
    def _patched_seals(self, rows: list[module.Candidate]) -> tuple[str, str, str]:
        first = module.second._select_prior_candidates(rows)
        first_seal = module.transport.selection_sha256(first)
        saved_second_prior = module.second.PRIOR_SELECTION_SHA256
        module.second.PRIOR_SELECTION_SHA256 = first_seal
        try:
            selected_second = module.second.select_candidates(rows)
        finally:
            module.second.PRIOR_SELECTION_SHA256 = saved_second_prior
        return first_seal, module.transport.selection_sha256(selected_second), saved_second_prior

    def test_selection_is_disjoint_deterministic_and_balanced(self) -> None:
        rows = synthetic_inventory()
        first_seal, second_seal, saved_second_prior = self._patched_seals(rows)
        saved = (module.FIRST_SELECTION_SHA256, module.SECOND_SELECTION_SHA256)
        try:
            module.FIRST_SELECTION_SHA256 = first_seal
            module.SECOND_SELECTION_SHA256 = second_seal
            module.second.PRIOR_SELECTION_SHA256 = first_seal
            first = module.select_candidates(rows)
            again = module.select_candidates(reversed(rows))
            spent_first, spent_second = module._spent_selections(rows)
        finally:
            module.FIRST_SELECTION_SHA256, module.SECOND_SELECTION_SHA256 = saved
            module.second.PRIOR_SELECTION_SHA256 = saved_second_prior
        self.assertEqual(first, again)
        self.assertEqual(len(first), module.TRACK_COUNT)
        spent = {row.source_id for row in spent_first + spent_second}
        self.assertFalse(spent & {row.source_id for row in first})
        for tonic in range(12):
            for mode in ("major", "minor"):
                self.assertEqual(
                    sum(
                        row.key_tonic == tonic and row.key_mode == mode
                        for row in first
                    ),
                    module.PER_CLASS_QUOTA,
                )

    def test_selection_rejects_inventory_drift(self) -> None:
        with self.assertRaises(module.transport.shared.ValidationError):
            module.select_candidates(synthetic_inventory()[:-1])

    def test_transport_context_reconstructs_spent_selections(self) -> None:
        rows = synthetic_inventory()
        first_seal, second_seal, saved_second_prior = self._patched_seals(rows)
        saved = (module.FIRST_SELECTION_SHA256, module.SECOND_SELECTION_SHA256)
        try:
            module.FIRST_SELECTION_SHA256 = first_seal
            module.SECOND_SELECTION_SHA256 = second_seal
            module.second.PRIOR_SELECTION_SHA256 = first_seal
            with module._configured_transport():
                selected = module.transport.select_candidates(rows)
        finally:
            module.FIRST_SELECTION_SHA256, module.SECOND_SELECTION_SHA256 = saved
            module.second.PRIOR_SELECTION_SHA256 = saved_second_prior
        self.assertEqual(len(selected), module.TRACK_COUNT)

    def test_comparison_accepts_frozen_resource_shape(self) -> None:
        def report(correct_count: int) -> dict[str, object]:
            tracks = []
            for index in range(module.TRACK_COUNT):
                expected_tonic = index % 12
                expected_mode = "major" if index < 36 else "minor"
                correct = index < correct_count
                tracks.append(
                    {
                        "track": f"track-{index:03d}",
                        "expected_tonic": expected_tonic,
                        "expected_mode": expected_mode,
                        "key_tonic": expected_tonic if correct else (expected_tonic + 1) % 12,
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
                "by_mode": module.transport._mode_summary(tracks),
            }

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / "baseline.json"
            candidate = root / "candidate.json"
            output = root / "comparison.json"
            baseline.write_text(json.dumps(report(40)), encoding="utf-8")
            candidate.write_text(json.dumps(report(60)), encoding="utf-8")
            value = module.compare_reports(
                baseline,
                candidate,
                "1" * 40,
                "2" * 40,
                "APTA_ENABLE_EXPERIMENTAL_SEMITONE_BAND_KEY=ON",
                True,
                True,
                True,
                True,
                960,
                960,
                0,
                72,
                output,
            )
        self.assertTrue(value["holdout_eligible"])
        self.assertEqual(value["format"], module.COMPARISON_FORMAT)


if __name__ == "__main__":
    unittest.main()
