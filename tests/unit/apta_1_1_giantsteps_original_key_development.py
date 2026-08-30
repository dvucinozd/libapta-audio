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
    "apta_1_1_giantsteps_original_key_development",
    TOOLS / "apta_1_1_giantsteps_original_key_development.py",
)
assert SPEC is not None and SPEC.loader is not None
module = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = module
SPEC.loader.exec_module(module)


def candidate(index: int, tonic: int, mode: str) -> module.Candidate:
    tonic_name = module.TONIC_NAMES[tonic]
    return module.Candidate(
        source_id=f"{index:06d}.LOFI",
        key_name=f"{tonic_name} {mode}",
        key_tonic=tonic,
        key_mode=mode,
        transport_md5=f"{index:032x}"[-32:],
    )


class OriginalKeyDevelopmentTests(unittest.TestCase):
    def test_normalizes_flats_and_rejects_unknown_labels(self) -> None:
        self.assertEqual(module.normalize_key("Db major"), "c# major")
        self.assertEqual(module.normalize_key("B♭ minor"), "a# minor")
        with self.assertRaises(module.shared.ValidationError):
            module.normalize_key("H major")

    def test_selection_is_deterministic_and_mode_balanced(self) -> None:
        rows: list[module.Candidate] = []
        index = 1
        for mode in ("major", "minor"):
            for tonic in range(12):
                count = 1 if mode == "major" and tonic == 1 else 8
                for _unused in range(count):
                    rows.append(candidate(index, tonic, mode))
                    index += 1
        first = module.select_candidates(rows)
        second = module.select_candidates(reversed(rows))
        self.assertEqual(first, second)
        self.assertEqual(len(first), 96)
        self.assertEqual(sum(row.key_mode == "major" for row in first), 48)
        self.assertEqual(sum(row.key_mode == "minor" for row in first), 48)
        self.assertEqual(
            sum(row.key_mode == "major" and row.key_tonic == 1 for row in first),
            1,
        )
        self.assertEqual(module.selection_sha256(first), module.selection_sha256(second))

    def test_comparison_requires_metric_and_external_gates(self) -> None:
        tracks = []
        for index in range(96):
            mode = "major" if index < 48 else "minor"
            tonic = index % 12
            tracks.append(
                {
                    "track": f"track-{index:024x}",
                    "expected_tonic": tonic,
                    "expected_mode": mode,
                    "key_tonic": tonic,
                    "key_mode": mode,
                    "key_confidence": 70,
                    "key_correct": True,
                    "error_family": "exact",
                }
            )
        baseline_tracks = [dict(row) for row in tracks]
        for row in baseline_tracks[:48]:
            row["key_mode"] = "minor"
            row["key_correct"] = False
            row["error_family"] = "parallel"

        def report(rows: list[dict[str, object]]) -> dict[str, object]:
            correct = sum(bool(row["key_correct"]) for row in rows)
            return {
                "format": module.REPORT_FORMAT,
                "split": "development",
                "evidence_level": "local-research-development",
                "acceptance_claim": False,
                "overall": {
                    "track_count": 96,
                    "key_correct": correct,
                    "key_accuracy": correct / 96,
                    "high_confidence_threshold": 75,
                    "high_confidence_key_errors": 0,
                },
                "error_families": {},
                "by_class": {},
                "tracks": rows,
                "by_mode": module._by_mode(rows),
            }

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / "baseline.json"
            centered = root / "candidate.json"
            output = root / "comparison.json"
            baseline.write_text(json.dumps(report(baseline_tracks)), encoding="utf-8")
            centered.write_text(json.dumps(report(tracks)), encoding="utf-8")
            value = module.compare_reports(
                baseline,
                centered,
                "1" * 40,
                "2" * 40,
                "APTA_ENABLE_EXPERIMENTAL_CENTERED_KEY_CORRELATION=ON",
                True,
                False,
                True,
                0,
                0,
                0,
                output,
            )
            self.assertFalse(value["holdout_eligible"])
            self.assertFalse(value["gates"]["sanitizer_pass"])
            value = module.compare_reports(
                baseline,
                centered,
                "1" * 40,
                "2" * 40,
                "APTA_ENABLE_EXPERIMENTAL_CENTERED_KEY_CORRELATION=ON",
                True,
                True,
                True,
                0,
                0,
                0,
                output,
            )
            self.assertTrue(value["holdout_eligible"])


if __name__ == "__main__":
    unittest.main()
