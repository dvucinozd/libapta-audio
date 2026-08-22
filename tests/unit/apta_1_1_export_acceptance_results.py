#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[2] / "tools/apta_1_1_export_acceptance_results.py"
spec = importlib.util.spec_from_file_location("apta_1_1_export", MODULE_PATH)
assert spec is not None and spec.loader is not None
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


class AcceptanceResultExportTests(unittest.TestCase):
    def inspection(self) -> dict[str, object]:
        return {
            "MKEY": {
                "tonic": 9,
                "mode": 2,
                "confidence": 82,
            },
            "MTRD": {
                "numerator": 4,
                "denominator": 4,
                "downbeat_frame": 12000,
                "confidence": 91,
            },
            "GGRD": {
                "period_whole_frames": 24000,
                "period_fraction_q32": 2147483648,
                "confidence": 88,
            },
        }

    def test_parses_final_dj_sections(self) -> None:
        row = exporter.parse_inspection("track-abc", self.inspection())
        self.assertEqual(row["track"], "track-abc")
        self.assertEqual(row["key_tonic"], 9)
        self.assertEqual(row["key_mode"], "minor")
        self.assertEqual(row["key_confidence"], 82)
        self.assertEqual(row["meter_numerator"], 4)
        self.assertEqual(row["downbeat_confidence"], 91)
        self.assertEqual(row["beat_period_frames"], "24000.5")
        self.assertEqual(row["grid_confidence"], 88)

    def test_falls_back_to_local_grid(self) -> None:
        value = self.inspection()
        value["LGRD"] = value.pop("GGRD")
        row = exporter.parse_inspection("track-local", value)
        self.assertEqual(row["beat_period_frames"], "24000.5")

    def test_rejects_missing_key_meter_or_grid(self) -> None:
        for section in ("MKEY", "MTRD", "GGRD"):
            value = self.inspection()
            value.pop(section)
            with self.assertRaises(exporter.ExportError):
                exporter.parse_inspection("track-x", value)

    def test_rejects_unknown_key_mode(self) -> None:
        value = self.inspection()
        value["MKEY"]["mode"] = 0
        with self.assertRaises(exporter.ExportError):
            exporter.parse_inspection("track-x", value)

    def test_rejects_invalid_meter(self) -> None:
        value = self.inspection()
        value["MTRD"]["numerator"] = 7
        with self.assertRaises(exporter.ExportError):
            exporter.parse_inspection("track-x", value)

    def test_rejects_invalid_confidence(self) -> None:
        value = self.inspection()
        value["GGRD"]["confidence"] = 255
        with self.assertRaises(exporter.ExportError):
            exporter.parse_inspection("track-x", value)


if __name__ == "__main__":
    unittest.main()
