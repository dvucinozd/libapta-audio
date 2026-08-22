#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[2] / "tools/apta_1_1_p4_hardware_evidence.py"
spec = importlib.util.spec_from_file_location("apta_1_1_p4_hw", MODULE_PATH)
assert spec is not None and spec.loader is not None
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)


class P4HardwareEvidenceTests(unittest.TestCase):
    def valid(self) -> dict[str, object]:
        return {
            "schema": hw.SCHEMA,
            "source_revision": "a" * 40,
            "firmware_sha256": "b" * 64,
            "board_model": "ESP32-P4 evaluation board",
            "board_revision": "1.0",
            "test_operator": "operator",
            "test_location": "bench",
            "idf_target": "esp32p4",
            "esp_idf_version": "6.0.2",
            "psram_enabled": True,
            "sample_rate_hz": 48000,
            "duration_seconds": 1800,
            "workspace_bytes": hw.MIN_WORKSPACE_BYTES,
            "result_pool_bytes": hw.MIN_RESULT_POOL_BYTES,
            "overview_columns": 2637,
            "resident_beat_records": 9216,
            "internal_heap_free_before_bytes": 300000,
            "internal_heap_min_free_bytes": 120000,
            "psram_free_before_bytes": 6000000,
            "psram_min_free_bytes": 3000000,
            "process_call_p99_us": 1000,
            "process_call_max_us": 1800,
            "features": sorted(hw.REQUIRED_FEATURES),
            "allocation_failure_count": 0,
            "process_deadline_miss_count": 0,
            "input_drop_count": 0,
            "usb_audio_coexistence_passed": True,
            "test_completed": True,
        }

    def test_accepts_complete_physical_evidence(self) -> None:
        report = hw.evaluate(self.valid(), "a" * 40)
        self.assertEqual(report["status"], "pass")

    def test_rejects_short_run(self) -> None:
        evidence = self.valid()
        evidence["duration_seconds"] = 1799
        self.assertEqual(hw.evaluate(evidence)["status"], "fail")

    def test_rejects_source_revision_mismatch(self) -> None:
        report = hw.evaluate(self.valid(), "c" * 40)
        self.assertEqual(report["status"], "fail")
        self.assertTrue(any("source_revision" in item for item in report["failures"]))

    def test_rejects_failure_counters(self) -> None:
        for name in ("allocation_failure_count", "process_deadline_miss_count", "input_drop_count"):
            evidence = self.valid()
            evidence[name] = 1
            self.assertEqual(hw.evaluate(evidence)["status"], "fail", name)

    def test_rejects_missing_required_feature(self) -> None:
        evidence = self.valid()
        evidence["features"] = [item for item in evidence["features"] if item != "musical_key"]
        self.assertEqual(hw.evaluate(evidence)["status"], "fail")

    def test_rejects_missing_psram_or_usb_coexistence(self) -> None:
        evidence = self.valid()
        evidence["psram_enabled"] = False
        self.assertEqual(hw.evaluate(evidence)["status"], "fail")
        evidence = self.valid()
        evidence["usb_audio_coexistence_passed"] = False
        self.assertEqual(hw.evaluate(evidence)["status"], "fail")

    def test_rejects_capacity_overflow(self) -> None:
        evidence = self.valid()
        evidence["overview_columns"] = hw.MAX_OVERVIEW_COLUMNS + 1
        self.assertEqual(hw.evaluate(evidence)["status"], "fail")
        evidence = self.valid()
        evidence["resident_beat_records"] = hw.MAX_RESIDENT_BEAT_RECORDS + 1
        self.assertEqual(hw.evaluate(evidence)["status"], "fail")


if __name__ == "__main__":
    unittest.main()
