#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from copy import deepcopy
from fractions import Fraction as F
from itertools import product
from pathlib import Path
import random
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_1_1_quantized_guided_reference as module


class QuantizedGuidedTests(unittest.TestCase):
    def fixture(self, beats=None, total=900000):
        beats = list(range(10000, 850000, 24000)) if beats is None else beats
        truth = {"beat_frames": beats, "downbeat_frames": beats[::4]}
        windows = [{"first_frame": 0, "end_frame": total}]
        return truth, windows, total

    def test_constant_complete_path_is_one_wire_qualified_piece(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total, module.free.plan(truth, windows, total))
        self.assertEqual(result["status"], "witness")
        self.assertEqual(result["minimum_segments"], 1)
        self.assertTrue(result["wire_capacity_ok"])
        self.assertEqual([p // module.Q for p, _, _ in module.seg.expand(result["segments"])], truth["beat_frames"])

    def test_anchor_rounding_ties_up_and_clamps_to_strict_domain(self):
        data = {"beats": [0, 24000]}
        reach = {"points": [[1, 2], [48001, 2]]}
        domains = [module.free.Interval(0, 2), module.free.Interval(24000, 24001, True, False)]
        with mock.patch.object(module.free, "constraints", return_value=(domains, [], [], [])):
            self.assertEqual(module.anchors(data, reach), [1, 24000])
        domains[0] = module.free.Interval(0, 2, False, True)
        with mock.patch.object(module.free, "constraints", return_value=(domains, [], [], [])):
            self.assertEqual(module.anchors(data, {"points": [[1, 10], [48001, 2]]}), [1, 24000])

    def test_empty_integer_domain_is_explicit_not_continuous_infeasibility(self):
        data = {"beats": [0]}
        domains = [module.free.Interval(F(1, 4), F(3, 4))]
        with mock.patch.object(module.free, "constraints", return_value=(domains, [], [], [])):
            self.assertIsNone(module.anchors(data, {"points": [[1, 2]]}))

    def test_saved_point_identity_and_domain_corruption_fail(self):
        truth, windows, total = self.fixture()
        data = module.seg.prepare(truth, windows, total)
        reach = module.free.plan(truth, windows, total)
        for bad in (dict(reach, points=[]), dict(reach, points=[[0, 1]] + reach["points"][1:])):
            with self.assertRaises(ValueError):
                module.anchors(data, bad)

    def test_relaxation_infeasible_and_insufficient_are_distinct(self):
        truth, windows, total = self.fixture([10000] + list(range(40000, 850000, 24000)))
        reach = module.free.plan(truth, windows, total)
        self.assertEqual(reach["status"], "infeasible")
        self.assertEqual(module.plan(truth, windows, total, reach)["status"], "relaxation_infeasible")
        truth, windows, total = self.fixture([10000])
        self.assertEqual(module.plan(truth, windows, total, {"status": "insufficient_reference"})["status"], "insufficient_reference")

    def test_guided_family_failure_does_not_claim_all_anchors_impossible(self):
        rng, beats = random.Random(9981), [10000]
        for _ in range(24):
            beats.append(beats[-1] + 24000 + rng.randrange(-1000, 1001))
        truth, windows, total = self.fixture(beats)
        reach = module.free.plan(truth, windows, total)
        self.assertEqual(reach["status"], "feasible")
        result = module.plan(truth, windows, total, reach)
        self.assertEqual(result["status"], "no_guided_family_path")
        self.assertEqual(len(result["guided_anchors"]), len(beats))

    def test_partition_minima_and_ties_match_exhaustive_small_paths(self):
        for gaps in ([24000] * 7, [24000 + 20 * i for i in range(7)], [24000] * 4 + [25000] * 3):
            beats = [10000]
            for gap in gaps:
                beats.append(beats[-1] + gap)
            truth, windows, total = self.fixture(beats)
            reach = module.free.plan(truth, windows, total)
            if reach["status"] != "feasible":
                continue
            data = module.seg.prepare(truth, windows, total)
            guided = module.anchors(data, reach)
            edges = {(i, p["end_ordinal"]) for i in range(len(beats)) for p in module.candidates(data, guided, i)}
            paths = []
            for cuts in product((False, True), repeat=len(beats) - 1):
                ends = [i + 1 for i, cut in enumerate(cuts) if cut] + [len(beats)]
                if all((a, b) in edges for a, b in zip([0] + ends[:-1], ends)):
                    paths.append(ends)
            result = module.plan(truth, windows, total, reach)
            if paths:
                expected = min(paths, key=lambda p: (len(p), tuple(-e for e in p)))
                self.assertEqual([p["end_ordinal"] for p in result["segments"]], expected)
            else:
                self.assertEqual(result["status"], "no_guided_family_path")

    def test_fractional_points_keep_meter_ordinal_and_whole_frame_binding(self):
        period = module.K // 123457
        beats = [10000 + module.ref.rounded(F(i * period, module.Q)) for i in range(32)]
        truth, windows, total = self.fixture(beats)
        result = module.plan(truth, windows, total, module.free.plan(truth, windows, total))
        self.assertEqual(result["status"], "witness")
        points = module.seg.expand(result["segments"])
        self.assertTrue(any(p % module.Q for p, _, _ in points))
        meters = module.seg.meters(truth, windows, result)
        self.assertTrue(meters)
        self.assertTrue(all(m["downbeat_frame"] == points[m["downbeat_ordinal"]][0] // module.Q for m in meters))

    def test_native_request_explicit_anchor_field_does_not_change_legacy_layout(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total, module.free.plan(truth, windows, total))
        meters = module.seg.meters(truth, windows, result)
        new = module.native_request(truth, result, meters, total).split()
        old = module.seg.native_request(truth, result, meters, total).split()
        offset = 5 + len(truth["beat_frames"]) * 2
        self.assertEqual(new[0], "SEGMENT_GUIDED_V1")
        self.assertEqual(new[1:offset], old[1:offset])
        self.assertEqual(new[offset:offset + 3], old[offset:offset + 3])
        self.assertEqual(int(new[offset + 3]), result["segments"][0]["anchor_frame"])
        self.assertEqual(new[offset + 4:], old[offset + 3:])

    def test_window_and_source_translation_keep_original_membership(self):
        truth, _, total = self.fixture()
        windows = [{"first_frame": 0, "end_frame": 250001}, {"first_frame": 250001, "end_frame": total}]
        result = module.plan(truth, windows, total, module.free.plan(truth, windows, total))
        for (point, ordinal, _), reference in zip(module.seg.expand(result["segments"]), truth["beat_frames"]):
            self.assertEqual(point < 250001 * module.Q, reference < 250001)
            self.assertEqual(ordinal, truth["beat_frames"].index(reference))
        shift = 12345
        shifted = {k: [p + shift for p in v] for k, v in truth.items()}
        other_windows = [{"first_frame": 0, "end_frame": total + shift}]
        other = module.plan(shifted, other_windows, total + shift, module.free.plan(shifted, other_windows, total + shift))
        self.assertEqual([p + shift * module.Q for p, _, _ in module.seg.expand(result["segments"])],
                         [p for p, _, _ in module.seg.expand(other["segments"])])

    def test_contradiction_categories_are_mechanical_not_musical_verdicts(self):
        truth, windows, total = self.fixture()
        data = module.seg.prepare(truth, windows, total)
        self.assertEqual(module.contradiction_category(data), "chain_contradiction")
        nominal_bad = deepcopy(data)
        nominal_bad["periods"][0] = F(8000)
        self.assertEqual(module.contradiction_category(nominal_bad), "empty_nominal_tempo_range")
        geometry_bad = deepcopy(data)
        geometry_bad["boundaries"][1] = 100000
        self.assertEqual(module.contradiction_category(geometry_bad), "empty_geometry_domain")

    def test_protocol_hash_and_unsupported_saved_status_fail_closed(self):
        self.assertFalse(module.protocol()["formal_acceptance"])
        with mock.patch.object(module.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                module.protocol()
        with self.assertRaises(ValueError):
            module.plan(*self.fixture(), {"status": "unknown"})


if __name__ == "__main__":
    unittest.main()
