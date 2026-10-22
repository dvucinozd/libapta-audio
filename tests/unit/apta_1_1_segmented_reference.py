#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from fractions import Fraction
from itertools import product
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_segmented_reference as module


class SegmentedReferenceTests(unittest.TestCase):
    def fixture(self, beats=None, total=400000):
        beats = list(range(10000, 380000, 24000)) if beats is None else beats
        truth = {"beat_frames": beats, "downbeat_frames": beats[::4], "meter_numerator": 4, "meter_denominator": 4}
        windows = [{"first_frame": 0, "end_frame": total}]
        return truth, windows, total

    def test_constant_positive_control_is_one_complete_piece(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total)
        self.assertEqual(result["status"], "witness")
        self.assertEqual(result["minimum_segments"], 1)
        self.assertTrue(result["wire_capacity_ok"])
        self.assertEqual([p // module.Q for p, _, _ in module.expand(result["segments"])], truth["beat_frames"])
        self.assertEqual(result["coverage"], [10000, truth["beat_frames"][-1] + 1])

    def test_native_millibpm_interval_matches_exhaustive_integer_tempos(self):
        for tempo in (40000, 120000, 300000):
            period = module.K // tempo
            possible = [t for t in range(40000, 300001) if period <= module.K // t <= period]
            self.assertEqual(module.tempo_interval(period, period), (min(possible), max(possible)))
        self.assertIsNone(module.tempo_interval(module.K // 40000 + 1, module.K // 40000 + 1))
        self.assertIsNone(module.tempo_interval(20, 19))

    def test_fractional_native_positions_survive_reference_approximation(self):
        period = module.K // 123457
        beats = [10000 + module.ref.rounded(Fraction(i * period, module.Q)) for i in range(16)]
        truth, windows, total = self.fixture(beats)
        result = module.plan(truth, windows, total)
        self.assertEqual(result["status"], "witness")
        points = module.expand(result["segments"])
        self.assertTrue(any(p % module.Q for p, _, _ in points))
        self.assertEqual([i for _, i, _ in points], list(range(16)))
        self.assertTrue(all(abs(Fraction(p, module.Q) - b) < 1 for (p, _, _), b in zip(points, beats)))

    def test_slow_drift_keeps_actual_junction_intervals_within_guard(self):
        beats = [10000]
        for i in range(30):
            beats.append(beats[-1] + 24000 + i * 20)
        truth, windows, total = self.fixture(beats, 900000)
        result = module.plan(truth, windows, total)
        self.assertEqual(result["status"], "witness")
        data = module.prepare(truth, windows, total)
        module.validate_points(data, result["segments"], module.expand(result["segments"]))

    def test_partition_minimum_and_longest_first_tie_match_small_exhaustive_paths(self):
        for gaps in ([24000] * 6, [24000, 24020, 24040, 24060, 24080, 24100], [24000, 24000, 24200, 24200, 24400, 24400], [24000] * 4 + [25000] * 4):
            beats = [10000]
            for gap in gaps:
                beats.append(beats[-1] + gap)
            truth, windows, total = self.fixture(beats)
            data = module.prepare(truth, windows, total)
            edges = {(s, c["end_ordinal"]) for s in range(len(beats)) for c in module.candidates(data, s)}
            paths = []
            for cuts in product((False, True), repeat=len(beats) - 1):
                ends = [i + 1 for i, cut in enumerate(cuts) if cut] + [len(beats)]
                starts = [0] + ends[:-1]
                if all((a, b) in edges for a, b in zip(starts, ends)):
                    paths.append(ends)
            result = module.plan(truth, windows, total)
            if not paths:
                self.assertEqual(result["status"], "no_anchored_family_path")
            else:
                expected = min(paths, key=lambda p: (len(p), tuple(-v for v in p)))
                self.assertEqual([s["end_ordinal"] for s in result["segments"]], expected)

    def test_meters_keep_original_downbeat_ordinals_on_expanded_witness(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total)
        meters = module.meters(truth, windows, result)
        self.assertEqual(len(meters), 1)
        point = module.expand(result["segments"])[meters[0]["downbeat_ordinal"]]
        self.assertEqual(meters[0]["downbeat_frame"], point[0] // module.Q)
        self.assertEqual(meters[0]["first_frame"], result["coverage"][0])
        self.assertEqual(meters[0]["end_frame"], result["coverage"][1])

    def test_original_s6_membership_is_preserved_for_every_reference(self):
        truth, _, total = self.fixture()
        windows = [{"first_frame": 0, "end_frame": 190001}, {"first_frame": 190001, "end_frame": total}]
        result = module.plan(truth, windows, total)
        self.assertEqual(result["status"], "witness")
        for (point, _, _), reference in zip(module.expand(result["segments"]), truth["beat_frames"]):
            self.assertEqual(point < 190001 * module.Q, reference < 190001)

    def test_source_shift_changes_only_absolute_coordinates(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total)
        shift = 12345
        shifted = dict(truth, beat_frames=[p + shift for p in truth["beat_frames"]], downbeat_frames=[p + shift for p in truth["downbeat_frames"]])
        shifted_result = module.plan(shifted, [{"first_frame": 0, "end_frame": total + shift}], total + shift)
        self.assertEqual(result["minimum_segments"], shifted_result["minimum_segments"])
        self.assertEqual([p + shift * module.Q for p, _, _ in module.expand(result["segments"])], [p for p, _, _ in module.expand(shifted_result["segments"])])

    def test_declared_gap_refuses_whole_witness_without_inserting_or_bridging(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total, ((100000, 110000),))
        self.assertEqual(result["status"], "unsupported_pcm_gap")
        self.assertEqual(result["segments"], [])
        outside = module.plan(truth, windows, total, ((0, 1000),))
        self.assertEqual(outside["status"], "witness")

    def test_unsupported_tempo_cannot_be_clamped_into_a_path(self):
        truth, windows, total = self.fixture(list(range(10000, 370000, 8000)))
        self.assertEqual(module.plan(truth, windows, total)["status"], "no_anchored_family_path")

    def test_singleton_source_has_no_invented_period(self):
        truth, windows, total = self.fixture([10000])
        self.assertEqual(module.plan(truth, windows, total)["status"], "insufficient_reference")

    def test_single_beat_pieces_retain_original_indices_and_counts(self):
        truth, windows, total = self.fixture()
        data = module.prepare(truth, windows, total)
        pieces = [next(c for c in module.candidates(data, i) if c["end_ordinal"] == i + 1) for i in range(len(truth["beat_frames"]))]
        points = module.expand(pieces)
        module.validate_points(data, pieces, points)
        self.assertGreater(len(pieces), 8)
        self.assertEqual([i for _, i, _ in points], list(range(len(truth["beat_frames"]))))

    def test_natural_point_and_identity_corruption_are_rejected(self):
        truth, windows, total = self.fixture()
        data = module.prepare(truth, windows, total)
        result = module.plan(truth, windows, total)
        points = module.expand(result["segments"])
        bad = list(points)
        bad[1] = (bad[1][0], 7, bad[1][2])
        with self.assertRaises(ValueError):
            module.validate_points(data, result["segments"], bad)
        shortened = [dict(result["segments"][0], beat_count=2)]
        with self.assertRaises(ValueError):
            module.validate_points(data, shortened, module.expand(shortened))

    def test_invalid_reference_partition_and_cap_fail(self):
        for beats in ([0, 10, 10], [-1, 10000], [0, 400000], list(range(3073))):
            truth, windows, total = self.fixture(beats)
            with self.assertRaises(ValueError):
                module.plan(truth, windows, total)
        truth, windows, total = self.fixture()
        with self.assertRaises(ValueError):
            module.plan(truth, [{"first_frame": 0, "end_frame": total - 1}], total)
        with self.assertRaises(ValueError):
            module.plan(truth, windows, total, ((20, 10),))

    def test_protocol_hash_is_enforced(self):
        self.assertFalse(module.protocol()["formal_acceptance"])
        with mock.patch.object(module.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                module.protocol()


if __name__ == "__main__":
    unittest.main()
