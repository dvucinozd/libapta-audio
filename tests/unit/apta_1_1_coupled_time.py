#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated CT-01 tests; exact active-set oracle shares no solver arithmetic."""
import ast
from array import array
from copy import deepcopy
from fractions import Fraction
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import apta_coupled_time as model


def exact_objective(a, x):
    return (sum((x[i] - a[i]) ** 2 for i in range(len(a)))
            + 16 * sum((x[j - 1] - 2 * x[j] + x[j + 1]) ** 2
                       for j in range(1, len(a) - 1)))


def eliminate(matrix, rhs):
    """Exact dense Gaussian elimination, used only for N<=8."""
    n = len(rhs)
    rows = [[Fraction(v) for v in row] + [Fraction(rhs[i])]
            for i, row in enumerate(matrix)]
    for column in range(n):
        pivot = next(i for i in range(column, n) if rows[i][column])
        rows[column], rows[pivot] = rows[pivot], rows[column]
        divisor = rows[column][column]
        rows[column] = [v / divisor for v in rows[column]]
        for i in range(n):
            if i == column:
                continue
            multiplier = rows[i][column]
            if multiplier:
                rows[i] = [v - multiplier * p for v, p in zip(rows[i], rows[column])]
    return [row[-1] for row in rows]


def tiny_oracle(a, lower, upper):
    """Build I+16*D.T*D independently, enumerate all feasible active sets/KKT."""
    n = len(a)
    assert 1 <= n <= 8
    matrix = [[int(i == j) for j in range(n)] for i in range(n)]
    for center in range(1, n - 1):
        row = [0] * n
        row[center - 1:center + 2] = [1, -2, 1]
        for i in range(n):
            for j in range(n):
                matrix[i][j] += 16 * row[i] * row[j]
    # A fixed coordinate has one state; it has no one-sided gradient condition.
    choices = [(0,) if lower[i] == upper[i] else (-1, 0, 1) for i in range(n)]
    best = None
    for states in itertools.product(*choices):
        free = [i for i in range(n) if states[i] == 0 and lower[i] != upper[i]]
        fixed = [i for i in range(n) if i not in free]
        x = [Fraction(lower[i] if states[i] <= 0 else upper[i]) for i in range(n)]
        reduced = [[matrix[i][j] for j in free] for i in free]
        rhs = [Fraction(a[i]) - sum(matrix[i][j] * x[j] for j in fixed) for i in free]
        for i, value in zip(free, eliminate(reduced, rhs)):
            x[i] = value
        if any(not lower[i] <= x[i] <= upper[i] for i in range(n)):
            continue
        gradient = [2 * (sum(matrix[i][j] * x[j] for j in range(n)) - a[i])
                    for i in range(n)]
        if any((states[i] == 0 and gradient[i] != 0)
               or (states[i] == -1 and gradient[i] < 0)
               or (states[i] == 1 and gradient[i] > 0)
               for i in range(n) if lower[i] != upper[i]):
            continue
        objective = exact_objective(a, x)
        if best is None or objective < best[0]:
            best = (objective, x, gradient)
        elif objective == best[0]:
            assert x == best[1], "strictly convex objective has one exact minimizer"
    assert best is not None, "feasible box must have a KKT solution"
    return best


def whole(total):
    return [{"first_frame": 0, "end_frame": total}]


def fixture(a, total, windows=None, downbeats=None):
    return (a, list(range(0, len(a), 4)) if downbeats is None else downbeats,
            total, whole(total) if windows is None else windows)


class CoupledTimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.docs = ROOT / "docs/rust"
        cls.schema = json.loads((cls.docs / "COUPLED-TIME-CANDIDATE-SCHEMA.json").read_text())

    def assert_envelope(self, value):
        # Full Draft2020-12 validation is separate; keep routine tests stdlib-only.
        self.assertEqual(set(value), set(self.schema["required"]))
        for key in ("format", "hypothesis_name", "confidence", "confidence_calibrated",
                    "native_graph_constructed", "physical_p4", "formal_acceptance",
                    "production_promotion"):
            self.assertIs(type(value[key]), type(self.schema["properties"][key]["const"]))
            self.assertEqual(value[key], self.schema["properties"][key]["const"])
        self.assertEqual(value["protocol_sha256"], model.PROTOCOL_SHA256)
        for key in ("events", "solution"):
            self.assertEqual(set(value[key]), set(self.schema["properties"][key]["required"]))
        for window in value["windows"]:
            self.assertEqual(set(window), set(self.schema["properties"]["windows"]["items"]["required"]))

    def assert_candidate(self, arguments):
        a, downbeats, total, windows = arguments
        before = deepcopy(arguments)
        value = model.construct(*arguments)
        self.assertEqual(arguments, before)
        self.assert_envelope(value)
        self.assertEqual(value["status"], "candidate_events")
        self.assertEqual(value["input_event_count"], len(a))
        frames = value["events"]["beat_frames"]
        self.assertEqual(value["events"]["beat_ordinals"], list(range(len(a))))
        self.assertEqual(value["events"]["downbeat_ordinals"], list(downbeats))
        self.assertEqual(value["events"]["downbeat_frames"], [frames[i] for i in downbeats])
        self.assertTrue(all(type(v) is int and 0 <= v < total for v in frames))
        self.assertTrue(all(a < b for a, b in zip(frames, frames[1:])))
        solution = value["solution"]
        x = [float.fromhex(v) for v in solution["coordinates_hex"]]
        self.assertEqual([v.hex() for v in x], solution["coordinates_hex"])
        self.assertEqual(len(x), len(a))
        expected_lower, expected_upper = [], []
        for i, observed in enumerate(a):
            gaps = [abs(observed - a[j]) for j in (i - 1, i + 1) if 0 <= j < len(a)]
            radius = min(960, min(gaps) // 4)
            expected_lower.append(max(0, observed - radius))
            expected_upper.append(min(total - 1, observed + radius))
        self.assertEqual(solution["lower_bounds"], expected_lower)
        self.assertEqual(solution["upper_bounds"], expected_upper)
        for i, coordinate in enumerate(x):
            self.assertTrue(math.isfinite(coordinate))
            self.assertLessEqual(expected_lower[i], coordinate)
            self.assertLessEqual(coordinate, expected_upper[i])
            self.assertEqual(frames[i], math.floor(coordinate + 0.5))
        self.assertEqual(solution["active_lower_ordinals"],
                         [i for i in range(len(a)) if x[i] == expected_lower[i]])
        self.assertEqual(solution["active_upper_ordinals"],
                         [i for i in range(len(a)) if x[i] == expected_upper[i]])
        self.assertLessEqual(solution["residual_frames"], 1e-6)
        sweeps = solution["sweeps"]
        self.assertLessEqual(sweeps, 4096)
        self.assertEqual(solution["root_evaluations"], len(a) * (3 * sweeps + 1))
        self.assertEqual(solution["coordinate_updates"], 2 * len(a) * sweeps)
        # Exact evaluation at saved binary64 coordinates, not an alternate solver.
        rational_x = [Fraction(v) for v in x]
        expected_objective = float(exact_objective(a, rational_x))
        self.assertAlmostEqual(solution["objective_frames_squared"], expected_objective,
                               delta=1e-9 * max(1, expected_objective))
        self.assertLessEqual(expected_objective, float(exact_objective(a, a)) + 1e-6)
        ownership = []
        for expected, actual in zip(windows, value["windows"]):
            self.assertEqual(actual["first_frame"], expected["first_frame"])
            self.assertEqual(actual["end_frame"], expected["end_frame"])
            members = [i for i, v in enumerate(frames)
                       if expected["first_frame"] <= v < expected["end_frame"]]
            self.assertEqual(actual["beat_ordinals"], members)
            self.assertEqual(actual["status"], "candidate_events" if members else "no_events")
            ownership.extend(members)
        self.assertEqual(ownership, list(range(len(a))))
        changes = []
        for i in range(len(a)):
            observed = next(j for j, w in enumerate(windows) if w["first_frame"] <= a[i] < w["end_frame"])
            final = next(j for j, w in enumerate(windows) if w["first_frame"] <= frames[i] < w["end_frame"])
            if observed != final:
                changes.append({"ordinal": i, "observed_window": observed, "final_window": final})
        self.assertEqual(value["window_ownership_changes"], changes)
        self.assertEqual(value["known_pcm_gaps"], [])
        # Exercise strict JSON, including finite-number handling.
        self.assertEqual(json.loads(json.dumps(value, allow_nan=False)), value)
        return value

    def assert_oracle(self, a, lower, upper):
        before = deepcopy((a, lower, upper))
        solution = model.solve_times(a, lower, upper)
        self.assertEqual((a, lower, upper), before)
        objective, exact, gradient = tiny_oracle(a, lower, upper)
        coordinates = [float.fromhex(v) for v in solution["coordinates_hex"]]
        for i, actual in enumerate(coordinates):
            self.assertAlmostEqual(actual, float(exact[i]), delta=1e-3)
            self.assertLessEqual(lower[i], actual)
            self.assertLessEqual(actual, upper[i])
            if lower[i] != upper[i]:
                if exact[i] == lower[i]:
                    self.assertGreaterEqual(gradient[i], 0)
                elif exact[i] == upper[i]:
                    self.assertLessEqual(gradient[i], 0)
                else:
                    self.assertEqual(gradient[i], 0)
        reported = solution["objective_frames_squared"]
        exact_value = float(objective)
        self.assertLessEqual(reported - exact_value, 1e-3 * max(1, exact_value))
        self.assertGreaterEqual(reported - exact_value, -1e-9 * max(1, exact_value))
        return solution, exact, gradient

    def test_protocol_binding_and_constants(self):
        path = self.docs / "COUPLED-TIME-SYNTHETIC-PROTOCOL.json"
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), model.PROTOCOL_SHA256)
        protocol = json.loads(path.read_text())
        for name, digest in protocol["binding"].items():
            self.assertEqual(hashlib.sha256((self.docs / name).read_bytes()).hexdigest(), digest)
        self.assertEqual(model.MAXIMUM_SWEEPS, 4096)
        self.assertEqual(model.RESIDUAL_LIMIT, 1e-6)
        self.assertEqual(model.MAXIMUM_RADIUS, 960)
        self.assertEqual(model.MINIMUM_EVENTS, 9)
        self.assertEqual(model.MAXIMUM_EVENTS, 3072)
        self.assertEqual(model.MAXIMUM_SOURCE_FRAMES, 1440000)
        self.assertEqual(model.MAXIMUM_WINDOWS, 8)
        # CT04 now tests the runner's disabled real dispatch in separate processes.

    def test_g01_affine_initial_convergence(self):
        for period in (24000, 25000, 30240):
            with self.subTest(period=period):
                a = [1000 + period * i for i in range(16)]
                value = self.assert_candidate(fixture(a, 2000 + 16 * period))
                self.assertEqual(value["events"]["beat_frames"], a)
                self.assertEqual(value["solution"]["sweeps"], 0)
                self.assertEqual(value["solution"]["root_evaluations"], len(a))
                self.assertEqual(value["solution"]["objective_frames_squared"], 0.)

    def test_g02_alternating_offsets_final_intervals_and_phase(self):
        a = [1000 + 24000 * i + (-160 if i % 2 == 0 else 160) for i in range(16)]
        value = self.assert_candidate(fixture(a, 400000))
        frames = value["events"]["beat_frames"]
        self.assertTrue(all(abs(frames[i + 1] - frames[i] - 24000) <= 240 for i in range(15)))
        self.assertTrue(all(abs(frames[i] - (1000 + 24000 * i)) <= 2400 for i in range(16)))
        self.assert_oracle(a[:5], [max(0, v - 960) for v in a[:5]], [v + 960 for v in a[:5]])

    def test_g03_ramp_and_g04_abrupt_regions(self):
        ramp = [1000 + 23000 * i + 20 * i * (i - 1) for i in range(16)]
        step = [1000]
        for i in range(1, 16):
            step.append(step[-1] + (23000 if i <= 7 else 26000))
        for a, total in ((ramp, 400000), (step, 500000)):
            with self.subTest(kind="ramp" if a is ramp else "step"):
                value = self.assert_candidate(fixture(a, total))
                self.assertNotEqual(value["events"]["beat_frames"], a)
                tiny = a[5:11]
                self.assert_oracle(tiny, [v - 960 for v in tiny], [v + 960 for v in tiny])

    def test_g05_missing_and_g06_extra_events_are_not_repaired(self):
        missing = [1000 + 24000 * i for i in range(17) if i != 8]
        extra = [1000 + 24000 * i for i in range(16)]
        extra.insert(8, (extra[7] + extra[8]) // 2)
        for a, count in ((missing, 16), (extra, 17)):
            with self.subTest(count=count):
                value = self.assert_candidate(fixture(a, 500000, downbeats=[]))
                frames = value["events"]["beat_frames"]
                self.assertEqual(len(frames), count)
                # Bounded timing cannot repair topology; retain the wrong-rate gap.
                gaps = [b - a for a, b in zip(frames, frames[1:])]
                self.assertTrue(max(gaps) > 40000 if count == 16 else min(gaps) < 15000)

    def test_g07_half_and_double_rate_unchanged(self):
        for period in (48000, 12000):
            a = [1000 + period * i for i in range(16)]
            value = self.assert_candidate(fixture(a, 2000 + 16 * period))
            self.assertEqual(value["events"]["beat_frames"], a)

    def test_g08_cross_window_movement_and_downbeat_identity(self):
        a = [1000 + 24000 * i + (320 if i == 8 else 0) for i in range(17)]
        windows = [{"first_frame": 0, "end_frame": 193319},
                   {"first_frame": 193319, "end_frame": 500000}]
        value = self.assert_candidate(fixture(a, 500000, windows))
        self.assertEqual(value["window_ownership_changes"],
                         [{"ordinal": 8, "observed_window": 1, "final_window": 0}])
        self.assertIn(8, value["windows"][0]["beat_ordinals"])
        self.assertIn(8, value["events"]["downbeat_ordinals"])
        self.assertEqual(value["events"]["downbeat_frames"][2], value["events"]["beat_frames"][8])

    def test_g09_source_endpoints_and_empty_windows(self):
        value = self.assert_candidate(fixture([24000 * i for i in range(9)], 192001))
        self.assertEqual(value["solution"]["active_lower_ordinals"], [0])
        self.assertEqual(value["solution"]["active_upper_ordinals"], [8])
        self.assertEqual(value["events"]["beat_frames"][-1], 192000)
        windows = [{"first_frame": 0, "end_frame": 1},
                   {"first_frame": 1, "end_frame": 200000},
                   {"first_frame": 200000, "end_frame": 210000}]
        self.assertEqual(self.assert_candidate(fixture([24000 * i for i in range(9)], 210000,
                                                      windows))["windows"][-1]["status"], "no_events")

    def test_g10_one_frame_gaps_fixed_boxes(self):
        value = self.assert_candidate(fixture(list(range(9)), 9))
        for key in ("active_lower_ordinals", "active_upper_ordinals"):
            self.assertEqual(value["solution"][key], list(range(9)))
        self.assertEqual(value["solution"]["sweeps"], 0)

    def test_g11_translation_with_unclipped_boxes(self):
        for a in ([1000 + 25000 * i for i in range(16)],
                  [1000 + 24000 * i + (-160 if i % 2 == 0 else 160) for i in range(16)]):
            windows = [{"first_frame": 0, "end_frame": 180000},
                       {"first_frame": 180000, "end_frame": 400000}]
            original = self.assert_candidate(fixture(a, 400000, windows))
            shift = 524288
            shifted_windows = [{"first_frame": 0, "end_frame": 180000 + shift},
                               {"first_frame": 180000 + shift, "end_frame": 400000 + shift}]
            translated = self.assert_candidate(fixture([v + shift for v in a], 400000 + shift,
                                                       shifted_windows))
            x = [float.fromhex(v) for v in original["solution"]["coordinates_hex"]]
            y = [float.fromhex(v) for v in translated["solution"]["coordinates_hex"]]
            for before, after in zip(x, y):
                self.assertAlmostEqual(after - shift, before, delta=1e-3)
            if a[1] - a[0] == 25000:
                self.assertEqual([v - shift for v in translated["events"]["beat_frames"]],
                                 original["events"]["beat_frames"])

    def test_g12_exact_oracle_all_sizes(self):
        for n in range(1, 9):
            a = [1000 + 20 * i + (3 if i % 2 else -3) for i in range(n)]
            with self.subTest(n=n):
                self.assert_oracle(a, [v - 5 for v in a], [v + 5 for v in a])

    def test_g12_exact_oracle_fixed_and_active_bounds(self):
        cases = [([10], [10], [10]), ([10, 30], [0, 20], [20, 40]),
                 ([100, 101, 250], [99, 101, 249], [101, 101, 251]),
                 ([10, 90, 100], [10, 89, 99], [11, 91, 101]),
                 ([100, 130, 131, 160], [100, 100, 130, 150], [120, 140, 131, 160])]
        for a, lower, upper in cases:
            with self.subTest(a=a):
                self.assert_oracle(a, lower, upper)
        solution, _, _ = self.assert_oracle([100, 101, 250], [99, 101, 249], [101, 101, 251])
        self.assertEqual(solution["active_lower_ordinals"], [0, 1, 2])
        self.assertEqual(solution["active_upper_ordinals"], [1])

    def test_g12_seeded_irregular_oracle_boxes(self):
        rng = random.Random(20261023)
        for n in (3, 4, 5, 6):
            for case in range(3):
                a = [500]
                for _ in range(n - 1):
                    a.append(a[-1] + rng.randrange(1, 80))
                lower = [v - rng.randrange(0, 12) for v in a]
                upper = [v + rng.randrange(0, 12) for v in a]
                with self.subTest(n=n, case=case):
                    self.assert_oracle(a, lower, upper)

    def test_g13_machine_rounding_half_neighbors_and_signed_zero(self):
        for k in (0, 1, 1000, 524288, 1439998):
            half = k + 0.5
            for x in (math.nextafter(half, -math.inf), half, math.nextafter(half, math.inf)):
                shifted = x + 0.5
                # int is truncation here; all these generated shifted values are >=0.
                self.assertEqual(model._round_frame(x), int(shifted))
        self.assertEqual(model._round_frame(math.nextafter(0.5, -math.inf)), 1)
        self.assertEqual(model._round_frame(-0.), 0)
        self.assertEqual(model._round_frame(0.), 0)
        self.assertEqual(model.solve_times([0], [0], [0])["coordinates_hex"], ["0x0.0p+0"])
        # A minimizer equal to a bound retains its own sign bit at equality.
        self.assertEqual(model._root(0, [0.0], [0.0], [0.0], [0.0]).hex(), "0x0.0p+0")
        self.assertEqual(model._root(0, [-0.0], [0.0], [0.0], [0.0]).hex(), "-0x0.0p+0")

    def test_g14_full_capacity_jitter_and_g15_resource_shapes(self):
        windows = [{"first_frame": i * 180000, "end_frame": (i + 1) * 180000}
                   for i in range(8)]
        for kind, a in (
                ("jitter_bound_active", [1000 + 400 * i + (-80 if i % 2 == 0 else 80)
                                         for i in range(3072)]),
                ("affine", [1000 + 400 * i for i in range(3072)]),
                ("ramp", [1000 + 100 * i + i * (i - 1) // 10 for i in range(3072)])):
            with self.subTest(kind=kind):
                value = self.assert_candidate(fixture(a, 1440000, windows))
                self.assertLessEqual(value["solution"]["root_evaluations"], 37751808)
                self.assertLessEqual(value["solution"]["coordinate_updates"], 25165824)
                if kind == "jitter_bound_active":
                    self.assertTrue(value["solution"]["active_lower_ordinals"])
                    self.assertTrue(value["solution"]["active_upper_ordinals"])

    def test_g16_short_and_gap_abstention_precedence(self):
        for n in range(9):
            for gaps in ([], [(0, 1), (1, 2)]):
                args = fixture(list(range(n)), 10, [{"first_frame": 0, "end_frame": 5},
                                                  {"first_frame": 5, "end_frame": 10}])
                value = model.construct(*args, known_gaps=gaps)
                self.assert_envelope(value)
                expected = "known_pcm_gap" if gaps else "insufficient_events"
                self.assertEqual(value["status"], expected)
                self.assertEqual(value["input_event_count"], n)
                self.assertEqual(value["known_pcm_gaps"], [list(g) for g in gaps])
                self.assertTrue(all(w["status"] == expected for w in value["windows"]))
                self.assertTrue(all(not items for items in value["events"].values()))
                self.assertIsNone(value["solution"]["objective_frames_squared"])
                self.assertEqual(value["solution"]["root_evaluations"], 0)
        args = fixture([1000 + 24000 * i for i in range(9)], 220000)
        with mock.patch.object(model, "solve_times", side_effect=AssertionError("must abstain")):
            self.assertEqual(model.construct(*args, known_gaps=[(10, 11)])["status"], "known_pcm_gap")

    def test_g16_invalid_inputs_validate_before_abstention(self):
        good = fixture([1000 + 24000 * i for i in range(9)], 220000)
        invalid = []
        for frames in (None, {}, iter([1, 2]), [True], [1.0], [-1], [220000], [1, 1],
                       [2, 1], list(range(3073)), array("d", [1.0])):
            invalid.append((frames, [], 220000, whole(220000), [(10, 11)]))
        for ordinals in (None, [False], [0.0], [-1], [9], [0, 0], [1, 0]):
            invalid.append((good[0], ordinals, good[2], good[3], [(10, 11)]))
        for total in (True, 220000., 0, -1, 1440001):
            invalid.append(([], [], total, whole(total), []))
        bad_windows = [[], whole(220000) * 9, [{"first_frame": True, "end_frame": 220000}],
                       [{"first_frame": 0., "end_frame": 220000}],
                       [{"first_frame": 1, "end_frame": 220000}], whole(219999),
                       [{"first_frame": 0, "end_frame": 220001}],
                       [{"first_frame": 0, "end_frame": 220000, "truth": 4}],
                       [{"first_frame": 0, "end_frame": 10}, {"first_frame": 9, "end_frame": 220000}],
                       [{"first_frame": 0, "end_frame": 10}, {"first_frame": 11, "end_frame": 220000}]]
        for windows in bad_windows:
            invalid.append(([], [], 220000, windows, []))
        for gaps in (None, {}, [(0, 0)], [(-1, 1)], [(0, 220001)], [(False, 1)],
                     [(0., 1)], [(0, 2), (1, 3)], [(2, 3), (0, 1)], [[0]], [(0, 1, 2)]):
            invalid.append(([], [], 220000, whole(220000), gaps))
        for index, arguments in enumerate(invalid):
            with self.subTest(index=index):
                with self.assertRaisesRegex(ValueError, "^invalid_input$"):
                    model.construct(*arguments)
        with self.assertRaises(TypeError):
            model.construct(*good, truth=[])

    def test_solver_rejects_invalid_toy_boxes_and_types(self):
        for args in (([], [], []), ([1], [], [2]), ([1], [0], []), ([1], [True], [2]),
                     ([1], [0.], [2]), ([1], [0], [2.]), ([1], [2], [3]),
                     ([1], [0], [0]), ([1], [-1], [2]), ([1], [0], [1440000]),
                     ([1, 1], [0, 0], [2, 2]), ([0.], [0], [1]), ([False], [0], [1])):
            with self.subTest(args=args), self.assertRaisesRegex(ValueError, "^invalid_input$"):
                model.solve_times(*args)

    def test_g17_nonfinite_and_nonconvergence_have_no_fallback(self):
        arguments = fixture([1000 + 24000 * i + (160 if i % 2 else -160)
                             for i in range(16)], 400000)
        for value in (math.nan, math.inf, -math.inf):
            with mock.patch.object(model, "_root", return_value=value):
                with self.assertRaisesRegex(ValueError, "^nonfinite_numeric$"):
                    model.construct(*arguments)
        original_root = model._root
        calls = 0

        def fail_on_first_update(*args):
            nonlocal calls
            calls += 1
            return math.nan if calls == 17 else original_root(*args)

        with mock.patch.object(model, "_root", side_effect=fail_on_first_update):
            with self.assertRaisesRegex(ValueError, "^nonfinite_numeric$"):
                model.construct(*arguments)
        with mock.patch.object(model, "MAXIMUM_SWEEPS", 0):
            with self.assertRaisesRegex(ValueError, "^nonconvergence$"):
                model.construct(*arguments)
        # Exercise the actual unmodified 4096 bound with a test-only residual stub.
        with mock.patch.object(model, "_residual", return_value=1.) as residual:
            with self.assertRaisesRegex(ValueError, "^nonconvergence$"):
                model.solve_times([0], [0], [0])
            self.assertEqual(residual.call_count, 4097)
        with mock.patch.object(model, "_round_frame", return_value=1440000):
            with self.assertRaisesRegex(ValueError, "^invalid_output$"):
                model.construct(*arguments)
        for error in (MemoryError, OSError):
            with mock.patch.object(model, "solve_times", side_effect=error("test")):
                with self.assertRaises(error):
                    model.construct(*arguments)
        with mock.patch.object(model.sys, "float_info", mant_dig=24, max_exp=128):
            with self.assertRaisesRegex(ValueError, "^unsupported_float64_runtime$"):
                model.construct(*arguments)

    def test_g17_nonfinite_arithmetic_helpers(self):
        for value in (math.nan, math.inf, -math.inf):
            for call in (lambda: model._objective([0.], [value]),
                         lambda: model._round_frame(value),
                         lambda: model._root(1, [0., 1., 2.], [value, 1., 2.], [0.] * 3, [3.] * 3)):
                with self.assertRaisesRegex(ValueError, "^nonfinite_numeric$"):
                    call()

    def test_residual_inclusive_limit_and_no_fixed_box_shortcut(self):
        with mock.patch.object(model, "_residual", return_value=1e-6):
            value = model.solve_times([0], [0], [0])
            self.assertEqual(value["sweeps"], 0)
        with mock.patch.object(model, "_residual", side_effect=[math.nextafter(1e-6, math.inf), 1e-6]), \
                mock.patch.object(model, "_root", wraps=model._root) as roots:
            value = model.solve_times([0], [0], [0])
            self.assertEqual(value["sweeps"], 1)
            self.assertEqual(roots.call_count, 2)  # Both updates run even for L==U.

    def test_exact_root_call_count_order_and_repeatability(self):
        a = [100, 101, 150]
        with mock.patch.object(model, "_root", wraps=model._root) as roots:
            solution = model.solve_times(a, [99, 100, 149], [101, 102, 151])
        order = [call.args[0] for call in roots.call_args_list]
        self.assertEqual(len(order), solution["root_evaluations"])
        self.assertEqual(order, list(range(3)) +
                         (list(range(3)) + list(reversed(range(3))) + list(range(3))) * solution["sweeps"])
        args = fixture([1000 + 24000 * i + (320 if i == 8 else 0) for i in range(17)], 500000)
        first = model.construct(*args)
        self.assertEqual(json.dumps(first, sort_keys=True, allow_nan=False),
                         json.dumps(model.construct(*args), sort_keys=True, allow_nan=False))
        self.assertEqual(model.solve_times(array("i", [0, 1]), (0, 0), array("i", [2, 3]))["sweeps"], 0)

    def test_g18_import_purity_no_io_and_fresh_containers(self):
        path = ROOT / "tools/apta_coupled_time.py"
        source = path.read_text()
        tree = ast.parse(source)
        allowed = {"array", "math", "sys"}
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                self.assertTrue(all(alias.name in allowed for alias in node.names))
            if isinstance(node, ast.ImportFrom):
                self.assertIn(node.module, allowed)
            if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id == "sys":
                self.assertEqual(node.attr, "float_info")
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                self.assertNotIn(node.func.id, {"open", "print", "eval", "exec", "__import__", "input"})
        compiled = compile(source, str(path), "exec")
        args = fixture(array("i", [1000 + 24000 * i for i in range(9)]), 220000)
        before = deepcopy(args)
        with mock.patch("builtins.open", side_effect=AssertionError("no I/O")), \
                mock.patch("builtins.print", side_effect=AssertionError("no stdout")):
            namespace = {}
            exec(compiled, namespace)
            first = namespace["construct"](*args)
        second = model.construct(*args)
        first["events"]["beat_frames"][0] = -1
        first["events"]["downbeat_ordinals"].append(100)
        first["windows"][0]["end_frame"] = -1
        first["solution"]["lower_bounds"][0] = -1
        self.assertEqual(args, before)
        self.assertEqual(second, model.construct(*args))
        gaps = [[0, 1], [1, 2]]
        result = model.construct([], [], 10, whole(10), gaps)
        result["known_pcm_gaps"][0][0] = 5
        self.assertEqual(gaps, [[0, 1], [1, 2]])
        solution = model.solve_times([10], [9], [11])
        solution["lower_bounds"][0] = -1
        self.assertEqual(model.solve_times([10], [9], [11])["lower_bounds"], [9])


if __name__ == "__main__":
    unittest.main()
