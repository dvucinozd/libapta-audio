#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from copy import deepcopy
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_multibeat_timing as module


class MultiBeatTimingTests(unittest.TestCase):
    def fixture(self, period=24000):
        return list(range(0, 960000, period))

    def test_constant_positive_control_preserves_positions_and_ordinals(self):
        beats = self.fixture()
        result = module.project(beats, beats[::4], 262144, 524288, 960000)
        self.assertEqual(result["selected"]["kind"], "accepted")
        self.assertEqual(result["selected"]["tempo_millibpm"], 120000)
        self.assertEqual(result["selected"]["anchor_frame"], 264000)
        self.assertEqual(result["support"]["anchor_ordinal"], 11)
        self.assertEqual([p // module.Q for p, _ in module.representation.points_for_control(result)],
                         [p for p in beats if 262144 <= p < 524288])

    def test_quantized_period_is_reconstructed_across_multiple_beats(self):
        beats = [module.ref.rounded((1000 + i * 25000) / 960) * 960 for i in range(39)]
        result = module.project(beats, [], 262144, 524288, 1000000)
        self.assertEqual(result["selected"]["kind"], "accepted")
        period = module.representation.points_for_control(result)[0][1] / module.Q
        self.assertLess(abs(period - 25000), 125)
        self.assertGreater(max(abs(b - a - 25000) for a, b in zip(beats, beats[1:])), 250)

    def test_exact_source_shift_preserves_local_timing_and_meter_ordinal(self):
        beats = self.fixture()
        shift = 123456
        original = module.project(beats, beats[::4], 0, 262144, 960000)
        shifted = module.project([p + shift for p in beats], [p + shift for p in beats[::4]],
                                 shift, shift + 262144, shift + 960000)
        self.assertEqual(original["support"], shifted["support"])
        self.assertEqual(shifted["selected"]["anchor_frame"], original["selected"]["anchor_frame"] + shift)
        self.assertEqual(shifted["meter"]["downbeat_frame"], original["meter"]["downbeat_frame"] + shift)

    def test_source_edges_clip_context_without_extrapolated_events(self):
        beats = self.fixture()
        windows = [module.project(beats, [], a, b, 960000)
                   for a, b in ((0, 262144), (262144, 524288), (524288, 786432), (786432, 960000))]
        partition = module.check_partition(windows, 960000)
        self.assertTrue(partition["full_source_support"])
        self.assertEqual(partition["accepted_segments"], 4)
        self.assertEqual(windows[0]["support"]["first_ordinal"], 0)
        self.assertEqual(windows[-1]["support"]["first_ordinal"] + windows[-1]["support"]["events"], len(beats))

    def test_missing_or_extra_musical_beat_abstains_without_pcm_gap(self):
        original = self.fixture()
        for beats in ([p for p in original if p != 384000], sorted(original + [396000])):
            result = module.project(beats, [], 262144, 524288, 960000)
            self.assertEqual(result["selected"]["kind"], "inconsistent_events")
            self.assertEqual(result["meter"], {})
            self.assertNotIn("known_pcm_gap", str(result))

    def test_context_across_known_pcm_gap_is_rejected(self):
        beats = self.fixture()
        # The first gap is outside the requested window but within its support.
        for gaps in (((250000, 251000),), ((300000, 301000),)):
            result = module.project(beats, [], 262144, 524288, 960000, gaps)
            self.assertEqual(result["selected"]["kind"], "known_pcm_gap")
        result = module.project(beats, [], 262144, 524288, 960000, ((800000, 810000),))
        self.assertEqual(result["selected"]["kind"], "accepted")

    def test_metrical_ambiguity_is_never_repaired_using_truth(self):
        truth = {"beat_frames": list(range(0, 1000001, 24000)), "downbeat_frames": [0, 96000],
                 "meter_numerator": 4, "meter_denominator": 4}
        for period in (12000, 48000):
            beats = self.fixture(period)
            result = module.project(beats, [], 262144, 524288, 960000)
            self.assertEqual(result["selected"]["kind"], "accepted")
            score = module.ref.annotated.score_window(result, truth, 48000)
            self.assertEqual(score["confirmed_period_windows"], 0)
            self.assertEqual(result["selected"]["tempo_millibpm"], 48000 * 60000 // period)

    def test_meter_uses_exact_original_ordinal_on_its_reconstructed_grid(self):
        beats = self.fixture()
        result = module.project(beats, beats[::4], 0, 400000, 960000)
        self.assertEqual(result["meter"]["kind"], "estimated")
        ordinal = result["support"]["downbeat_ordinal"]
        point = module.representation.points_for_control(result)[ordinal - result["support"]["anchor_ordinal"]][0]
        self.assertEqual(point, result["meter"]["downbeat_frame"] * module.Q + result["meter"]["downbeat_fraction_q32"])
        unsupported = module.project(beats, beats[::4], 0, 100000, 960000)
        self.assertEqual(unsupported["meter"], {})

    def test_short_or_empty_context_is_not_fallback_success(self):
        for beats in ([], [10000], [10000 + i * 24000 for i in range(8)]):
            result = module.project(beats, [], 0, 262144, 960000)
            self.assertEqual(result["selected"]["kind"], "insufficient_events")
            self.assertEqual(module.representation.points_for_control(result), [])

    def test_fit_cannot_create_a_beat_across_the_window_end(self):
        beats = [1000 + i * 24000 for i in range(9)]
        beats[-1] -= 960
        result = module.project(beats, [], 0, 121000, 262144)
        self.assertEqual(result["selected"]["kind"], "event_count_changed")
        self.assertEqual(result["support"]["in_window_events"], 5)
        self.assertEqual(module.representation.points_for_control(result), [])

    def test_fit_anchor_cannot_move_before_the_source(self):
        beats = [i * 24000 for i in range(9)]
        beats[-1] += 960
        result = module.project(beats, [], 0, 120000, 262144)
        self.assertEqual(result["selected"]["kind"], "anchor_outside_window")

    def test_any_single_regression_vetoes_an_aggregate_gain(self):
        totals = {"candidate": {"confirmed_joint_windows": 200},
                  "common_candidate": {"complete_sequence_windows": 190}}
        deltas = {"ols": {key + "_breaks": 0 for key in ("period", "phase", "joint", "meter", "downbeat")},
                  "coarse": {"joint_breaks": 0}}
        self.assertTrue(all(module.gates(totals, deltas).values()))
        deltas["ols"]["phase_breaks"] = 1
        self.assertFalse(all(module.gates(totals, deltas).values()))

    def test_invalid_source_events_gaps_and_caps_fail(self):
        for beats in ([0, 10, 10], [-1, 10], [0, 960000], [0, float("nan")], list(range(3073))):
            with self.assertRaises(ValueError):
                module.project(beats, [], 0, 262144, 960000)
        for gaps in (((20, 10),), ((10, 30), (20, 40)), ((0, 960001),)):
            with self.assertRaises(ValueError):
                module.project(self.fixture(), [], 0, 262144, 960000, gaps)
        with self.assertRaises(ValueError):
            module.project([], [], 0, 262144, 1440001)
        with self.assertRaises(ValueError):
            module.project(self.fixture(), [10], 0, 262144, 960000)

    def test_unsupported_tempo_is_not_clamped(self):
        for period in (8000, 100000):
            beats = list(range(0, 1400000, period))
            result = module.project(beats, [], 262144, 1048576, 1400000)
            self.assertEqual(result["selected"]["kind"], "unsupported_tempo")

    def test_partition_does_not_bridge_analysis_abstention(self):
        beats = self.fixture()
        windows = [module.project(beats, [], a, b, 960000)
                   for a, b in ((0, 262144), (262144, 524288), (524288, 960000))]
        windows[1]["selected"] = {"kind": "insufficient_events"}
        result = module.check_partition(windows, 960000)
        self.assertEqual(result["unknown_support_ranges"], [[262144, 524288]])
        self.assertEqual(result["known_pcm_gaps"], [])
        self.assertFalse(result["full_source_support"])
        self.assertFalse(result["native_graph_constructed"])

    def test_partition_rejects_count_ordinal_geometry_or_capacity_changes(self):
        beats = self.fixture()
        original = [module.project(beats, [], a, b, 960000)
                    for a, b in ((0, 262144), (262144, 524288), (524288, 960000))]
        for key in ("in_window_events", "anchor_ordinal"):
            changed = deepcopy(original)
            changed[1]["support"][key] += 1
            with self.assertRaises(ValueError):
                module.check_partition(changed, 960000)
        changed = deepcopy(original)
        changed[1]["first_frame"] += 1
        with self.assertRaises(ValueError):
            module.check_partition(changed, 960000)
        with self.assertRaises(ValueError):
            module.check_partition(original * 3, 960000)
        with self.assertRaises(ValueError):
            module.check_partition(original[:-1], 960000)

    def test_lost_support_is_counted_as_a_regression(self):
        before = {"confirmed_period_windows": 1, "confirmed_phase_windows": 1,
                  "confirmed_joint_windows": 1, "meter_correct": 1, "downbeat_correct": 1}
        result = module.paired(before, {k: 0 for k in before})
        for field in ("period", "phase", "joint", "meter", "downbeat"):
            self.assertEqual(result[field + "_breaks"], 1)

    def test_registration_hash_is_enforced(self):
        self.assertFalse(module.protocol()["formal_acceptance"])
        with mock.patch.object(module.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                module.protocol()


if __name__ == "__main__":
    unittest.main()
