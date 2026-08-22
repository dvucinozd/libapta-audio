#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import csv
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).parents[2] / "tools" / "apta_tempo_ensemble_eval.py"
SPEC = importlib.util.spec_from_file_location("apta_tempo_ensemble_eval", MODULE)
assert SPEC is not None and SPEC.loader is not None
evalmod = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evalmod)


def row(track, truth, reported, relation="exact", confidence=80):
    exact = abs(reported - truth) * 100 <= truth
    return {
        "track": track,
        "truth_millibpm": str(truth),
        "reported_millibpm": str(reported),
        "relation": relation,
        "confidence": str(confidence),
        "octave_error": "1" if relation in {"half", "half-time", "double", "double-time"} and not exact else "0",
    }


class EnsembleEvaluationTests(unittest.TestCase):
    def test_accepts_fixed_metrical_error_without_regression(self):
        ids = ["a", "b"]
        baseline = {
            "a": {"truth": 128000, "reported": 64000, "confidence": 90, "relation": "half-time", "exact": False, "metrical_error": True},
            "b": {"truth": 120000, "reported": 120000, "confidence": 80, "relation": "exact", "exact": True, "metrical_error": False},
        }
        candidate = {
            "a": {"truth": 128000, "reported": 128000, "confidence": 90, "relation": "exact", "exact": True, "metrical_error": False},
            "b": dict(baseline["b"]),
        }
        result = evalmod.evaluate(ids, baseline, candidate)
        self.assertTrue(result["accepted"])
        self.assertEqual(result["comparison"]["fixed_count"], 1)
        self.assertEqual(result["comparison"]["broken_count"], 0)

    def test_rejects_broken_exact_selection(self):
        ids = ["a"]
        baseline = {"a": {"truth": 128000, "reported": 128000, "confidence": 80, "relation": "exact", "exact": True, "metrical_error": False}}
        candidate = {"a": {"truth": 128000, "reported": 64000, "confidence": 90, "relation": "half-time", "exact": False, "metrical_error": True}}
        result = evalmod.evaluate(ids, baseline, candidate)
        self.assertFalse(result["accepted"])
        self.assertFalse(result["gates"]["no_exact_accuracy_regression"])
        self.assertFalse(result["gates"]["no_promotion_regression"])

    def test_result_reader_rejects_duplicate_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "r.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["track", "truth_millibpm", "reported_millibpm", "relation", "confidence", "octave_error"])
                writer.writeheader()
                writer.writerow(row("a", 128000, 128000))
                writer.writerow(row("a", 128000, 128000))
            with self.assertRaisesRegex(ValueError, "duplicate"):
                evalmod.read_results(path)

    def test_main_marks_small_fresh_set_diagnostic_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "manifest.json"
            baseline = root / "baseline.csv"
            candidate = root / "candidate.csv"
            report = root / "report.json"
            manifest.write_text(json.dumps({"tracks": [{"id": "a"}]}) + "\n", encoding="utf-8")
            for path in (baseline, candidate):
                with path.open("w", encoding="utf-8", newline="") as stream:
                    writer = csv.DictWriter(stream, fieldnames=["track", "truth_millibpm", "reported_millibpm", "relation", "confidence", "octave_error"])
                    writer.writeheader()
                    writer.writerow(row("a", 128000, 128000))
            code = evalmod.main(["--manifest", str(manifest), "--baseline", str(baseline), "--candidate", str(candidate), "--report", str(report)])
            self.assertEqual(code, 2)
            data = json.loads(report.read_text(encoding="utf-8"))
            self.assertTrue(data["evidence"]["diagnostic_only"])
            self.assertFalse(data["accepted"])


if __name__ == "__main__":
    unittest.main()
