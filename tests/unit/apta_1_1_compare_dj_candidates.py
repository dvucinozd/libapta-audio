#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
MODULE_PATH = TOOLS_DIR / "apta_1_1_compare_dj_candidates.py"
spec = importlib.util.spec_from_file_location("apta_1_1_compare", MODULE_PATH)
assert spec is not None and spec.loader is not None
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class CandidateComparisonTests(unittest.TestCase):
    def setUp(self) -> None:
        self.ids = [f"track-{index:03d}" for index in range(4)]
        self.manifest: dict[str, object] = {"track_ids": self.ids}
        self.labels = [self.truth(index, track) for index, track in enumerate(self.ids)]

    @staticmethod
    def truth(index: int, track: str) -> dict[str, object]:
        return {
            "track": track,
            "key_tonic": index,
            "key_mode": "major" if index % 2 == 0 else "minor",
            "meter_numerator": 4,
            "meter_denominator": 4,
            "downbeat_frame": index * 96000,
            "beat_period_frames": 24000.0,
        }

    @staticmethod
    def result(truth: dict[str, object]) -> dict[str, object]:
        return {
            **truth,
            "key_confidence": 80,
            "meter_confidence": 80,
            "downbeat_confidence": 80,
            "grid_confidence": 80,
        }

    def compare(
        self,
        baseline: list[dict[str, object]],
        candidate: list[dict[str, object]],
        **kwargs: object,
    ) -> dict[str, object]:
        return comparison.compare(
            self.manifest,
            self.labels,
            baseline,
            candidate,
            baseline_name="baseline",
            candidate_name="candidate",
            baseline_revision="a" * 40,
            candidate_revision="b" * 40,
            corpus_status="spent",
            **kwargs,
        )

    def test_reports_fixes_breaks_safety_and_missing_results(self) -> None:
        baseline = [self.result(row) for row in self.labels[:3]]
        candidate = [self.result(row) for row in self.labels]
        baseline[1]["key_tonic"] = 9
        candidate[2]["key_tonic"] = 9
        candidate[2]["beat_period_frames"] = 12000.0

        report = self.compare(baseline, candidate)
        key = report["families"]["key"]
        grid = report["families"]["beatgrid"]

        self.assertEqual(report["execution_failures"]["baseline"], 1)
        self.assertEqual(report["execution_failures"]["candidate"], 0)
        self.assertEqual(key["baseline"]["correct"], 2)
        self.assertEqual(key["candidate"]["correct"], 3)
        self.assertEqual(key["transitions"]["fixes"], 2)
        self.assertEqual(key["transitions"]["breaks"], 1)
        self.assertEqual(key["transitions"]["net_fixes"], 1)
        self.assertEqual(key["transitions"]["changed_verdicts"], 2)
        self.assertEqual(key["baseline"]["high_confidence_errors"], 1)
        self.assertEqual(key["candidate"]["high_confidence_errors"], 1)
        self.assertEqual(grid["transitions"]["fixes"], 1)
        self.assertEqual(grid["transitions"]["breaks"], 1)
        self.assertFalse(report["acceptance_claim"])

    def test_rejects_result_outside_manifest(self) -> None:
        baseline = [self.result(row) for row in self.labels]
        candidate = [self.result(row) for row in self.labels]
        candidate.append(self.result(self.truth(9, "track-extra")))
        with self.assertRaisesRegex(comparison.ComparisonError, "outside the manifest"):
            self.compare(baseline, candidate)

    def test_reports_exact_resource_delta(self) -> None:
        rows = [self.result(row) for row in self.labels]
        report = self.compare(
            rows,
            rows,
            baseline_resources={"workspace_bytes": 1000, "runtime_seconds": 4.0},
            candidate_resources={"workspace_bytes": 1128, "runtime_seconds": 4.5},
        )
        self.assertTrue(report["resources"]["available"])
        self.assertEqual(report["resources"]["delta"]["workspace_bytes"], 128)
        self.assertEqual(report["resources"]["delta"]["runtime_seconds"], 0.5)

    def test_requires_matching_resource_keys(self) -> None:
        rows = [self.result(row) for row in self.labels]
        with self.assertRaisesRegex(comparison.ComparisonError, "keys must match"):
            self.compare(
                rows,
                rows,
                baseline_resources={"workspace_bytes": 1000},
                candidate_resources={"runtime_seconds": 4.5},
            )

    def test_report_is_deterministic_and_normalizes_flags(self) -> None:
        rows = [self.result(row) for row in self.labels]
        first = self.compare(
            rows,
            rows,
            baseline_flags=["Z=ON", "A=OFF", "Z=ON"],
            candidate_flags=["B=ON", "A=ON"],
        )
        second = self.compare(
            rows,
            rows,
            baseline_flags=["A=OFF", "Z=ON"],
            candidate_flags=["A=ON", "B=ON", "A=ON"],
        )

        self.assertEqual(first, second)
        self.assertEqual(first["baseline"]["flags"], ["A=OFF", "Z=ON"])
        self.assertEqual(first["candidate"]["flags"], ["A=ON", "B=ON"])


if __name__ == "__main__":
    unittest.main()
