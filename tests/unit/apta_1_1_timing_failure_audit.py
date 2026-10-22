#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from copy import deepcopy
from fractions import Fraction
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_timing_failure_audit as module


class TimingFailureAuditTests(unittest.TestCase):
    def fixture(self, period=24000):
        beats = list(range(0, 960000, period))
        return {"beat_frames": beats, "downbeat_frames": beats[::4],
                "meter_numerator": 4, "meter_denominator": 4}

    def analyse(self, beats=None, truth=None, first=262144, end=524288, total=960000):
        truth = truth or self.fixture()
        beats = truth["beat_frames"] if beats is None else beats
        return module.analyse_window(beats, [], truth, first, end, total)

    def test_positive_constant_witness_and_model_pass(self):
        result = self.analyse()
        self.assertEqual(result["triage_group"], "model_joint_pass")
        self.assertEqual(result["witness_score"]["confirmed_joint_windows"], 1)
        self.assertEqual(result["witness_common"]["complete_sequence_windows"], 1)
        self.assertFalse(result["reference_periods"]["constant_period_intersection_empty"])
        self.assertEqual(result["tempo_family"]["kind"], "same_period")
        self.assertEqual(result["correspondence"]["uncovered_reference"], 0)

    def test_half_and_double_period_are_named_without_changing_model(self):
        original = self.fixture()["beat_frames"]
        for beats, family in ((original[::2], "double_period_half_bpm"),
                              (list(range(0, 960000, 12000)), "half_period_double_bpm")):
            result = self.analyse(beats)
            self.assertEqual(result["tempo_family"]["kind"], family)
            self.assertEqual(result["triage_group"], "non_unit_tempo_family")
            self.assertTrue(result["flags"]["event_correspondence_mismatch"])
            self.assertEqual(result["model_score"]["confirmed_period_windows"], 0)

    def test_all_fixed_family_ratios_and_unmatched_ratio(self):
        for name, ratio in module.FAMILIES:
            period = 36000 * ratio
            tempo = module.ref.rounded(Fraction(48000 * 60000, 1) / period)
            result = module.family({"selected": {"kind": "accepted", "tempo_millibpm": tempo}}, Fraction(36000))
            self.assertEqual(result["kind"], name)
        self.assertEqual(module.family({"selected": {"kind": "accepted", "tempo_millibpm": 97000}}, Fraction(36000))["kind"], "unmatched_ratio")
        self.assertEqual(module.family({"selected": {"kind": "insufficient_events"}}, Fraction(36000))["kind"], "unavailable_projection")

    def test_half_beat_phase_offset_is_a_correspondence_mismatch(self):
        result = self.analyse(list(range(12000, 960000, 24000)))
        self.assertEqual(result["model_score"]["confirmed_period_windows"], 1)
        self.assertEqual(result["model_score"]["confirmed_phase_windows"], 0)
        self.assertEqual(result["tempo_family"]["kind"], "same_period")
        self.assertGreater(result["correspondence"]["phase_mismatched_events"], 0)
        self.assertEqual(result["triage_group"], "event_correspondence_mismatch")

    def test_missing_event_and_extra_phase_wrong_event_are_distinct_counts(self):
        beats = self.fixture()["beat_frames"]
        missing = self.analyse([p for p in beats if p != 384000])
        self.assertEqual(missing["correspondence"]["uncovered_reference"], 1)
        self.assertEqual(missing["correspondence"]["phase_mismatched_events"], 0)
        extra = self.analyse(sorted(beats + [396000]))
        self.assertEqual(extra["correspondence"]["uncovered_reference"], 0)
        self.assertEqual(extra["correspondence"]["phase_mismatched_events"], 1)

    def test_duplicate_nearby_match_is_not_unique_reference_coverage(self):
        beats = self.fixture()["beat_frames"]
        result = self.analyse(sorted(beats + [385000]))["correspondence"]
        self.assertEqual(result["duplicate_matches"], 1)
        self.assertEqual(result["nonunique_reference"], 1)
        self.assertEqual(result["uncovered_reference"], 0)
        self.assertEqual(result["uniquely_covered_reference"], result["reference_events"] - 1)

    def test_incomplete_reference_is_unknown_before_other_triage(self):
        truth = self.fixture()
        truth["beat_frames"] = truth["beat_frames"][1:]
        truth["downbeat_frames"] = truth["downbeat_frames"][1:]
        result = self.analyse(self.fixture()["beat_frames"], truth, first=0, end=262144)
        self.assertEqual(result["triage_group"], "reference_context_incomplete")
        self.assertEqual(result["correspondence"]["unknown_events"], 1)
        self.assertEqual(result["model_score"]["reference_complete_windows"], 0)

    def test_too_few_model_events_remain_unsupported_not_wrong_family(self):
        result = self.analyse([264000, 288000, 312000])
        self.assertEqual(result["triage_group"], "projection_unsupported")
        self.assertEqual(result["tempo_family"]["kind"], "unavailable_projection")

    def variable_fixture(self):
        beats = [0]
        for gap in [24000] * 10 + [26000] * 28:
            beats.append(beats[-1] + gap)
        return {"beat_frames": beats, "downbeat_frames": beats[::4],
                "meter_numerator": 4, "meter_denominator": 4}

    def test_perfect_reference_events_do_not_guarantee_constant_witness_pass(self):
        truth = self.variable_fixture()
        result = self.analyse(truth=truth, first=100000, end=524288, total=1000000)
        self.assertTrue(result["reference_periods"]["constant_period_intersection_empty"])
        self.assertEqual(result["witness_score"]["confirmed_joint_windows"], 0)
        self.assertEqual(result["correspondence"]["nonunique_reference"], 0)
        self.assertEqual(result["correspondence"]["phase_mismatched_events"], 0)
        self.assertEqual(result["triage_group"], "reference_period_incompatibility")

    def test_exact_closed_period_intersection_keeps_touching_endpoint(self):
        periods = [Fraction(100), Fraction(10100, 99)]
        result = module.period_stats(periods, [])
        self.assertEqual(result["intersection_lower"], [101, 1])
        self.assertEqual(result["intersection_upper"], [101, 1])
        self.assertFalse(result["constant_period_intersection_empty"])
        result = module.period_stats([Fraction(100), Fraction(103)], [])
        self.assertTrue(result["constant_period_intersection_empty"])

    def test_priority_group_preserves_overlapping_family_and_reference_flags(self):
        truth = self.variable_fixture()
        result = self.analyse(list(range(0, 990000, 52000)), truth, first=0, end=524288, total=1000000)
        self.assertEqual(result["triage_group"], "reference_period_incompatibility")
        self.assertTrue(result["flags"]["non_unit_tempo_family"])
        self.assertTrue(result["flags"]["event_correspondence_mismatch"])

    def test_precision_only_model_pass_can_hide_missing_prefix_beats(self):
        truth = self.fixture()
        beats = [p for p in truth["beat_frames"] if not 262144 <= p < 336000]
        result = self.analyse(beats, truth)
        self.assertEqual(result["triage_group"], "model_joint_pass")
        self.assertEqual(result["model_common"]["complete_sequence_windows"], 0)
        self.assertTrue(result["flags"]["event_correspondence_mismatch"])
        self.assertGreater(result["correspondence"]["uncovered_reference"], 0)

    def test_source_shift_preserves_all_attribution_and_original_inputs(self):
        truth = self.fixture()
        before = deepcopy(truth)
        original = self.analyse(truth=truth)
        shift = 123456
        shifted_truth = dict(truth, beat_frames=[p + shift for p in truth["beat_frames"]],
                             downbeat_frames=[p + shift for p in truth["downbeat_frames"]])
        shifted = self.analyse(truth=shifted_truth, first=262144 + shift, end=524288 + shift, total=960000 + shift)
        for field in ("model_score", "witness_score", "model_common", "witness_common", "reference_periods", "tempo_family", "correspondence", "flags", "triage_group"):
            self.assertEqual(original[field], shifted[field])
        self.assertEqual(truth, before)

    def test_nearby_reference_outside_window_is_not_inside_coverage(self):
        truth = self.fixture()["beat_frames"]
        periods = {p: Fraction(24000) for p in truth}
        result = module.correspondence([23999], truth, periods, 1000, 24000)
        self.assertEqual(result["outside_window_matches"], 1)
        self.assertEqual(result["reference_events"], 0)
        self.assertEqual(result["covered_reference"], 0)

    def test_aggregate_unknowns_and_overlap_do_not_double_count_triage(self):
        record = self.analyse()
        record["coarse_score"] = record["model_score"]
        result = module.aggregate([{"genre": "synthetic", "windows": [record]}])
        self.assertEqual(sum(result["triage_all"].values()), 1)
        self.assertEqual(result["model_witness_cross_tab"], {"model_pass_witness_pass": 1})
        self.assertEqual(result["triage_complete_failures"], {})

    def test_invalid_geometry_order_capacity_and_missing_truth_fail(self):
        for beats in ([0, 10, 10], [-1, 10], [0, 960000], list(range(3073))):
            with self.assertRaises(ValueError):
                self.analyse(beats)
        with self.assertRaises(ValueError):
            self.analyse(first=524288, end=262144)
        with self.assertRaises(ValueError):
            self.analyse(total=1440001)
        with self.assertRaises(ValueError):
            self.analyse(truth={"beat_frames": [0], "downbeat_frames": [0]})

    def test_protocol_byte_hash_is_enforced(self):
        self.assertFalse(module.protocol()["formal_acceptance"])
        with mock.patch.object(module.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                module.protocol()


if __name__ == "__main__":
    unittest.main()
