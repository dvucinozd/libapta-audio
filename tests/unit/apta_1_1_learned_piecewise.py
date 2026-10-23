#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import ast
from copy import deepcopy
from fractions import Fraction as F
from itertools import product
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import apta_learned_piecewise as model
import apta_1_1_learned_piecewise as evaluation


class LearnedPiecewiseTests(unittest.TestCase):
    def fixture(self, beats=None, end=None):
        beats = list(range(10000, 298000, 24000)) if beats is None else beats
        end = beats[-1] + 12000 if end is None else end
        return beats, beats[::4], [{"first_frame": 0, "end_frame": end}], end

    def test_constant_q32_path_is_exact_with_own_meter(self):
        args = self.fixture()
        result = model.construct(*args)
        self.assertTrue(result["full_source_support"])
        window = result["windows"][0]
        self.assertEqual(len(window["pieces"]), 1)
        self.assertEqual([p // model.Q for p, _, _ in model.expand(window["pieces"])], args[0])
        self.assertEqual(window["meter"]["downbeat_frame"], args[0][0])
        self.assertEqual(window["meter"]["downbeat_ordinal"], 0)

    def test_two_tempos_need_two_pieces_without_event_loss(self):
        beats = list(range(10000, 298000, 24000)) + list(range(298000, 598000, 25000))
        args = self.fixture(beats)
        result = model.construct(*args)
        self.assertTrue(result["full_source_support"])
        pieces = result["windows"][0]["pieces"]
        self.assertGreater(len(pieces), 1)
        self.assertEqual([p[1] for p in model.expand(pieces)], list(range(len(beats))))
        self.assertTrue(all(model.compatible(a, b) for a, b in zip(model.expand(pieces), model.expand(pieces)[1:])))

    def test_fractional_published_period_and_ols_match_exact_equations(self):
        period = F(48000 * 60000, 123457)
        beats = [10000 + model.rounded(i * period) for i in range(12)]
        args = self.fixture(beats)
        result = model.construct(*args)
        piece = result["windows"][0]["pieces"][0]
        n = len(beats)
        mean_x, mean_y = F(n - 1, 2), F(sum(beats), n)
        slope = sum((F(i) - mean_x) * (p - mean_y) for i, p in enumerate(beats)) / sum((F(i) - mean_x) ** 2 for i in range(n))
        self.assertEqual(piece["anchor_frame"], model.rounded(mean_y - slope * mean_x))
        self.assertEqual(piece["tempo_millibpm"], model.rounded(F(model.RATE * 60000) / slope))
        self.assertTrue(any(p % model.Q for p, _, _ in model.expand([piece])))

    def test_small_partitions_match_exhaustive_paths_objective(self):
        for gaps in ([24000] * 11, [24000] * 5 + [25000] * 6, [24000 + i * 30 for i in range(11)]):
            beats = [10000]
            for gap in gaps:
                beats.append(beats[-1] + gap)
            args = self.fixture(beats)
            end = args[-1]
            bounds = [0] + [(a + b) // 2 + 1 for a, b in zip(beats, beats[1:])] + [end]
            paths = []
            for cuts in product((False, True), repeat=len(beats) - 1):
                ends = [i + 1 for i, cut in enumerate(cuts) if cut] + [len(beats)]
                pieces = [model.fit(beats, a, b, bounds, 0) for a, b in zip([0] + ends[:-1], ends)]
                if all(pieces) and all(model.compatible(model.expand([a])[-1], model.expand([b])[0]) for a, b in zip(pieces, pieces[1:])):
                    paths.append(pieces)
            result = model.construct(*args)["windows"][0]
            if paths:
                self.assertEqual(result["pieces"], min(paths, key=model.path_key))
            else:
                self.assertEqual(result["status"], "no_piecewise_path")

    def test_one_hop_residual_inconsistent_event_abstains_without_deletion(self):
        beats = [10000, 34000, 58000, 87000, 106000, 130000, 154000]
        result = model.construct(*self.fixture(beats))
        self.assertEqual(result["windows"][0]["status"], "no_piecewise_path")
        self.assertFalse(result["windows"][0]["pieces"])

    def test_natural_count_rejects_window_tail_hiding_an_extra_beat(self):
        args = self.fixture(end=400000)
        result = model.construct(*args)
        self.assertEqual(result["windows"][0]["status"], "no_piecewise_path")

    def test_fewer_than_four_events_are_unknown_support_not_pcm_gap(self):
        result = model.construct(*self.fixture([10000, 34000, 58000]))
        self.assertEqual(result["windows"][0]["status"], "insufficient_events")
        self.assertEqual(result["known_pcm_gaps"], [])

    def test_gap_rejects_affected_window_only_and_is_preserved(self):
        beats = list(range(10000, 586000, 24000))
        windows = [{"first_frame": 0, "end_frame": 288000}, {"first_frame": 288000, "end_frame": 576000}]
        result = model.construct(beats, beats[::4], windows, 576000, [(20000, 21000)])
        self.assertEqual(result["windows"][0]["status"], "known_pcm_gap")
        self.assertEqual(result["windows"][1]["status"], "accepted")
        self.assertEqual(result["known_pcm_gaps"], [[20000, 21000]])

    def test_bad_source_seam_marks_both_without_retry(self):
        beats = list(range(10000, 298000, 24000)) + list(range(302000, 590000, 24000))
        windows = [{"first_frame": 0, "end_frame": 298000}, {"first_frame": 298000, "end_frame": 578000}]
        # Correct the fixture end to include all events plus the natural half-period tail.
        total = beats[-1] + 12000
        windows[-1]["end_frame"] = total
        result = model.construct(beats, beats[::4], windows, total)
        self.assertEqual(result["failed_source_seams"], [0, 1])
        self.assertEqual([w["status"] for w in result["windows"]], ["source_seam_failure"] * 2)
        self.assertTrue(all(w["diagnostic_pieces"] and not w["pieces"] for w in result["windows"]))

    def test_capacity_abstains_for_source_and_retains_complete_diagnostics(self):
        beats = list(range(10000, 298000, 24000)) + list(range(298000, 598000, 25000))
        with mock.patch.object(model, "MAXIMUM_PIECES", 1):
            result = model.construct(*self.fixture(beats))
        self.assertTrue(result["capacity_exceeded"])
        self.assertGreater(result["pieces_before_capacity"], 1)
        self.assertEqual(result["windows"][0]["status"], "source_capacity_exceeded")
        self.assertFalse(result["windows"][0]["pieces"])
        self.assertTrue(result["windows"][0]["diagnostic_pieces"])

    def test_actual_successor_is_unknown_at_terminal_and_across_abstention(self):
        truth = {"beat_frames": [0, 24000, 48000, 72000], "downbeat_frames": [0]}
        windows = [{"first_frame": 0, "end_frame": 48000}, {"first_frame": 48000, "end_frame": 72001}]
        points = [[(0, 0, 24000 * model.Q), (24000 * model.Q, 1, 24000 * model.Q)], []]
        score = evaluation.score_point_windows(points, truth, windows, True)
        self.assertEqual(score[0]["unknown_successor_points"], 1)
        points[1] = [(48000 * model.Q, 2, 24000 * model.Q), (72000 * model.Q, 3, 24000 * model.Q)]
        score = evaluation.score_point_windows(points, truth, windows, True)
        self.assertEqual(score[0]["unknown_successor_points"], 0)
        self.assertEqual(score[1]["unknown_successor_points"], 1)

    def test_meter_is_scored_at_candidate_position_with_frozen_logic(self):
        args = self.fixture()
        window = model.construct(*args)["windows"][0]
        truth = {"beat_frames": args[0], "downbeat_frames": args[1], "meter_numerator": 4, "meter_denominator": 4}
        score = evaluation.meter_score(window, truth)
        self.assertEqual(score["meter_correct"], 1)
        self.assertEqual(score["downbeat_correct"], 1)
        bad = deepcopy(window)
        bad["meter"]["downbeat_frame"] += 12000
        self.assertEqual(evaluation.meter_score(bad, truth)["downbeat_correct"], 0)
        with self.assertRaises(ValueError):
            model.validate(args[0], {"windows": [bad]})

    def test_corrupted_count_period_cell_and_ordinal_fail_independent_validation(self):
        args = self.fixture()
        result = model.construct(*args)
        for key, value in (("beat_count", 3), ("period_q32", 0), ("start_ordinal", 1), ("first_frame", 5000)):
            bad = deepcopy(result)
            bad["windows"][0]["pieces"][0][key] = value
            with self.assertRaises(ValueError):
                model.validate(args[0], bad)

    def test_plain_geometry_rejects_reference_fields(self):
        beats, db, windows, total = self.fixture()
        windows[0]["truth"] = {"tempo": 120000}
        with self.assertRaises(ValueError):
            model.construct(beats, db, windows, total)

    def test_constructor_has_only_fraction_import_and_performs_no_io(self):
        tree = ast.parse(Path(model.__file__).read_text())
        imports = [n for n in ast.walk(tree) if isinstance(n, (ast.Import, ast.ImportFrom))]
        self.assertEqual(len(imports), 1)
        self.assertEqual(imports[0].module, "fractions")
        args = self.fixture()
        expected = model.construct(*args)
        with mock.patch("builtins.open", side_effect=AssertionError("constructor must not read files")):
            self.assertEqual(model.construct(*args), expected)
        truth = {"beat_frames": args[0], "downbeat_frames": args[1], "meter_numerator": 4, "meter_denominator": 4}
        evaluation.score_point_windows(evaluation.candidate_points(expected), truth, args[2], False)
        truth["beat_frames"] = [p + 9000 for p in args[0]]
        evaluation.score_point_windows(evaluation.candidate_points(expected), truth, args[2], False)
        self.assertEqual(model.construct(*args), expected)

    def test_offset_translates_all_positions_and_preserves_periods(self):
        beats, db, windows, total = self.fixture()
        original = model.construct(beats, db, windows, total)
        shifted = model.construct([p + 123 for p in beats], [p + 123 for p in db],
                                  [{"first_frame": 0, "end_frame": total + 123}], total + 123)
        a, b = model.expand(original["windows"][0]["pieces"]), model.expand(shifted["windows"][0]["pieces"])
        self.assertEqual([(p + 123 * model.Q, o, t) for p, o, t in a], b)

    def test_invalid_caps_order_partition_and_gaps_fail_closed(self):
        for beats in ([1, 1], [-1, 24000], list(range(3073))):
            with self.assertRaises(ValueError):
                model.construct(beats, [], [{"first_frame": 0, "end_frame": 400000}], 400000)
        args = self.fixture()
        with self.assertRaises(ValueError):
            model.construct(args[0], args[1], [{"first_frame": 0, "end_frame": args[-1] - 1}], args[-1])
        with self.assertRaises(ValueError):
            model.construct(*args, gaps=[(20, 10)])
        with self.assertRaises(ValueError):
            model.construct(args[0], [123], args[2], args[-1])

    def test_protocol_digest_constants_and_zero_regression_gates(self):
        self.assertFalse(evaluation.protocol()["formal_acceptance"])
        with mock.patch.object(evaluation.ref.history, "sha256_file", return_value="0" * 64):
            with self.assertRaises(ValueError):
                evaluation.protocol()
        with mock.patch.object(model, "MAXIMUM_PIECES", 9):
            with self.assertRaises(ValueError):
                evaluation.protocol()
        totals = {"candidate_nominal": {"joint_windows": 188, "complete_sequence_windows": 179},
                  "candidate_actual": {"complete_sequence_windows": 150}, "ols_actual": {"complete_sequence_windows": 149}}
        changes = {k: {"joint_breaks": 0, "period_breaks": 0, "phase_breaks": 0, "meter_breaks": 0, "downbeat_breaks": 0} for k in ("ols", "coarse")}
        self.assertTrue(all(evaluation.gates(totals, changes).values()))
        changes["ols"]["phase_breaks"] = 1
        self.assertFalse(all(evaluation.gates(totals, changes).values()))


if __name__ == "__main__":
    unittest.main()
