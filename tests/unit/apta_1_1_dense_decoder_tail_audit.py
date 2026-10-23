#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))
import apta_1_1_dense_decoder_tail_audit as tail


class TailTests(unittest.TestCase):
    def beats(self):
        return [0,24000,48000,72000,96000,120000]

    def test_fixed_rational_families(self):
        for numerator,denominator in tail.FAMILIES:
            self.assertEqual(tail.family(24000*numerator//denominator,24000),str(numerator)+"_over_"+str(denominator))
        self.assertEqual(tail.family(27000,24000),"off_family")

    def test_family_relative_inclusive_band(self):
        self.assertEqual(tail.family(12120,24000),"1_over_2")
        self.assertEqual(tail.family(12121,24000),"off_family")
        with self.assertRaises(ValueError):
            tail.family(0,24000)

    def test_five_percent_boundary_is_not_tail(self):
        result = tail.tail_point(0,25200,26,self.beats())
        self.assertEqual(result["period_errors"],1)
        self.assertEqual(result["large_tail_intervals"],0)

    def test_double_interval_skips_a_phase_supported_reference_beat(self):
        result = tail.tail_point(0,48000,50,self.beats())
        self.assertEqual(result["family_2_over_1"],1)
        self.assertEqual(result["reference_step_2"],1)
        self.assertEqual(result["phase_supported_skipped_reference_events"],1)

    def test_half_interval_does_not_imply_both_endpoints_match(self):
        result = tail.tail_point(0,12000,12,self.beats())
        self.assertEqual(result["family_1_over_2"],1)
        self.assertEqual(result["both_endpoints_phase_correct"],0)
        self.assertEqual(result["reference_step_0"],1)

    def test_half_ratio_can_match_adjacent_irregular_reference_events(self):
        beats = [0,24000,48000,60000,84000,108000]
        result = tail.tail_point(48000,60000,12,beats)
        self.assertEqual(result["family_1_over_2"],1)
        self.assertEqual(result["phase_supported_adjacent_reference_events"],1)

    def test_successor_outside_reference_is_not_phase_supported(self):
        result = tail.tail_point(0,96000,100,[0,24000,48000])
        self.assertEqual(result["next_outside_reference"],1)
        self.assertEqual(result["both_endpoints_phase_correct"],0)

    def test_model_period_range_flag(self):
        result = tail.tail_point(0,80000,75,[0,90000,180000,270000])
        self.assertEqual(result["target_above_72000"],1)
        self.assertEqual(result["target_below_9600"],0)

    def test_window_groups_count_each_start_once_and_preserve_separate_support(self):
        observer = tail.TailAudit()
        candidate = {"events":{"beat_frames":[0,48000,96000],"beat_rows":[0,50,100]},
            "windows":[{"beat_ordinals":[0]},{"beat_ordinals":[1,2]}]}
        saved = {"ols_actual":[{"joint_windows":1},{"joint_windows":0}],
            "candidate_actual":[{"joint_windows":0},{"joint_windows":0}],
            "candidate_nominal":[{"joint_windows":1},{"joint_windows":1}]}
        observer.observe("generated",candidate,saved,{"beat_frames":self.beats()})
        self.assertEqual(observer.groups["all"]["large_tail_intervals"],2)
        self.assertEqual(observer.groups["actual_joint_break"]["large_tail_intervals"],1)
        self.assertEqual(observer.windows["nominal_correct_actual_failed"]["windows"],2)
        self.assertNotIn("generated",str(observer.result()))

    def test_last_event_is_never_given_an_invented_interval(self):
        observer = tail.TailAudit()
        candidate = {"events":{"beat_frames":[0],"beat_rows":[0]},"windows":[{"beat_ordinals":[0]}]}
        saved = {k:[{"joint_windows":0}] for k in ("ols_actual","candidate_actual","candidate_nominal")}
        observer.observe("generated",candidate,saved,{"beat_frames":self.beats()})
        self.assertEqual(observer.groups["all"]["known_intervals"],0)


if __name__ == "__main__":
    unittest.main()
