#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import sys
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).parents[2] / "tools" / "apta_confidence_calibration.py"
SPEC = importlib.util.spec_from_file_location("apta_confidence_calibration", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
mod = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = mod
SPEC.loader.exec_module(mod)


def rows(prefix: str, count: int, raw: int, correct: int):
    return [
        {"id": f"{prefix}-{index:03d}", "raw_confidence": raw, "correct": correct}
        for index in range(count)
    ]


class CalibrationTests(unittest.TestCase):
    def test_isotonic_lut_is_monotone(self):
        training = rows("a", 32, 20, 0) + rows("b", 32, 50, 1) + rows("c", 32, 80, 1)
        lut = mod.isotonic_lut(training)
        self.assertEqual(len(lut), 101)
        self.assertTrue(all(lut[index] <= lut[index + 1] for index in range(100)))

    def test_model_is_deterministic(self):
        training = rows("a", 48, 25, 0) + rows("b", 48, 80, 1)
        first = mod.canonical_model(training)
        second = mod.canonical_model(list(reversed(training)))
        self.assertEqual(first, second)
        self.assertGreater(first["calibration_model_id"], 0)

    def test_accepts_holdout_improvement(self):
        training = rows("train-low", 48, 90, 0) + rows("train-high", 48, 95, 1)
        model = mod.canonical_model(training)
        holdout = rows("hold-low", 24, 90, 0) + rows("hold-high", 24, 95, 1)
        result = mod.evaluate(model, training, holdout)
        self.assertTrue(result["accepted"])
        self.assertTrue(result["conditions"]["brier_not_worse"])
        self.assertTrue(any(result["benefits"].values()))

    def test_small_holdout_is_diagnostic_only(self):
        training = rows("train-low", 48, 90, 0) + rows("train-high", 48, 95, 1)
        model = mod.canonical_model(training)
        holdout = rows("hold-low", 12, 90, 0) + rows("hold-high", 12, 95, 1)
        result = mod.evaluate(model, training, holdout)
        self.assertFalse(result["accepted"])
        self.assertEqual(result["evidence_level"], "diagnostic-only")

    def test_rejects_training_holdout_overlap(self):
        training = rows("sample", 96, 80, 1)
        model = mod.canonical_model(training)
        with self.assertRaisesRegex(mod.CalibrationError, "overlap"):
            mod.evaluate(model, training, rows("sample", 48, 80, 1))

    def test_rejects_model_not_matching_training(self):
        training = rows("train", 96, 80, 1)
        model = mod.canonical_model(training)
        changed = list(training)
        changed[0] = dict(changed[0], correct=0)
        with self.assertRaisesRegex(mod.CalibrationError, "does not match"):
            mod.evaluate(model, changed, rows("hold", 48, 80, 1))


if __name__ == "__main__":
    unittest.main()
