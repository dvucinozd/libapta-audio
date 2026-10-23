#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated-only checks with exhaustive paths independent of the production DP."""
import ast
from copy import deepcopy
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import random
import struct
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import apta_dense_decoder as model


def exhaustive(qb, qd, total, meter):
    """Enumerate complete paths, not states; no production helpers or tie code."""
    best = None

    def visit(path, score):
        nonlocal best
        t, d, k = path[-1]
        if (t + d) * 960 >= total:
            # Terminal tie first; then predecessor incoming intervals backwards.
            # Each equal-score state's recurrence chooses its smallest parent p.
            key = (-score, t, d, k, tuple(s[1] for s in path[-2::-1]))
            if best is None or key < best[0]:
                best = (key, list(path), score)
        for step in range(max(10, d - 2), min(75, d + 2) + 1):
            row = t + step
            if row >= len(qb):
                continue
            phase = (k + 1) % meter
            value = score + qb[row] + (qd[row] if phase == 0 else 0) - 2048 * (step - d) ** 2
            visit(path + [(row, step, phase)], value)

    for row in range(len(qb)):
        for latent in range(max(10, row + 1), 76):
            for phase in range(meter):
                visit([(row, latent, phase)], qb[row] + (qd[row] if phase == 0 else 0))
    if best is None:
        raise AssertionError("enumerator found no terminal")
    return {"terminal_score": best[2],
            "terminal_state": dict(zip(("row", "interval_rows", "phase"), best[1][-1])),
            "beat_rows": [s[0] for s in best[1]],
            "incoming_intervals": [s[1] for s in best[1]],
            "beat_phases": [s[2] for s in best[1]]}


def pulse_fixture(meter=4, length=400, positions=None, windows=None):
    positions = list(range(10, length, 25)) if positions is None else positions
    beat, downbeat = [-8.] * (length + 1), [-8.] * (length + 1)
    for i, row in enumerate(positions):
        beat[row] = 8.
        if i % meter == 0:
            downbeat[row] = 8.
    total = length * 960
    return beat, downbeat, total, windows or [{"first_frame": 0, "end_frame": total}]


