#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import copy
import json
import os
from pathlib import Path
import runpy
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_native_event_lattice as oracle
import apta_1_1_native_history_development as history


class EventLattice(unittest.TestCase):
    def test_periodic_weak_extra_and_missing_events(self):
        events = [[8192 + n * 24576, 1.0] for n in range(11)]
        for evidence in [events, events[:5] + events[6:],
                         sorted(events + [[8192 + n * 24576 + 6144, 0.1] for n in range(10)])]:
            result = oracle.select(evidence, 0, 262144, 48000)
            self.assertEqual(result["kind"], "accepted")
            self.assertEqual(result["tempo_millibpm"], 117188)
            self.assertEqual(result["confidence"], 49)
            self.assertEqual(result, oracle.select(evidence, 0, 262144, 48000))
        shifted = [[frame + 262144, weight] for frame, weight in events]
        result = oracle.select(shifted, 262144, 524288, 48000)
        self.assertEqual(result["anchor_frame"], 8192 + 262144)

    def test_silence_short_evidence_and_distinct_period_ambiguity(self):
        self.assertEqual(oracle.select([], 0, 262144, 48000)["kind"], "insufficient_events")
        self.assertNotEqual(oracle.select([[8192, 1.0]], 0, 262144, 48000)["kind"], "accepted")
        events = [[8192 + n * 24576, 1.0] for n in range(11)]
        other = [[16384 + n * 25344, 1.0] for n in range(10)]
        self.assertEqual(oracle.select(sorted(events + other), 0, 262144, 48000)["kind"], "ambiguous")

    def test_frozen_best_phase_rule_does_not_guard_same_period_ambiguity(self):
        events = [[8192 + n * 24576, 1.0] for n in range(11)]
        other = [[16384 + n * 24576, 1.0] for n in range(10)]
        result = oracle.select(sorted(events + other), 0, 262144, 48000)
        # Known design limitation: only the best phase survives per period.
        # Retain it as evidence; do not change the frozen oracle to rescue it.
        self.assertEqual(result["kind"], "accepted")
        self.assertEqual(result["anchor_frame"], 8192)

    def test_all_beat_phase_is_separate_from_period_and_bar_phase(self):
        truth = {"beat_period_frames": 24000.0, "downbeat_frame": 0, "meter_numerator": 4, "meter_denominator": 4}
        window = {"first_frame": 0, "end_frame": 262144,
                  "selected": {"kind": "accepted", "anchor_frame": 24000, "tempo_millibpm": 120000, "confidence": 49},
                  "meter": {"kind": "estimated", "numerator": 4, "denominator": 4,
                            "downbeat_frame": 24000, "downbeat_fraction_q32": 0}}
        result = oracle.metrics(window, truth, 48000)
        self.assertTrue(result["joint_correct"])
        self.assertTrue(result["meter_correct"])
        self.assertFalse(result["downbeat_correct"])
        changed = copy.deepcopy(window)
        changed["selected"].update(anchor_frame=2000, tempo_millibpm=119000)
        result = oracle.metrics(changed, truth, 48000)
        self.assertTrue(result["period_correct"])
        self.assertFalse(result["beat_phase_correct"])
        changed["selected"]["kind"] = "rejected"
        result = oracle.metrics(changed, truth, 48000)
        self.assertFalse(result["joint_correct"])
        self.assertFalse(result["meter_emitted"])

    @unittest.skipUnless(os.environ.get("APTA_ONSET_TRACE_DIAGNOSTIC"), "requires actual native trace fixture")
    def test_real_trace_hash_labels_cohort_and_overwrite_guards(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            helper = runpy.run_path(str(Path(__file__).with_name("apta_1_1_native_history_development.py")))["DevelopmentProtocol"]()
            helper.wav = root / "generated.wav"
            helper.write_carrier_audio()
            input_manifest = root / "input.json"
            history.freeze(root, input_manifest, 1, "a" * 64, 6)
            fixture = root / "paired"
            history.run(input_manifest, root, Path(os.environ["APTA_ONSET_TRACE_DIAGNOSTIC"]), fixture, "1" * 40, "novelty", True)
            coverage = fixture / "report.json"
            rows = json.loads(coverage.read_text())["outputs"]
            track = rows[0]["track"]
            labels = root / "labels.csv"
            labels.write_text("track,key_tonic,key_mode,meter_numerator,meter_denominator,downbeat_frame,beat_period_frames\n" + f"{track},0,major,4,4,8192,24576\n")
            label_hash = history.sha256_file(labels)
            manifest = root / "manifest.json"
            value = {"format": oracle.canonical.FORMAT, "track_count": 1, "track_ids": [track],
                     "labels_sha256": label_hash, "frozen_utc": "2026-10-22T00:00:00Z",
                     "reference_source": "generated independent timing", "verification_procedure": "generated fixture only"}
            manifest.write_text(json.dumps(value))
            coverage_hash = history.sha256_file(coverage)
            target = root / "result.json"
            result = oracle.evaluate(coverage, coverage_hash, labels, manifest, label_hash, target)
            self.assertEqual(result["totals"]["event_oracle"]["joint_correct"], 1)
            self.assertFalse(result["formal_acceptance"])
            before = target.read_bytes()
            with self.assertRaises(FileExistsError):
                oracle.evaluate(coverage, coverage_hash, labels, manifest, label_hash, target)
            self.assertEqual(target.read_bytes(), before)
            for bad_hash, bad_labels in [("0" * 64, label_hash), (coverage_hash, "0" * 64)]:
                with self.assertRaises(ValueError):
                    oracle.evaluate(coverage, bad_hash, labels, manifest, bad_labels, root / "bad.json")
            value["track_ids"] = ["track-" + "0" * 24]
            manifest.write_text(json.dumps(value))
            with self.assertRaises(ValueError):
                oracle.evaluate(coverage, coverage_hash, labels, manifest, label_hash, root / "cohort.json")


if __name__ == "__main__":
    unittest.main()
