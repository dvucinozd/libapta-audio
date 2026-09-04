#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_key_extraction_reference_summary as summary
from key_mode_diagnostic_summary import fixture


def rows():
    result = fixture()["rows"]
    for row in result:
        if row["kind"].startswith("pcm_"):
            ref = {key: copy.deepcopy(row[key]) for key in ("chroma", "selected_tonic", "selected_mode", "confidence")}
            ref.update(max_chroma_error_over_max_reference=0.0, max_fourier_goertzel_energy_error=1e-11)
            row["extraction_reference"] = {"effective": ref, "nominal": copy.deepcopy(ref)}
    return result


class ReferenceSummaryTests(unittest.TestCase):
    def test_complete_zero_difference(self):
        result = summary.summarize_rows(rows())
        for name in ("effective", "nominal"):
            self.assertEqual(result[name]["compared_pcm_rows"], 576)
            self.assertEqual(result[name]["changed_tonic_or_mode"], 0)
            self.assertEqual(result[name]["max_chroma_error_over_max_reference"], 0)

    def test_changed_verdict_counted(self):
        data = rows()
        row = next(r for r in data if r["kind"].startswith("pcm_"))
        row["extraction_reference"]["nominal"]["selected_mode"] = 1 - row["selected_mode"]
        self.assertEqual(summary.summarize_rows(data)["nominal"]["changed_tonic_or_mode"], 1)

    def test_invalid_reference(self):
        for field, value in (("chroma", [float("nan")] * 12),
                             ("max_fourier_goertzel_energy_error", 1e-4),
                             ("max_chroma_error_over_max_reference", 0.1),
                             ("selected_tonic", 12)):
            data = rows()
            row = next(r for r in data if r["kind"].startswith("pcm_"))
            row["extraction_reference"]["effective"][field] = value
            with self.assertRaises(ValueError):
                summary.summarize_rows(data)

    def test_missing_pcm_rejected(self):
        with self.assertRaises(ValueError):
            summary.summarize_rows(rows()[:-1])


if __name__ == "__main__":
    unittest.main()
