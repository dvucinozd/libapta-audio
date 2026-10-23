#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CT-02 independent replay, corruption rejection and Linux watchdog checks."""
import ast
from copy import deepcopy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import apta_coupled_time as model
import apta_coupled_time_verify as verifier
import apta_coupled_time_preflight as preflight
import apta_dense_runner_support as io


def wire(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()


def sha(payload):
    return hashlib.sha256(payload).hexdigest()


class VerificationTests(unittest.TestCase):
    corruptions_checked = 0

    def test_registered_generated_cases_exactly_replay(self):
        previous = json.loads((ROOT / "docs/rust/COUPLED-TIME-CT01-20261023.json").read_text())
        old = {row["generated_fixture"]: row for row in previous["generated_full_capacity_arithmetic_cases"]}
        for name in preflight.CASES:
            a, d, total, windows, gaps = preflight.generated(name)
            with self.subTest(case=name):
                saved = model.construct(a, d, total, windows, gaps)
                before = deepcopy((a, d, windows, gaps, saved))
                expected = verifier.verify(a, d, total, windows, saved, gaps)
                self.assertEqual(wire(saved), wire(expected))
                verifier.verify_bytes(a, d, total, windows, wire(saved), sha(wire(saved)), gaps)
                self.assertEqual((a, d, windows, gaps, saved), before)
                if name.startswith("full_"):
                    key = {"full_affine": "affine", "full_ramp": "ramp",
                           "full_bound_active": "jitter_bound_active"}[name]
                    self.assertEqual(sha(wire(saved)), old[key]["canonical_candidate_sha256"])

    def test_tiny_exact_oracle_without_shared_arithmetic(self):
        spec = importlib.util.spec_from_file_location("ct01_oracle", Path(__file__).with_name("apta_1_1_coupled_time.py"))
        fixtures = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixtures)
        for n in range(1, 9):
            a = [1000 + 20 * i + (3 if i % 2 else -3) for i in range(n)]
            lower = [v if i % 2 == 0 else v - 5 for i, v in enumerate(a)]
            upper = [v if i % 2 == 0 else v + 5 for i, v in enumerate(a)]
            expected = fixtures.tiny_oracle(a, lower, upper)
            actual = verifier.replay_solution(a, lower, upper)
            for x, y in zip(actual["coordinates_hex"], expected[1]):
                self.assertAlmostEqual(float.fromhex(x), float(y), delta=1e-3)
            objective = float(expected[0])
            self.assertLessEqual(actual["objective_frames_squared"] - objective, 1e-3 * max(1, objective))
            self.assertGreaterEqual(actual["objective_frames_squared"] - objective, -1e-9 * max(1, objective))

    def test_every_saved_leaf_and_container_corruption_rejected(self):
        a, d, total, windows, gaps = preflight.generated("affine25000")
        saved = model.construct(a, d, total, windows, gaps)
        paths = []

        def visit(value, path):
            paths.append(path)
            if type(value) is dict:
                for key, item in value.items():
                    visit(item, path + (key,))
            elif type(value) is list:
                for index, item in enumerate(value):
                    visit(item, path + (index,))

        visit(saved, ())
        for path in paths:
            bad = deepcopy(saved)
            node = bad
            for key in path[:-1]:
                node = node[key]
            value = node[path[-1]] if path else node
            if type(value) is dict:
                value["unexpected"] = 0
            elif type(value) is list:
                value.append(None)
            else:
                if type(value) is bool:
                    changed = not value
                elif type(value) is int:
                    changed = value + 1
                elif type(value) is float:
                    changed = math.nextafter(value, math.inf)
                elif type(value) is str:
                    changed = value + "x"
                else:
                    changed = 0
                node[path[-1]] = changed
            with self.subTest(path=path), self.assertRaises(ValueError):
                verifier.verify(a, d, total, windows, bad, gaps)
            type(self).corruptions_checked += 1
        self.assertGreater(type(self).corruptions_checked, 100)

    def test_integer_lookalikes_signed_zero_and_noncanonical_coordinates_rejected(self):
        a, d, total, windows, gaps = preflight.generated("affine25000")
        saved = model.construct(a, d, total, windows, gaps)
        edits = [(('confidence',), False), (('confidence',), 0.),
                 (('physical_p4',), 0), (('input_event_count',), 16.),
                 (('events', 'beat_ordinals', 0), False),
                 (('solution', 'sweeps'), False), (('solution', 'residual_frames'), -0.),
                 (('solution', 'objective_frames_squared'), math.inf),
                 (('solution', 'coordinates_hex', 0), '1000'),
                 (('solution', 'coordinates_hex', 0), float(1000).hex().upper()),
                 (('solution', 'coordinates_hex', 0), 'nan'),
                 (('solution', 'lower_bounds'), tuple(saved['solution']['lower_bounds']))]
        for path, value in edits:
            bad = deepcopy(saved)
            target = bad
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(ValueError):
                verifier.verify(a, d, total, windows, bad, gaps)

    def test_strict_json_hash_and_exact_wire_bytes(self):
        a, d, total, windows, gaps = preflight.generated("fixed")
        saved = model.construct(a, d, total, windows, gaps)
        payload = wire(saved)
        verifier.verify_bytes(a, d, total, windows, payload, sha(payload), gaps)
        bad_payloads = [payload + b'\n', json.dumps(saved).encode(), b'\xff', b'{', b'null',
                        payload.replace(b'"confidence":0', b'"confidence":0,"confidence":0'),
                        payload.replace(b'"confidence":0', b'"confidence":NaN'),
                        payload.replace(b'"residual_frames":0.0', b'"residual_frames":1e999'),
                        payload.replace(b'"confidence":0', b'"confidence":0.0')]
        for bad in bad_payloads:
            with self.subTest(payload=bad[:30]), self.assertRaises(ValueError):
                verifier.verify_bytes(a, d, total, windows, bad, sha(bad), gaps)
        with mock.patch.object(verifier, "reconstruct", side_effect=AssertionError("hash first")):
            with self.assertRaisesRegex(ValueError, "hash"):
                verifier.verify_bytes(a, d, total, windows, payload, "0" * 64, gaps)
        for bad in (bytearray(payload), b'x' * 67108865):
            with self.assertRaisesRegex(ValueError, "capacity"):
                verifier.verify_bytes(a, d, total, windows, bad, sha(bad), gaps)

    def test_gap_and_short_status_corruption_rejected(self):
        for name in ("empty", "short", "gap", "gap_empty"):
            a, d, total, windows, gaps = preflight.generated(name)
            saved = model.construct(a, d, total, windows, gaps)
            for path, value in ((('solution', 'residual_frames'), 0.),
                                (('events', 'beat_frames'), [0]),
                                (('solution', 'coordinates_hex'), ['0x0.0p+0']),
                                (('windows', 0, 'status'), 'candidate_events')):
                bad = deepcopy(saved)
                target = bad
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = value
                with self.subTest(case=name, path=path), self.assertRaises(ValueError):
                    verifier.verify(a, d, total, windows, bad, gaps)

    def test_invalid_input_before_abstention_and_toy_domain(self):
        for beats, downbeats, total, windows, gaps in (
                ([True], [], 10, [{'first_frame': 0, 'end_frame': 10}], [(0, 1)]),
                ([], [], 10, [{'first_frame': 0, 'end_frame': 10, 'truth': 4}], []),
                ([], [0], 10, [{'first_frame': 0, 'end_frame': 10}], []),
                ([], [], 10, [{'first_frame': 0, 'end_frame': 10}], [(1, 1)]),
                ([], [], 10, [{'first_frame': 0, 'end_frame': 10}], [(0, 2), (1, 3)])):
            with self.assertRaisesRegex(ValueError, "^invalid_input$"):
                verifier.reconstruct(beats, downbeats, total, windows, gaps)
        for args in (([], [], []), ([0.], [0], [1]), ([1], [2], [3]), ([1], [0], [1440000])):
            with self.assertRaisesRegex(ValueError, "^invalid_input$"):
                verifier.replay_solution(*args)

    def test_half_neighbors_signed_zero_and_numeric_failure(self):
        for k in (0, 1, 1000, 524288, 1439998):
            for x in (k + .5, math.nextafter(k + .5, -math.inf), math.nextafter(k + .5, math.inf)):
                self.assertEqual(verifier.rounded(x), math.floor(x + .5))
        self.assertEqual(verifier.rounded(math.nextafter(.5, -math.inf)), 1)
        self.assertEqual(verifier.rounded(-0.), 0)
        self.assertEqual(verifier.coordinate(0, [-0.], [0.], [0], [0]).hex(), '-0x0.0p+0')
        with mock.patch.object(verifier, "coordinate", return_value=math.nan):
            with self.assertRaisesRegex(ValueError, "nonfinite_numeric"):
                verifier.replay_solution([0], [0], [0])
        with mock.patch.object(verifier, "residual", return_value=1.):
            with self.assertRaisesRegex(ValueError, "nonconvergence"):
                verifier.replay_solution([0], [0], [0])

    def test_independence_purity_and_no_input_aliases(self):
        source = (ROOT / 'tools/apta_coupled_time_verify.py').read_text()
        tree = ast.parse(source)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                self.assertTrue(all(alias.name in {'array', 'math', 'sys', 'json', 'hashlib'} for alias in node.names))
            if isinstance(node, ast.ImportFrom):
                self.assertIn(node.module, {'array', 'math', 'sys', 'json', 'hashlib'})
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                self.assertNotIn(node.func.id, {'open', 'print', 'eval', 'exec', '__import__', 'input'})
        a, d, total, windows, gaps = preflight.generated('crossing')
        candidate = model.construct(a, d, total, windows, gaps)
        before = deepcopy((a, d, windows, gaps, candidate))
        with mock.patch.object(model, 'construct', side_effect=AssertionError('not independent')), \
                mock.patch.object(model, 'solve_times', side_effect=AssertionError('not independent')), \
                mock.patch.object(model, '_root', side_effect=AssertionError('not independent')), \
                mock.patch.object(model, '_round_frame', side_effect=AssertionError('not independent')), \
                mock.patch('builtins.open', side_effect=AssertionError('no I/O')):
            expected = verifier.verify(a, d, total, windows, candidate, gaps)
        self.assertEqual((a, d, windows, gaps, candidate), before)
        expected['events']['beat_frames'][0] = -1
        self.assertEqual((a, d, windows, gaps, candidate), before)


