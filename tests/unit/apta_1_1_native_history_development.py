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
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_native_history_development as probe


class DevelopmentProtocol(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.audio = self.root / "audio"
        self.audio.mkdir()
        self.wav = self.audio / "private-input-name.wav"
        self.manifest = self.root / "manifest.json"
        self.write_audio(1024)

    def write_audio(self, count):
        pcm = array.array("h")
        for frame in range(count):
            value = (28672 if frame // 16384 % 4 == 1 else 8192) if frame % 16384 < 64 else 0
            pcm.extend((value, value))
        if sys.byteorder != "little":
            pcm.byteswap()
        with wave.open(str(self.wav), "wb") as target:
            target.setnchannels(2)
            target.setsampwidth(2)
            target.setframerate(48000)
            target.writeframes(pcm.tobytes())

    def freeze(self):
        return probe.freeze(self.audio, self.manifest, 1, "a" * 64, 6)

    def test_freeze_binds_full_bytes_geometry_and_refuses_overwrite(self):
        frozen = self.freeze()
        loaded = probe.load_manifest(self.manifest)
        self.assertEqual(loaded, frozen)
        row = loaded["tracks"][0]
        self.assertEqual(row["sha256"], probe.sha256_file(self.wav))
        self.assertEqual(row["source_total_frames"], 1024)
        self.assertEqual(row["requested_frames"], 288000)
        before = self.manifest.read_bytes()
        with self.assertRaises(FileExistsError):
            self.freeze()
        self.assertEqual(self.manifest.read_bytes(), before)

    def test_count_role_path_and_identity_guards(self):
        with self.assertRaises(ValueError):
            probe.freeze(self.audio, self.manifest, 2, "a" * 64, 6)
        value = self.freeze()
        for field, bad in [("source_role", "holdout"), ("source_role", "fresh")]:
            changed = copy.deepcopy(value)
            changed[field] = bad
            self.manifest.write_text(json.dumps(changed))
            with self.assertRaises(ValueError):
                probe.load_manifest(self.manifest)
        for field, bad in [("file", "../unopened.wav"), ("sha256", "b" * 64), ("requested_frames", 1440001)]:
            changed = copy.deepcopy(value)
            changed["tracks"][0][field] = bad
            self.manifest.write_text(json.dumps(changed))
            with self.assertRaises(ValueError):
                probe.load_manifest(self.manifest)

    def test_output_rejects_identity_scope_loss_and_promotion(self):
        row = self.freeze()["tracks"][0]
        value = {"format": "libapta-history-diagnostic-1", "mode": "coarse", "input_sha256": row["sha256"],
                 "sample_rate": 48000, "channel_count": 2, "source_total_frames": 1024,
                 "requested_frames": 288000, "analyzed_first_frame": 0, "analyzed_frames": 1024,
                 "finished": True, "capacity_exhausted": False, "provisional": True,
                 "windows": [{"first_frame": 0, "end_frame": 1024, "baseline": {"kind": "too_short"},
                              "selected": {"kind": "too_short"}, "selection_reason": "not_enabled", "meter": {"kind": "grid_unsupported"}}]}
        probe.validate_output(value, row, "coarse")
        for field, bad in [("input_sha256", "b" * 64), ("analyzed_frames", 1000), ("finished", False), ("capacity_exhausted", True), ("provisional", False)]:
            changed = copy.deepcopy(value)
            changed[field] = bad
            with self.assertRaises(ValueError):
                probe.validate_output(changed, row, "coarse")
        changed = copy.deepcopy(value)
        changed["windows"][0]["selected"] = {"kind": "unavailable"}
        with self.assertRaises(ValueError):
            probe.validate_output(changed, row, "coarse")

    @unittest.skipUnless(os.environ.get("APTA_HISTORY_DIAGNOSTIC"), "requires built native diagnostic binary")
    def test_actual_native_prefix_identity_paired_baseline_and_no_overwrite(self):
        self.write_audio(288000)
        frozen = self.freeze()
        analyzer = Path(os.environ["APTA_HISTORY_DIAGNOSTIC"])
        report = probe.run(self.manifest, self.audio, analyzer, self.root / "paired", "1" * 40)
        self.assertEqual(report["totals"]["attack"]["supported_windows"], 1)
        self.assertEqual(report["totals"]["attack"]["meter_windows"], 1)
        self.assertIsNone(report["accuracy"])
        self.assertIsNone(report["false_lattice_rate"])
        self.assertNotIn(self.wav.name, json.dumps(report))
        target = self.root / "prefix.json"
        command = [str(analyzer), str(self.wav), str(target), "--mode=attack", "--frames=262144"]
        subprocess.run(command, check=True, capture_output=True)
        value = json.loads(target.read_text())
        row = dict(frozen["tracks"][0], requested_frames=262144)
        probe.validate_output(value, row, "attack")
        self.assertEqual(value["source_total_frames"], 288000)
        self.assertEqual(value["analyzed_frames"], 262144)
        self.assertEqual(value["windows"][0]["meter"]["numerator"], 4)
        self.assertEqual(value["windows"][0]["meter"]["downbeat_frame"], 16384)
        before = target.read_bytes()
        self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertEqual(target.read_bytes(), before)

    @unittest.skipUnless(os.environ.get("APTA_HISTORY_DIAGNOSTIC"), "requires built native diagnostic binary")
    def test_changed_source_cannot_enter_actual_run(self):
        self.freeze()
        with self.wav.open("ab") as target:
            target.write(b"changed")
        with self.assertRaisesRegex(ValueError, "frozen development source changed"):
            probe.run(self.manifest, self.audio, Path(os.environ["APTA_HISTORY_DIAGNOSTIC"]), self.root / "rejected", "1" * 40)
        self.assertFalse((self.root / "rejected" / "report.json").exists())

    @unittest.skipUnless(os.environ.get("APTA_ONSET_DIAGNOSTIC"), "requires onset diagnostic build")
    def test_novelty_actual_pcm_separates_carrier_and_preserves_old_modes(self):
        self.write_carrier_audio()
        frozen = self.freeze()
        report = probe.run(self.manifest, self.audio, Path(os.environ["APTA_ONSET_DIAGNOSTIC"]),
                           self.root / "novelty", "1" * 40, "novelty")
        self.assertEqual(report["candidate"], "novelty")
        self.assertEqual(report["totals"]["attack"]["supported_windows"], 0)
        self.assertEqual(report["totals"]["novelty"]["supported_windows"], 1)
        source = self.root / "novelty" / (frozen["tracks"][0]["track"] + "-novelty.json")
        window = json.loads(source.read_text())["windows"][0]
        self.assertEqual(window["selected"]["anchor_frame"], 8192)
        self.assertEqual(window["selected"]["tempo_millibpm"], 117188)
        self.assertEqual(window["selected"]["confidence"], 49)
        self.assertIsNone(report["accuracy"])
        changed = json.loads(source.read_text())
        changed["windows"][0]["selected"]["confidence"] = 50
        with self.assertRaises(ValueError):
            probe.validate_output(changed, frozen["tracks"][0], "novelty")

    def write_carrier_audio(self):
        pcm = array.array("h")
        for frame in range(262144):
            amplitude = 16384 if frame >= 8192 and (frame - 8192) % 24576 < 512 else 1638
            value = amplitude * (0, 1, 2, 1, 0, -1, -2, -1)[frame % 8] // 2
            pcm.extend((value, value))
        if sys.byteorder != "little":
            pcm.byteswap()
        with wave.open(str(self.wav), "wb") as target:
            target.setnchannels(2)
            target.setsampwidth(2)
            target.setframerate(48000)
            target.writeframes(pcm.tobytes())

    @unittest.skipUnless(os.environ.get("APTA_ONSET_TRACE_DIAGNOSTIC"), "requires native onset trace build")
    def test_actual_trace_retains_source_events_and_rejects_corruption(self):
        self.write_carrier_audio()
        frozen = self.freeze()
        analyzer = Path(os.environ["APTA_ONSET_TRACE_DIAGNOSTIC"])
        report = probe.run(self.manifest, self.audio, analyzer, self.root / "trace", "1" * 40, "novelty", True)
        target = self.root / "trace" / (frozen["tracks"][0]["track"] + "-novelty-trace.json")
        value = json.loads(target.read_text())
        events = value["windows"][0]["onset_trace"]["events"]
        self.assertTrue(report["traces_requested"])
        self.assertEqual([event[0] for event in events], [8192 + index * 24576 for index in range(11)])
        for bad in [float("nan"), 0.20, 1.01]:
            changed = copy.deepcopy(value)
            changed["windows"][0]["onset_trace"]["events"][0][1] = bad
            with self.assertRaises(ValueError):
                probe.validate_onset_trace(changed)
        for field, bad in [("complete", False), ("events", events + [events[0]])]:
            changed = copy.deepcopy(value)
            changed["windows"][0]["onset_trace"][field] = bad
            with self.assertRaises(ValueError):
                probe.validate_onset_trace(changed)


if __name__ == "__main__":
    unittest.main()
