#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).parents[2] / "tools" / "apta_1_1_tempo_ensemble_eval.py"
SPEC = importlib.util.spec_from_file_location("apta_1_1_tempo_ensemble_eval", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
mod = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = mod
SPEC.loader.exec_module(mod)


def manifest(count: int) -> dict[str, object]:
    ids = [f"fresh-{index:03d}" for index in range(count)]
    return {
        "format": mod.FORMAT,
        "track_count": count,
        "track_ids": ids,
        "manifest_sha256": "a" * 64,
        "labels_sha256": "b" * 64,
        "frozen_utc": "2026-08-22T00:00:00Z",
        "reference_source": "manual dual-review reference",
        "verification_procedure": "two independent labels, disagreements adjudicated before candidate run",
        "tempo_bins": {"60-89": count // 2, "90-179": count - count // 2},
    }


def row(track: str, truth: int = 120000, reported: int = 120000, confidence: int = 80, actionable: bool = True) -> dict[str, object]:
    return {
        "track": track,
        "truth": truth,
        "reported": reported,
        "confidence": confidence,
        "actionable": actionable,
    }


class EvaluationTests(unittest.TestCase):
    def test_accepts_improvement_without_safety_regression(self):
        data = manifest(48)
        baseline = [row(track) for track in data["track_ids"]]
        candidate = [dict(item) for item in baseline]
        baseline[0]["reported"] = 60000
        baseline[0]["confidence"] = 90
        candidate[0]["reported"] = 120000
        result = mod.evaluate(data, baseline, candidate)
        self.assertTrue(result["accepted"])
        self.assertEqual(result["transitions"]["fixed_count"], 1)
        self.assertEqual(result["transitions"]["broken_count"], 0)
        self.assertEqual(result["candidate"]["high_confidence_metrical_errors"], 0)

    def test_rejects_broken_exact_selection(self):
        data = manifest(48)
        baseline = [row(track) for track in data["track_ids"]]
        candidate = [dict(item) for item in baseline]
        candidate[0]["reported"] = 60000
        candidate[0]["confidence"] = 90
        result = mod.evaluate(data, baseline, candidate)
        self.assertFalse(result["accepted"])
        self.assertFalse(result["conditions"]["no_exact_accuracy_regression"])
        self.assertFalse(result["conditions"]["no_promotion_regression"])

    def test_rejects_high_confidence_metrical_regression_even_if_exact_count_ties(self):
        data = manifest(48)
        baseline = [row(track) for track in data["track_ids"]]
        candidate = [dict(item) for item in baseline]
        baseline[0]["reported"] = 118000
        baseline[0]["confidence"] = 90
        candidate[0]["reported"] = 60000
        candidate[0]["confidence"] = 90
        result = mod.evaluate(data, baseline, candidate)
        self.assertFalse(result["accepted"])
        self.assertFalse(result["conditions"]["no_metrical_safety_regression"])

    def test_less_than_48_tracks_is_diagnostic_only(self):
        data = manifest(47)
        baseline = [row(track) for track in data["track_ids"]]
        candidate = [dict(item) for item in baseline]
        baseline[0]["reported"] = 60000
        candidate[0]["reported"] = 120000
        result = mod.evaluate(data, baseline, candidate)
        self.assertFalse(result["accepted"])
        self.assertEqual(result["evidence_level"], "diagnostic-only")

    def test_requires_exact_frozen_track_set(self):
        data = manifest(48)
        baseline = [row(track) for track in data["track_ids"]]
        candidate = [dict(item) for item in baseline[:-1]]
        with self.assertRaisesRegex(mod.EvaluationError, "candidate track IDs"):
            mod.evaluate(data, baseline, candidate)

    def test_manifest_rejects_private_or_unregistered_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            data = manifest(48)
            data["artist"] = "must not be tracked"
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(mod.EvaluationError, "unsupported fields"):
                mod._load_manifest(path)


if __name__ == "__main__":
    unittest.main()