class PreflightTests(unittest.TestCase):
    def test_complete_generated_process_preflight(self):
        with tempfile.TemporaryDirectory(prefix='libapta-ct02-') as temporary:
            output = Path(os.environ.get('APTA_CT02_GENERATED_OUTPUT', str(Path(temporary) / 'run')))
            result = preflight.preflight(output)
            self.assertEqual(len(result['construct_cases']), 21)
            self.assertEqual(len(result['verify_cases']), 21)
            self.assertTrue((output / 'preflight.json').is_file())
            self.assertFalse((output / 'INVALID.json').exists())
            for role in ('construct', 'verify'):
                self.assertEqual(result['phases'][role]['status'], 'PASS')
                self.assertTrue(result['phases'][role]['process_group_monitored'])
                self.assertGreater(len(result['phases'][role]['peak_rss_kib_by_pid']), 2)
                for row in result[role + '_cases']:
                    self.assertLessEqual(row['supervised_wall_seconds'], 60)
                    self.assertLessEqual(row['self_peak_rss_kib'], 262144)
                    self.assertEqual(row['constructor_imported'], role == 'construct')
                    self.assertEqual(row['verifier_imported'], role == 'verify')
            self.assertLessEqual(io.tree_bytes((output,)), 67108864)
            with self.assertRaises(ValueError):
                preflight.preflight(output)

    def test_actual_phase_timeout_rss_output_rejection_preserves_invalid(self):
        original = io.supervise
        with tempfile.TemporaryDirectory(prefix='libapta-ct02-negative-') as temporary:
            for kind in ('timeout', 'rss', 'output'):
                def lowered(command, seconds, rss_kib, byte_limit, roots, prefix, group=True):
                    return original(command, .001 if kind == 'timeout' else seconds,
                                    1 if kind == 'rss' else rss_kib,
                                    1024 if kind == 'output' else byte_limit, roots, prefix, group)
                output = Path(temporary) / kind
                with mock.patch.object(io, 'supervise', side_effect=lowered):
                    with self.assertRaises(ValueError):
                        preflight.preflight(output)
                self.assertFalse((output / 'preflight.json').exists())
                self.assertEqual(json.loads((output / 'INVALID.json').read_text())['status'], 'INVALID')
                report = json.loads((output / 'construct.phase.resources.json').read_text())
                self.assertEqual(report['status'], 'INVALID')
                self.assertIn({'timeout': 'wall-time', 'rss': 'RSS', 'output': 'output'}[kind], report['failure'])

    def test_descendant_rss_is_accounted_and_group_is_killed(self):
        # Keep the test cap above this test parent's cumulative high-water mark.
        current = io.process_peaks(os.getpid(), False)
        cap = max(current.values()) + 8192
        child = ('import time; a=bytearray(' + str((cap + 16384) * 1024) + '); '
                 'a[::4096]=b"x"*len(a[::4096]); time.sleep(5)')
        command = 'import subprocess,sys; subprocess.run([sys.executable,"-c",' + repr(child) + '])'
        with tempfile.TemporaryDirectory(prefix='libapta-ct02-child-') as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, 'RSS'):
                io.supervise([sys.executable, '-c', command], 10, cap, 67108864, (root,), root / 'child')
            report = json.loads((root / 'child.resources.json').read_text())
            offenders = [pid for pid, peak in report['peak_rss_kib_by_pid'].items()
                         if peak > cap and int(pid) not in (report['root_pid'], os.getpid())]
            self.assertTrue(offenders)
            for pid in offenders:
                # SIGKILL delivery/reparenting is asynchronous for a grandchild.
                deadline = time.monotonic() + 2
                path = Path('/proc', pid, 'stat')
                while path.exists() and path.read_text().split()[2] != 'Z' and time.monotonic() < deadline:
                    time.sleep(.01)
                if path.exists():
                    self.assertEqual(path.read_text().split()[2], 'Z')

    def test_successful_root_cannot_leave_live_descendant(self):
        child = 'import time; time.sleep(5)'
        command = ('import subprocess,sys,time; subprocess.Popen([sys.executable,"-c",'
                   + repr(child) + ']); time.sleep(.1)')
        with tempfile.TemporaryDirectory(prefix='libapta-ct02-orphan-') as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, 'orphan'):
                io.supervise([sys.executable, '-c', command], 10, 262144, 67108864, (root,), root / 'orphan')

    def test_no_real_input_or_bypass_cli(self):
        import subprocess
        with tempfile.TemporaryDirectory(prefix='libapta-ct02-cli-') as temporary:
            output = Path(temporary) / 'absent'
            for name in ('truth', 'labels', 'events', 'audio', 'lambda', 'timeout', 'rss'):
                result = subprocess.run([sys.executable, str(ROOT / 'tools/apta_coupled_time_preflight.py'),
                                         '--output', str(output), '--' + name, 'unused'],
                                        capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'unrecognized arguments', result.stderr)
                self.assertFalse(output.exists())
        self.assertFalse((ROOT / 'tools/apta_1_1_coupled_time.py').exists())


if __name__ == '__main__':
    unittest.main()
