#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Synthetic-only regressions for candidate coverage and input isolation."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_lattice_coverage_audit as audit


def labels():
    return [{"track": f"track-{i:024x}", "split": "development" if i < 40 else "holdout",
             "meter_numerator": 3 if i % 40 < 20 else 4, "meter_denominator": 4,
             "beat_frames": [0, 25600, 51200]} for i in range(80)]


class CoverageTests(unittest.TestCase):
    def test_scan_matches_frozen_top3(self):
        rng = np.random.default_rng(114)
        for rate in (44100, 48000, 96000):
            signal = rng.random(4096)
            expected = audit._candidate_lags(signal, rate)
            actual = audit.ranked(audit.full_scan(signal, rate))[:3]
            self.assertEqual([r["lag"] for r in actual], [x[0] for x in expected])
            np.testing.assert_allclose([r["score"] for r in actual], [x[1] for x in expected])

    def test_silence_has_no_positive_candidates(self):
        scan = audit.full_scan(np.zeros(4096), 48000)
        self.assertEqual(audit.ranked(scan), [])
        self.assertEqual(audit.local_maxima(scan), [])

    def test_peaks_keep_lowest_plateau_bin_and_boundaries(self):
        rows = [{"lag": i + 40, "score": v} for i, v in enumerate([3, 1, 2, 2, 1, 4])]
        self.assertEqual([r["lag"] for r in audit.local_maxima(rows)], [45, 40, 42])

    def test_neighboring_bins_can_exhaust_slots(self):
        rows = [{"lag": i + 40, "score": v} for i, v in enumerate([9, 10, 9, 0, 8])]
        self.assertIsNone(audit.first_correct_rank(audit.ranked(rows)[:3], 44))
        self.assertEqual(audit.first_correct_rank(audit.local_maxima(rows), 44), 2)

    def test_peak_coverage_is_not_assumed_to_dominate(self):
        rows = [{"lag": i + 40, "score": v} for i, v in enumerate([10, 9, 0, 8])]
        self.assertEqual(audit.first_correct_rank(audit.ranked(rows), 41), 2)
        self.assertIsNone(audit.first_correct_rank(audit.local_maxima(rows), 41))

    def test_nonfinite_negative_shape_and_bad_rates_rejected(self):
        for signal in (np.zeros(2), np.zeros((4096, 1)), np.full(4096, np.nan),
                       np.full(4096, np.inf), np.full(4096, -1)):
            with self.assertRaises(ValueError):
                audit.full_scan(signal, 48000)
        for rate in (True, 0, 48000.5, 1000000):
            with self.assertRaises(ValueError):
                audit.full_scan(np.zeros(4096), rate)

    def test_only_development_annotations_validated(self):
        rows = labels()
        for row in rows[40:]:
            row["beat_frames"] = "must not inspect holdout beats"
        result = audit.development_labels(rows)
        self.assertEqual(len(result), 40)
        self.assertTrue(all(r["split"] == "development" for r in result.values()))

    def test_private_duplicate_mislabelled_and_unbalanced_rejected(self):
        mutations = (("track", "private-song"), ("track", "track-" + "0" * 24),
                     ("split", "holdout"), ("meter_numerator", 4),
                     ("beat_frames", [0, 25600, 25600]))
        for field, value in mutations:
            rows = labels()
            rows[1][field] = value
            with self.assertRaises(ValueError):
                audit.development_labels(rows)

    def test_hash_mismatch_stops_before_trace_access(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "labels.json"
            path.write_text(json.dumps(labels()), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "labels hash"):
                audit.evaluate(path, root / "absent", "ASAP", "a" * 40)

    def test_rejects_holdout_traces_before_loading_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "labels.json"
            path.write_text(json.dumps(labels()), encoding="utf-8")
            (root / (labels()[40]["track"] + ".ndjson")).write_text("NOT JSON", encoding="utf-8")
            with patch.dict(audit.PINS, {"ASAP": (audit._sha256(path), "unused")}):
                with self.assertRaisesRegex(ValueError, "coverage"):
                    audit.evaluate(path, root, "ASAP", "a" * 40)

    def test_end_to_end_determinism_and_capture_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "labels.json"
            path.write_text(json.dumps(labels()), encoding="utf-8")
            signal = np.zeros(4096)
            signal[::100] = 1
            trace = {"sample_rate": 48000, "onset_evidence_first_bin": 0,
                     "onset_flux": signal.tolist(),
                     "tempo_candidates": [{"lag_bins": lag} for lag, _ in
                                          audit._candidate_lags(signal, 48000)]}
            paths = []
            for row in labels()[:40]:
                target = root / (row["track"] + ".ndjson")
                target.write_text(json.dumps(trace), encoding="utf-8")
                paths.append(target)
            pins = {"ASAP": (audit._sha256(path), audit._trace_set_sha256(paths))}
            with patch.dict(audit.PINS, pins):
                first = audit.evaluate(path, root, "ASAP", "a" * 40)
                self.assertEqual(first, audit.evaluate(path, root, "ASAP", "a" * 40))
                self.assertEqual(first["track_count"], 40)
                self.assertFalse(first["candidate_promoted"])
                self.assertFalse(first["acceptance_claim"])
                self.assertEqual(first["summary"]["top3_period"], 40)
            trace["tempo_candidates"][0]["lag_bins"] += 1
            paths[0].write_text(json.dumps(trace), encoding="utf-8")
            with patch.dict(audit.PINS, pins):
                with self.assertRaisesRegex(ValueError, "trace-set hash"):
                    audit.evaluate(path, root, "ASAP", "a" * 40)
            pins["ASAP"] = audit._sha256(path), audit._trace_set_sha256(paths)
            with patch.dict(audit.PINS, pins):
                with self.assertRaisesRegex(ValueError, "top-three mismatch"):
                    audit.evaluate(path, root, "ASAP", "a" * 40)


if __name__ == "__main__":
    unittest.main()
