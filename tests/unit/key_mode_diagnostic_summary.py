#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_key_mode_diagnostic_summary as summary


def fixture():
    rows = []
    for kind in ("profile", "triad", "pcm_window", "pcm_cumulative"):
        for c in range(3):
            for tonic in range(12):
                for mode in range(2):
                    for window in (range(1, 5) if kind.startswith("pcm_") else (0,)):
                        rows.append(dict(kind=kind, condition=c, stimulus_tonic=tonic,
                                         stimulus_mode=mode, window=window, selected_tonic=tonic,
                                         selected_mode=mode, confidence=50, chroma=[1.0] * 12,
                                         reference_scores_major_then_minor=[1.0] * 24))
    return dict(format="apta-key-mode-diagnostic-1", acceptance_claim=False,
                checks_passed=True, semitone_band=False, row_count=720,
                mode_encoding="major=0,minor=1", rows=rows)


class SummaryTests(unittest.TestCase):
    def check_load(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            path.write_text(json.dumps(data), encoding="utf-8")
            return summary.load_report(path, False)

    def test_balanced_complete_summary(self):
        rows, digest = self.check_load(fixture())
        groups = summary.aggregate(rows)
        self.assertEqual(len(digest), 64)
        self.assertEqual(len(groups), 60)
        self.assertTrue(all(g["count"] == 12 and g["matches_stimulus_tonic_and_mode"] == 12
                            and g["mean_chroma_min_over_mean"] == 1 for g in groups))

    def test_duplicate_rejected(self):
        data = fixture()
        data["rows"][1] = copy.deepcopy(data["rows"][0])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.check_load(data)

    def test_nonfinite_evidence_rejected(self):
        data = fixture()
        data["rows"][0]["chroma"][0] = float("nan")
        with self.assertRaisesRegex(ValueError, "vector"):
            self.check_load(data)

    def test_mismatched_build_rejected(self):
        data = fixture()
        data["semitone_band"] = True
        with self.assertRaisesRegex(ValueError, "unexpected"):
            self.check_load(data)

    def test_incomplete_rejected(self):
        data = fixture()
        data["rows"].pop()
        with self.assertRaisesRegex(ValueError, "incomplete"):
            self.check_load(data)

    def test_inconsistent_reference_rejected(self):
        data = fixture()
        data["rows"][0]["reference_scores_major_then_minor"][0] = 0.0
        with self.assertRaisesRegex(ValueError, "mismatch"):
            self.check_load(data)


if __name__ == "__main__":
    unittest.main()