class DenseDecoderTests(unittest.TestCase):
    def assert_oracle(self, qb, qd, meter):
        total = len(qb) * 960
        actual = model.solve_meter(qb, qd, total, meter)
        expected = exhaustive(qb, qd, total, meter)
        for key, value in expected.items():
            self.assertEqual(actual[key], value, (meter, key, qb, qd))
        self.assertEqual(model.path_score(qb, qd, total, meter, actual["beat_rows"],
                                         actual["incoming_intervals"], actual["beat_phases"]),
                         actual["terminal_score"])
        return actual

    def test_quantization_exact_half_ties_away_from_zero(self):
        for x, q in [(0., 0), (-0., 0), (1/2048, 1), (-1/2048, -1),
                     (3/2048, 2), (-3/2048, -2), (1., 1024), (-1., -1024)]:
            self.assertEqual(model.quantize(x), q)

    def test_quantization_preserves_extremes_subnormals_and_clips(self):
        tiny = struct.unpack("<f", struct.pack("<I", 1))[0]
        largest = struct.unpack("<f", struct.pack("<I", 0x7f7fffff))[0]
        for x in (tiny, -tiny):
            self.assertEqual(model.quantize(x), 0)
        for x in (32., 1000., largest):
            self.assertEqual(model.quantize(x), 32768)
            self.assertEqual(model.quantize(-x), -32768)

    def test_invalid_nonfinite_and_non_f32_inputs_rejected(self):
        for x in (float("nan"), float("inf"), -float("inf"), 1e100, 1/3,
                  True, "1", None, {}, 10**500):
            with self.subTest(value=type(x).__name__), self.assertRaises(ValueError):
                model.quantize(x)

    def test_half_up_source_frame_rounding(self):
        for x, expected in [(Fraction(1, 2), 1), (Fraction(-1, 2), 0),
                            (Fraction(5, 2), 3), (Fraction(249, 100), 2)]:
            self.assertEqual(model.round_frame(x), expected)

    def test_exhaustive_null_and_flat_ties_both_meters(self):
        for n in (1, 10, 11, 20, 24):
            for meter in (3, 4):
                self.assert_oracle([0] * n, [0] * n, meter)

    def test_exhaustive_signed_competing_emissions_both_meters(self):
        rng = random.Random(20261023)
        for n in (12, 21, 24):
            qb = [rng.randrange(-8, 9) * 1024 for _ in range(n)]
            qd = [rng.randrange(-8, 9) * 1024 for _ in range(n)]
            for meter in (3, 4):
                self.assert_oracle(qb, qd, meter)

    def test_manual_three_beat_change_cost_and_seed_interval(self):
        qb, qd = [-32768] * 24, [0] * 24
        for t in (0, 10, 21):
            qb[t] = 32768
        result = self.assert_oracle(qb, qd, 3)
        self.assertEqual(result["beat_rows"], [0, 10, 21])
        self.assertEqual(result["incoming_intervals"], [10, 10, 11])
        self.assertEqual(result["terminal_score"], 3 * 32768 - 2048)

    def test_stronger_local_decoy_loses_to_whole_path(self):
        qb, qd = [-32768] * 24, [0] * 24
        for t in (0, 10, 20):
            qb[t] = 20480
        qb[9] = 32768
        result = self.assert_oracle(qb, qd, 4)
        self.assertEqual(result["beat_rows"], [0, 10, 20])
        self.assertNotIn(9, result["beat_rows"])

    def test_equal_meter_score_selects_three_without_margin_gate(self):
        args = list(pulse_fixture())
        args[1] = [0.] * len(args[1])
        result = model.construct(*args)
        a, b = result["per_meter_diagnostics"]
        self.assertEqual(a["terminal_score"], b["terminal_score"])
        self.assertEqual(result["selected_meter"], 3)
        self.assertEqual(result["score_difference_4_minus_3"], 0)
        self.assertEqual(result["status"], "candidate_events")

    def test_exact_four_four_pulses_and_same_sequence_downbeats(self):
        args = pulse_fixture(4)
        result = model.construct(*args)
        expected = list(range(10, 400, 25))
        self.assertEqual(result["status"], "candidate_events")
        self.assertEqual(result["selected_meter"], 4)
        self.assertEqual(result["events"]["beat_rows"], expected)
        self.assertEqual(result["events"]["beat_frames"], [i * 960 for i in expected])
        self.assertEqual(result["events"]["beat_phases"], [i % 4 for i in range(len(expected))])
        self.assertEqual(result["events"]["downbeat_frames"], [i * 960 for i in expected[::4]])

    def test_exact_three_four_pulses(self):
        args = pulse_fixture(3)
        result = model.construct(*args)
        self.assertEqual(result["selected_meter"], 3)
        self.assertEqual(result["events"]["beat_rows"], list(range(10, 400, 25)))
        self.assertEqual(result["events"]["downbeat_frames"], result["events"]["beat_frames"][::3])

    def test_variable_timing_is_not_forced_to_constant_period(self):
        gaps = [25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 29, 29, 28]
        rows = [10]
        for d in gaps:
            rows.append(rows[-1] + d)
        args = pulse_fixture(4, length=rows[-1] + 20, positions=rows)
        result = model.construct(*args)
        self.assertEqual(result["status"], "candidate_events")
        self.assertEqual(result["events"]["beat_rows"], rows)
        self.assertEqual(result["events"]["beat_phases"], [i % 4 for i in range(len(rows))])
        frames = result["events"]["beat_frames"]
        self.assertEqual([b - a for a, b in zip(frames, frames[1:])], [d * 960 for d in gaps])

    def test_nonpositive_path_has_empty_events_and_preserves_windows(self):
        total = 800 * 960
        windows = [{"first_frame": 0, "end_frame": total // 2},
                   {"first_frame": total // 2, "end_frame": total}]
        for x in (0., -8., -1000.):
            result = model.construct([x] * 801, [x] * 801, total, windows)
            self.assertEqual(result["status"], "nonpositive_path")
            self.assertTrue(all(not v for v in result["events"].values()))
            self.assertEqual([w["status"] for w in result["windows"]], ["nonpositive_path"] * 2)
            self.assertEqual(len(result["windows"]), 2)
            self.assertEqual(result["confidence"], 0)
            self.assertFalse(result["confidence_calibrated"])

    def test_positive_winner_with_fewer_than_nine_events_abstains(self):
        result = model.construct(*pulse_fixture(length=180))
        self.assertGreater(result["terminal_score"], 0)
        self.assertEqual(result["status"], "insufficient_events")
        self.assertFalse(result["events"]["beat_frames"])
        self.assertTrue(result["per_meter_diagnostics"])

    def test_exact_nine_event_minimum_is_supported(self):
        result = model.construct(*pulse_fixture(length=230))
        self.assertEqual(result["status"], "candidate_events")
        self.assertEqual(len(result["events"]["beat_frames"]), 9)

    def test_minimum_does_not_rescue_with_other_meter(self):
        original = model.solve_meter
        short = original([1] * 24, [0] * 24, 24 * 960, 3)
        other = deepcopy(short)
        other.update(meter=4, terminal_score=short["terminal_score"] - 1,
                     beat_rows=list(range(9)), beat_phases=[0] * 9)
        with mock.patch.object(model, "solve_meter", side_effect=[short, other]):
            result = model.construct([1.] * 25, [0.] * 25, 24 * 960,
                                     [{"first_frame": 0, "end_frame": 24 * 960}])
        self.assertEqual(result["status"], "insufficient_events")
        self.assertEqual(result["selected_meter"], 3)
        self.assertFalse(result["events"]["beat_frames"])

    def test_endpoint_row_never_emitted_or_used_as_refinement_neighbor(self):
        args = list(pulse_fixture())
        args[0][-1] = args[1][-1] = 32.
        before = model.construct(*pulse_fixture())
        result = model.construct(*args)
        self.assertEqual(result["events"], before["events"])
        self.assertEqual(result["boundary_rows_excluded"], [400])
        self.assertEqual(model.localize([0, 8192], 1), 960)
        self.assertEqual(model.localize([8192, 4096], 0), 0)

    def test_last_in_domain_peak_never_uses_boundary_neighbor(self):
        args = list(pulse_fixture(4, positions=list(range(24, 400, 25))))
        args[0][-1] = 4.
        result = model.construct(*args)
        self.assertEqual(result["events"]["beat_rows"][-1], 399)
        self.assertEqual(result["events"]["beat_frames"][-1], 399 * 960)

    def test_beyond_source_center_and_clipped_raw_endpoint_are_retained_as_metadata(self):
        args = list(pulse_fixture())
        args[2] -= 1
        args[3] = [{"first_frame": 0, "end_frame": args[2]}]
        args[0][-1] = 1000.
        result = model.construct(*args)
        self.assertEqual(result["boundary_rows_excluded"], [400])
        self.assertGreater(400 * 960, args[2])
        self.assertEqual(result["clipped_logits"], {"beat": 1, "downbeat": 0})
        self.assertTrue(all(p < args[2] for p in result["events"]["beat_frames"]))

    def test_parabolic_localization_uses_quantized_beat_only(self):
        self.assertEqual(model.localize([0, 8192, 4096], 1), 1120)
        self.assertEqual(model.localize([4096, 8192, 0], 1), 800)
        self.assertEqual(model.localize([0, 8192, 0], 1), 960)
        self.assertEqual(model.localize([8192, 8192, 0], 1), 960)
        with self.assertRaises(ValueError):
            model.localize([0, 1], 2)

    def test_refined_event_changes_window_once_and_retains_ordinal(self):
        split = 10 * 960 + 100
        args = list(pulse_fixture(windows=[{"first_frame": 0, "end_frame": split},
                                          {"first_frame": split, "end_frame": 400 * 960}]))
        args[0][9], args[0][11] = 0., 4.
        result = model.construct(*args)
        self.assertEqual(result["events"]["beat_frames"][0], 10 * 960 + 160)
        self.assertEqual(result["windows"][0]["beat_ordinals"], [])
        self.assertEqual(result["windows"][0]["status"], "no_events")
        self.assertEqual(result["windows"][1]["beat_ordinals"][0], 0)
        self.assertEqual(result["events"]["downbeat_frames"][0], result["events"]["beat_frames"][0])
        self.assertEqual(result["window_ownership_changes"],
                         [{"ordinal": 0, "row_window": 0, "refined_window": 1}])

    def test_known_gap_abstains_for_whole_source_without_dp(self):
        args = pulse_fixture()
        with mock.patch.object(model, "solve_meter", side_effect=AssertionError("no path through gaps")):
            result = model.construct(*args, known_gaps=[(10000, 11000)])
        self.assertEqual(result["status"], "known_pcm_gap")
        self.assertEqual(result["known_pcm_gaps"], [[10000, 11000]])
        self.assertEqual(result["state_cells"], 0)
        self.assertFalse(result["events"]["beat_frames"])

    def test_known_gap_in_one_window_abstains_for_all_windows(self):
        args = list(pulse_fixture())
        args[3] = [{"first_frame": 0, "end_frame": 200 * 960},
                   {"first_frame": 200 * 960, "end_frame": 400 * 960}]
        result = model.construct(*args, known_gaps=[(10000, 11000)])
        self.assertEqual([w["status"] for w in result["windows"]], ["known_pcm_gap"] * 2)
        self.assertFalse(result["events"]["beat_frames"])

    def test_malformed_gaps_fail_before_abstention(self):
        args = pulse_fixture()
        for gaps in (None, [(-1, 2)], [(10, 10)], [(0, args[2]+1)], [(0, 2), (1, 3)],
                     [(False, 1)], [[0]], [[0, 1, 2]]):
            with self.subTest(gaps=gaps), self.assertRaises(ValueError):
                model.construct(*args, known_gaps=gaps)

    def test_geometry_rejects_labels_noninteger_holes_overlaps_and_capacity(self):
        args = pulse_fixture()
        for windows in ([{"first_frame": 0, "end_frame": args[2], "truth": 4}], [],
                        [{"first_frame": 1, "end_frame": args[2]}],
                        [{"first_frame": False, "end_frame": args[2]}],
                        [{"first_frame": 0, "end_frame": args[2] - 1}],
                        [{"first_frame": 0, "end_frame": 100}, {"first_frame": 99, "end_frame": args[2]}],
                        [{"first_frame": i, "end_frame": i+1} for i in range(9)]):
            with self.subTest(windows=windows), self.assertRaises(ValueError):
                model.construct(*args[:3], windows)

    def test_bad_source_row_channel_shapes_and_boolean_geometry_rejected(self):
        windows = [{"first_frame": 0, "end_frame": 400*960}]
        for beat, db, total in (([], [], 0), ([0.], [0., 0.], 1), ([0.] * 1502, [0.] * 1502, 1440001),
                                ([0.], [0.], True), ([0.] * 400, [0.] * 400, 400 * 960),
                                ([0.] * 401, [0.] * 401, 1)):
            with self.subTest(total=total), self.assertRaises(ValueError):
                model.construct(beat, db, total, windows)

    def test_short_capture_geometries_match_expected_counts(self):
        for frames, rows in ((1436735, 1497), (1439661, 1500), (1440000, 1501), (1, 1)):
            windows = [{"first_frame": 0, "end_frame": frames}]
            domain = model.geometry(frames, rows, windows, ())
            self.assertEqual(domain, (frames + 959) // 960)
        with self.assertRaises(ValueError):
            model.geometry(1436735, 1498, [{"first_frame": 0, "end_frame": 1436735}], ())

    def test_quantized_solver_rejects_bad_domain_and_out_of_range_values(self):
        for qb, qd, total, meter in (([0], [0], 960, 5), ([0], [0], 961, 3),
                                     ([32769], [0], 960, 3), ([True], [0], 960, 3),
                                     ([0], [], 960, 3)):
            with self.assertRaises(ValueError):
                model.solve_meter(qb, qd, total, meter)

    def test_path_replay_rejects_bad_seed_phase_interval_and_terminal(self):
        qb, qd, total = [32768] * 24, [0] * 24, 24 * 960
        good = model.solve_meter(qb, qd, total, 3)
        rows, intervals, phases = [good[k] for k in ("beat_rows", "incoming_intervals", "beat_phases")]
        mutations = [(rows, intervals, [3] * len(phases)),
                     (rows, [9] * len(intervals), phases),
                     ([10], [10], [0]), ([0], [10], [0]),
                     (rows[::-1], intervals, phases), (rows, intervals[:-1], phases)]
        for r, d, k in mutations:
            with self.assertRaises(ValueError):
                model.path_score(qb, qd, total, 3, r, d, k)

    def test_localization_corruption_is_integrity_error_not_missing_support(self):
        with mock.patch.object(model, "localize", return_value=-1), self.assertRaises(ValueError):
            model.construct(*pulse_fixture())

    def test_constructor_is_pure_and_does_not_mutate_inputs(self):
        args = pulse_fixture()
        original = deepcopy(args)
        expected = model.construct(*args)
        tree = ast.parse(Path(model.__file__).read_text(encoding="utf-8"))
        names = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                names.extend(a.name for a in node.names)
            elif isinstance(node, ast.ImportFrom):
                names.append(node.module)
        self.assertEqual(set(names), {"array", "fractions", "math"})
        with mock.patch("builtins.open", side_effect=AssertionError("no constructor I/O")):
            self.assertEqual(model.construct(*args), expected)
        self.assertEqual(args, original)
        self.assertFalse(expected["native_graph_constructed"])
        self.assertFalse(expected["production_promotion"])

    def test_full_size_state_bytes_and_transition_bound(self):
        args = pulse_fixture(length=1500)
        result = model.construct(*args)
        self.assertEqual(result["status"], "candidate_events")
        self.assertEqual(result["state_cells"], 1500 * 66 * 7)
        self.assertEqual(result["maximum_live_state_bytes"], 1500 * 66 * 4 * 9)
        self.assertLessEqual(result["maximum_live_state_bytes"], 3566376)
        self.assertLessEqual(result["transitions_examined"], 1501 * 7 * 324)

    def test_registered_protocol_hash_and_every_constructor_constant_match(self):
        p = ROOT / "docs/rust/DENSE-DECODER-PROTOCOL.json"
        self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(), model.PROTOCOL_SHA256)
        config = json.loads(p.read_text())
        for key, value in (("name", model.NAME), ("source_sample_rate", model.RATE),
                           ("source_hop_frames", model.HOP), ("meters", list(model.METERS)),
                           ("minimum_interval_rows", model.MINIMUM_INTERVAL),
                           ("maximum_interval_rows", model.MAXIMUM_INTERVAL),
                           ("maximum_interval_change_rows", model.MAXIMUM_INTERVAL_CHANGE),
                           ("logit_clip_abs", model.LOGIT_CLIP), ("logit_scale", model.LOGIT_SCALE),
                           ("transition_penalty_per_squared_row", model.TRANSITION_PENALTY),
                           ("minimum_winning_path_events", model.MINIMUM_EVENTS)):
            self.assertEqual(config["decoder"][key], value)
        for key, value in (("maximum_source_frames", model.MAXIMUM_SOURCE_FRAMES),
                           ("maximum_rows", model.MAXIMUM_ROWS), ("maximum_windows", model.MAXIMUM_WINDOWS),
                           ("maximum_events", model.MAXIMUM_EVENTS),
                           ("maximum_single_meter_state_bytes", model.MAXIMUM_STATE_BYTES)):
            self.assertEqual(config["limits"][key], value)
        self.assertEqual(config["construction_contract"]["output_format"], model.FORMAT)
        self.assertEqual(config["construction_contract"]["unreachable_score"], model.UNREACHABLE)
        self.assertEqual(config["construction_contract"]["parent_encoding"]["seed"], model.SEED)
        self.assertEqual(config["construction_contract"]["parent_encoding"]["unreachable"],
                         model.PARENT_UNREACHABLE)


if __name__ == "__main__":
    unittest.main()
