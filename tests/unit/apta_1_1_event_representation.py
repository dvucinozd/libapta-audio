#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_event_representation as module


class RepresentationTests(unittest.TestCase):
    def fixture(self):
        return {"beat_frames": list(range(0, 360001, 24000)),
                "downbeat_frames": [0, 96000, 192000, 288000],
                "meter_numerator": 4, "meter_denominator": 4}

    def test_control_matches_unchanged_scorer(self):
        truth = self.fixture()
        control = module.ref.project(truth["beat_frames"], truth["downbeat_frames"], 0, 262144)
        score = module.score_points(module.points_for_control(control), truth, 0, 262144)
        old = module.ref.annotated.score_window(control, truth, module.ref.RATE)
        for field in ("period", "phase", "joint"):
            self.assertEqual(score[field + "_windows"], old["confirmed_" + field + "_windows"])
        self.assertEqual(score["complete_sequence_windows"], 1)

    def test_source_offsets_and_global_meter_ordinals_preserved(self):
        truth = self.fixture()
        shift = 524288
        shifted = dict(truth, beat_frames=[p + shift for p in truth["beat_frames"]], downbeat_frames=[p + shift for p in truth["downbeat_frames"]])
        old = module.score_points(module.points_for_events(truth["beat_frames"], 0, 262144), truth, 0, 262144)
        new = module.score_points(module.points_for_events(shifted["beat_frames"], shift, shift + 262144), shifted, shift, shift + 262144)
        self.assertEqual(new, old)
        beats = list(range(0, 960001, 24000))
        meter = module.meter_on_events(beats, beats[::4], 300000, 600000)
        self.assertEqual(meter["downbeat_ordinal"], beats.index(meter["downbeat_frame"]))
        self.assertGreater(meter["downbeat_ordinal"], 0)

    def test_lossless_segment_boundaries_and_capacity_are_not_truncated(self):
        events = [100, 24100, 48100, 73100, 98100]
        segments = module.exact_segments(events)
        self.assertEqual([s["beat_count"] for s in segments], [2, 3])
        self.assertEqual(module.expand_segments(segments), events)
        events = [0]
        for i in range(12):
            events.append(events[-1] + 24000 + (i % 2) * 960)
        segments = module.exact_segments(events)
        self.assertGreater(len(segments), 8)
        self.assertEqual(module.expand_segments(segments), events)

    def test_invalid_event_sequences_fail(self):
        for values in ([0, 10, 10], [0, 20, 10], [-1, 10], [0, float("nan")]):
            with self.assertRaises(ValueError):
                module.exact_segments(values)

    def test_quantized_correct_phase_does_not_imply_correct_intervals(self):
        truth = dict(self.fixture(), beat_frames=list(range(0, 375001, 25000)))
        beats = [module.ref.rounded((1000 + i * 25000) / 960) * 960 for i in range(15)]
        raw = module.score_points(module.points_for_events(beats, 0, 262144), truth, 0, 262144)
        control = module.ref.project(beats, [], 0, 262144)
        fitted = module.score_points(module.points_for_control(control), truth, 0, 262144)
        self.assertEqual(raw["phase_windows"], 1)
        self.assertEqual(raw["joint_windows"], 0)
        self.assertEqual(fitted["joint_windows"], 1)

    def test_half_double_and_missing_beat_periods_do_not_pass(self):
        truth = self.fixture()
        original = truth["beat_frames"]
        for beats in (original[::2], list(range(0, 360001, 12000)), [p for p in original if p != 96000]):
            score = module.score_points(module.points_for_events(beats, 0, 262144), truth, 0, 262144)
            self.assertEqual(score["joint_windows"], 0)
            self.assertEqual(score["complete_sequence_windows"], 0)

    def test_coverage_diagnostic_catches_missing_prefix_despite_correct_points(self):
        truth = self.fixture()
        beats = truth["beat_frames"][3:]
        score = module.score_points(module.points_for_events(beats, 0, 262144), truth, 0, 262144)
        self.assertEqual(score["joint_windows"], 1)
        self.assertEqual(score["complete_sequence_windows"], 0)

    def test_unknown_successor_and_incomplete_reference_are_not_success(self):
        truth = self.fixture()
        beats = truth["beat_frames"][:6]
        score = module.score_points(module.points_for_events(beats, 0, 262144), truth, 0, 262144)
        self.assertEqual(score["unknown_successor_points"], 1)
        self.assertEqual(score["joint_windows"], 0)
        missing = dict(truth, beat_frames=truth["beat_frames"][1:])
        score = module.score_points(module.points_for_events(truth["beat_frames"], 0, 262144), missing, 0, 262144)
        self.assertEqual(score["reference_complete_windows"], 0)
        self.assertEqual(score["joint_windows"], 0)

    def test_large_event_gap_preserves_positions_without_inventing_beats(self):
        events = [0, 24000, 48000, 240000, 264000]
        segments = module.exact_segments(events)
        self.assertEqual(module.expand_segments(segments), events)
        self.assertTrue(any(s["tempo_millibpm"] < 40000 for s in segments))

    def test_registered_hash_refuses_changes(self):
        self.assertFalse(module.protocol()["formal_acceptance"])
        with mock.patch.object(module.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                module.protocol()


if __name__ == "__main__":
    unittest.main()
