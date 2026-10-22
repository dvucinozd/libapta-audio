#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import array
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_native_dense_development as module
history = module.history


class DenseCaptureTests(unittest.TestCase):
    def fixture(self):
        values = [0.0] * 1024
        values[100] = 0.125
        return {"sample_rate": 48000, "windows": [{"first_frame": 0, "end_frame": 262144,
            "dense_trace": {"format": "libapta-dense-novelty-1", "complete": True, "count": 1024,
                "first_bin": 0, "bin_frames": 256, "last_bin_frames": 256, "startup_bins": 15, "values": values}}]}

    def test_dense_geometry_startup_and_subthreshold_values(self):
        value = self.fixture()
        history.validate_dense_trace(value)
        partial = {"first_frame": 262144, "end_frame": 262161,
            "dense_trace": {"format": "libapta-dense-novelty-1", "complete": True, "count": 1,
                "first_bin": 1024, "bin_frames": 256, "last_bin_frames": 17, "startup_bins": 0, "values": [0.03125]}}
        value["windows"].append(partial)
        history.validate_dense_trace(value)

    def test_dense_refuses_loss_bad_values_and_forged_geometry(self):
        value = self.fixture()
        for field, bad in [("complete", False), ("count", 1023), ("count", True), ("first_bin", 1),
                           ("bin_frames", 512), ("last_bin_frames", 17), ("startup_bins", 0), ("values", [])]:
            changed = copy.deepcopy(value)
            changed["windows"][0]["dense_trace"][field] = bad
            with self.assertRaises(ValueError): history.validate_dense_trace(changed)
        for bad in [float("nan"), float("inf"), -0.001, 1.001, True, "0.1", 0.1]:
            changed = copy.deepcopy(value)
            changed["windows"][0]["dense_trace"]["values"][100] = bad
            with self.assertRaises(ValueError): history.validate_dense_trace(changed)
        value["windows"][0]["dense_trace"]["values"][0] = 0.1
        with self.assertRaises(ValueError): history.validate_dense_trace(value)

    def test_baseline_hash_and_holdout_refused_before_native_invocation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = root / "baseline.json"
            baseline.write_text("{}")
            with mock.patch.object(history, "run") as native:
                with self.assertRaisesRegex(ValueError, "baseline hash"):
                    module.capture(root, root, baseline, "0" * 64, root / "output", "1" * 40, "a" * 64)
                (root / "manifest.json").write_text(json.dumps({"format": module.annotated.canonical.FORMAT,
                    "prepared_splits": ["holdout"], "holdout_performances_opened": True}))
                with self.assertRaisesRegex(ValueError, "development-only"):
                    module.capture(root, root, baseline, history.sha256_file(baseline), root / "output", "1" * 40, "a" * 64)
                native.assert_not_called()
                self.assertFalse((root / "output").exists())

    @unittest.skipUnless(os.environ.get("APTA_TEMPORAL_TRACE_DIAGNOSTIC"), "requires dense native diagnostic")
    def test_actual_native_dense_trace_partial_source_identity_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            audio = root / "audio"
            audio.mkdir()
            source = audio / "private-fixture.wav"
            samples = array.array("h")
            for frame in range(262144 + 17):
                amplitude = 1966 if frame % 4096 < 512 else 1638
                sample = amplitude * (0, 1, 2, 1, 0, -1, -2, -1)[frame % 8] // 2
                samples.extend((sample, sample))
            if sys.byteorder != "little": samples.byteswap()
            with wave.open(str(source), "wb") as target:
                target.setnchannels(2); target.setsampwidth(2); target.setframerate(48000)
                target.writeframes(samples.tobytes())
            manifest_path = root / "input.json"
            manifest = history.freeze(audio, manifest_path, 1, "a" * 64, 30)
            analyzer = Path(os.environ["APTA_TEMPORAL_TRACE_DIAGNOSTIC"])
            report = history.run(manifest_path, audio, analyzer, root / "native", "1" * 40, "novelty", trace=True, dense=True)
            row = manifest["tracks"][0]
            dense_path = root / "native" / f"{row['track']}-novelty-dense.json"
            value = json.loads(dense_path.read_text())
            history.validate_output(value, row, "novelty")
            history.validate_dense_trace(value)
            self.assertEqual(value["windows"][0]["dense_trace"]["count"], 1024)
            self.assertEqual(value["windows"][1]["dense_trace"]["last_bin_frames"], 17)
            self.assertTrue(any(0 < v <= 0.20 for v in value["windows"][0]["dense_trace"]["values"]))
            self.assertNotIn(source.name, dense_path.read_text())
            self.assertTrue(report["dense_traces_requested"])
            self.assertIsNone(report["accuracy"])
            before = dense_path.read_bytes()
            command = [str(analyzer), str(source), str(dense_path), "--mode=novelty-dense", "--frames=1440000"]
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(dense_path.read_bytes(), before)

    @unittest.skipUnless(os.environ.get("APTA_DENSE_UNSUPPORTED_DIAGNOSTIC"), "requires build without dense feature")
    def test_build_without_feature_rejects_dense_mode_before_opening_input(self):
        result = subprocess.run([os.environ["APTA_DENSE_UNSUPPORTED_DIAGNOSTIC"], "missing.wav", "unused.json",
                                 "--mode=novelty-dense", "--frames=1"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires temporal-trace-diagnostics", result.stderr)


if __name__ == "__main__": unittest.main()
