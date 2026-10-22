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
import apta_1_1_free_phase_reachability as module

I = module.Interval


class FreePhaseTests(unittest.TestCase):
    def fixture(self, beats=None, total=900000):
        beats = list(range(10000, 850000, 24000)) if beats is None else beats
        return ({"beat_frames": beats, "downbeat_frames": beats[::4]},
                [{"first_frame": 0, "end_frame": total}], total)

    def test_closed_singleton_feasible_and_strict_singleton_empty(self):
        self.assertFalse(I(1, 1).empty)
        for lc, hc in ((False, True), (True, False), (False, False)):
            self.assertTrue(I(1, 1, lc, hc).empty)
        self.assertTrue(I(2, 1).empty)

    def test_intersection_preserves_strict_tied_endpoints(self):
        self.assertEqual(I(0, 2).intersect(I(0, 2, False, False)), I(0, 2, False, False))
        self.assertTrue(I(0, 1, True, False).intersect(I(1, 2)).empty)
        self.assertEqual(I(0, 2).add(I(1, 3, False, True)), I(1, 5, False, True))

    def test_choose_nearest_included_bound_otherwise_midpoint(self):
        self.assertEqual(I(0, 2).choose(-1), 0)
        self.assertEqual(I(0, 2).choose(3), 2)
        self.assertEqual(I(0, 2, False, False).choose(-1), 1)
        self.assertEqual(I(0, 2, False, False).choose(F(1, 7)), F(1, 7))
        with self.assertRaises(ValueError):
            I(1, 0).choose(0)

    def test_forward_reachability_and_backward_moving_phase(self):
        domains = [I(0, 2), I(2, 4), I(4, 6)]
        status, points = module.reachable(domains, [I(2, 2)] * 2, [0, 4, 5])
        self.assertEqual(status["status"], "feasible")
        self.assertEqual(points, [1, 3, 5])
        # The fixed first reference anchor 0 would forbid the required last 5.
        status, _ = module.reachable([I(0, 0), I(2, 4), I(5, 5)], [I(2, 2)] * 2, [0, 4, 5])
        self.assertEqual(status["first_empty_ordinal"], 2)

    def test_cumulative_drift_is_infeasible_despite_pairwise_overlap(self):
        result, points = module.reachable([I(0, 0), I(1, 3), I(5, 6)], [I(1, 2)] * 2, [0, 2, 5])
        self.assertEqual(result["first_empty_ordinal"], 2)
        self.assertEqual(points, [])

    def test_strict_reachability_does_not_silently_close_an_endpoint(self):
        result, _ = module.reachable([I(0, 1, True, False), I(2, 2)], [I(1, 1)], [0, 2])
        self.assertEqual(result["status"], "infeasible")
        result, points = module.reachable([I(0, 1, False, True), I(2, 2)], [I(1, 1)], [0, 2])
        self.assertEqual(points, [1, 2])

    def test_difference_checker_detects_strict_zero_cycle(self):
        self.assertTrue(module.difference_constraints_feasible(2, [(0, 1, 1, False), (1, 0, -1, False)])[0])
        okay, cycle = module.difference_constraints_feasible(2, [(0, 1, 1, True), (1, 0, -1, False)])
        self.assertFalse(okay)
        self.assertEqual(cycle["total_bound"], [0, 1])
        self.assertEqual(cycle["strict_edges"], 1)

    def test_difference_checker_has_no_numeric_epsilon(self):
        tiny = F(1, 10 ** 100)
        self.assertTrue(module.difference_constraints_feasible(2, [(0, 1, tiny, True), (1, 0, 0, True)])[0])
        okay, cycle = module.difference_constraints_feasible(2, [(0, 1, -tiny, False), (1, 0, 0, False)])
        self.assertFalse(okay)
        self.assertEqual(cycle["total_bound"], [-1, 10 ** 100])

    def test_two_event_chains_match_exhaustive_integer_lattice(self):
        # Closed integer bounds yield integral feasible vertices: exact oracle.
        choices = [I(a, b) for a in range(-1, 2) for b in range(a, 3)]
        for left, right, step in product(choices, repeat=3):
            expected = any(left.contains(a) and right.contains(b) and step.contains(b - a)
                           for a, b in product(range(-1, 3), repeat=2))
            result, _ = module.reachable([left, right], [step], [0, 0])
            self.assertEqual(result["status"] == "feasible", expected)

    def test_random_strict_fractional_chains_match_independent_graph(self):
        rng = random.Random(4811)
        for _ in range(120):
            n = rng.randrange(2, 8)
            def interval():
                a, b = sorted(F(rng.randrange(-10, 11), 7) for _ in range(2))
                return I(a, b, bool(rng.randrange(2)), bool(rng.randrange(2)))
            domains, steps = [interval() for _ in range(n)], [interval() for _ in range(n - 1)]
            edges = []
            for i, domain in enumerate(domains):
                edges += [(0, i + 1, domain.high, not domain.high_closed),
                          (i + 1, 0, -domain.low, not domain.low_closed)]
            for i, step in enumerate(steps):
                edges += [(i + 1, i + 2, step.high, not step.high_closed),
                          (i + 2, i + 1, -step.low, not step.low_closed)]
            result, points = module.reachable(domains, steps, [0] * n)
            self.assertEqual(result["status"] == "feasible", module.difference_constraints_feasible(n + 1, edges)[0])
            if points:
                self.assertTrue(all(domain.contains(p) for domain, p in zip(domains, points)))
                self.assertTrue(all(step.contains(b - a) for step, a, b in zip(steps, points, points[1:])))

    def test_constant_path_is_exact_and_final_successor_not_invented(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total)
        self.assertEqual(result["status"], "feasible")
        self.assertEqual([F(*p) for p in result["points"]], truth["beat_frames"])
        self.assertEqual(len(result["points"]), len(truth["beat_frames"]))

    def test_old_fractional_and_changing_native_witnesses_embed(self):
        fractional = [10000 + module.ref.rounded(F(i * (module.K // 123457), module.Q)) for i in range(32)]
        drift = [10000]
        for i in range(30):
            drift.append(drift[-1] + 24000 + i * 20)
        for beats in (fractional, drift):
            truth, windows, total = self.fixture(beats)
            old = module.segmented.plan(truth, windows, total)
            self.assertEqual(old["status"], "witness")
            data = module.segmented.prepare(truth, windows, total)
            module.validate(data, [F(p, module.Q) for p, _, _ in module.segmented.expand(old["segments"])],
                            module.constraints(data)[3])
            self.assertEqual(module.plan(truth, windows, total)["status"], "feasible")

    def test_generated_reference_moves_phase_when_anchored_family_has_no_path(self):
        rng, beats = random.Random(9981), [10000]
        for _ in range(24):
            beats.append(beats[-1] + 24000 + rng.randrange(-1000, 1001))
        truth, windows, total = self.fixture(beats)
        self.assertEqual(module.segmented.plan(truth, windows, total)["status"], "no_anchored_family_path")
        result = module.plan(truth, windows, total)
        self.assertEqual(result["status"], "feasible")
        self.assertTrue(any(F(*p) != b for p, b in zip(result["points"], beats)))

    def test_reference_interval_contradiction_has_two_independent_certificates(self):
        truth, windows, total = self.fixture([10000] + list(range(40000, 850000, 24000)))
        result = module.plan(truth, windows, total)
        self.assertEqual(result["status"], "infeasible")
        self.assertEqual(result["first_empty_ordinal"], 1)
        self.assertTrue(result["negative_cycle"]["edge_indices"])
        self.assertLess(F(*result["negative_cycle"]["total_bound"]), 0)

    def test_nominal_tempo_outside_original_range_remains_infeasible(self):
        truth, windows, total = self.fixture(list(range(10000, 850000, 8000)))
        result = module.plan(truth, windows, total)
        self.assertEqual(result["status"], "infeasible")
        self.assertTrue(result["difference_constraints_agree"])

    def test_negative_guard_is_not_rescued(self):
        truth, windows, total = self.fixture([10000, 10001, 10002])
        self.assertEqual(module.plan(truth, windows, total)["status"], "infeasible")

    def test_strict_natural_geometry_rejects_hidden_previous_point(self):
        truth, windows, total = self.fixture()
        data = module.segmented.prepare(truth, windows, total)
        nominal = module.constraints(data)[3]
        points = [F(p) for p in truth["beat_frames"]]
        # A direct geometry corruption of the first cell keeps phase in bounds.
        data = deepcopy(data)
        data["boundaries"][0] = points[0] - nominal[0].high
        with self.assertRaises(ValueError):
            module.validate(data, points, nominal)

    def test_corrupted_coordinate_and_ordinal_count_fail(self):
        truth, windows, total = self.fixture()
        data = module.segmented.prepare(truth, windows, total)
        nominal = module.constraints(data)[3]
        points = [F(*p) for p in module.plan(truth, windows, total)["points"]]
        for bad in (points[:-1], [points[0] - 1] + points[1:], points[:2] + [points[2] + 1000] + points[3:]):
            with self.assertRaises(ValueError):
                module.validate(data, bad, nominal)

    def test_window_ownership_and_reference_envelope_retained(self):
        truth, _, total = self.fixture()
        windows = [{"first_frame": 0, "end_frame": 250001}, {"first_frame": 250001, "end_frame": total}]
        result = module.plan(truth, windows, total)
        for position, reference in zip(result["points"], truth["beat_frames"]):
            point = F(*position)
            self.assertEqual(point < 250001, reference < 250001)
            self.assertTrue(truth["beat_frames"][0] <= point < truth["beat_frames"][-1] + 1)

    def test_source_translation_changes_only_absolute_coordinates(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total)
        shifted = {key: [p + 12345 for p in value] for key, value in truth.items()}
        other = module.plan(shifted, [{"first_frame": 0, "end_frame": total + 12345}], total + 12345)
        self.assertEqual([F(*p) + 12345 for p in result["points"]], [F(*p) for p in other["points"]])

    def test_gaps_refuse_whole_path_but_outside_envelope_is_allowed(self):
        truth, windows, total = self.fixture()
        self.assertEqual(module.plan(truth, windows, total, [(100000, 110000)])["status"], "unsupported_pcm_gap")
        self.assertEqual(module.plan(truth, windows, total, [(0, 10000)])["status"], "feasible")
        with self.assertRaises(ValueError):
            module.plan(truth, windows, total, [(20, 10)])

    def test_missing_reference_context_remains_unknown_in_common_score(self):
        truth, windows, total = self.fixture()
        result = module.plan(truth, windows, total)
        points = [F(*p) for p in result["points"]]
        scored = module.representation.score_points([(p * module.Q, (points[i + 1] - p) * module.Q if i + 1 < len(points) else None)
                                                     for i, p in enumerate(points)], truth, 0, total)
        self.assertEqual(scored["reference_complete_windows"], 0)
        self.assertEqual(scored["joint_windows"], 0)
        self.assertEqual(scored["unknown_successor_points"], 1)

    def test_invalid_partition_events_and_reference_caps_fail_closed(self):
        for beats in ([0, 10, 10], [-1, 10000], list(range(3073))):
            truth, windows, total = self.fixture(beats)
            with self.assertRaises(ValueError):
                module.plan(truth, windows, total)
        truth, windows, total = self.fixture()
        with self.assertRaises(ValueError):
            module.plan(truth, [{"first_frame": 0, "end_frame": total - 1}], total)
        self.assertEqual(module.plan(*self.fixture([10000]))["status"], "insufficient_reference")

    def test_protocol_hash_and_checker_disagreement_fail_closed(self):
        self.assertFalse(module.protocol()["formal_acceptance"])
        with mock.patch.object(module.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                module.protocol()
        with mock.patch.object(module, "difference_constraints_feasible", return_value=(False, None)):
            with self.assertRaises(ValueError):
                module.plan(*self.fixture())


if __name__ == "__main__":
    unittest.main()
