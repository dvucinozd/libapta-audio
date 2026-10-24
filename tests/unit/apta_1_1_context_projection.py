#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""P1 public generated arithmetic only: never load saved events or score music."""
import ast
from collections import Counter
from copy import deepcopy
from fractions import Fraction
import inspect
import json
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_context_projection as projection
import apta_context_projection_oracle as oracle
import apta_1_1_context_projection_registration as registration
import apta_1_1_neural_reference as legacy
import apta_1_1_event_representation as representation

COUNTS = Counter()
MAX_SOURCE_SECONDS = 0.0


def encoded(value):
    return json.dumps(value, sort_keys=True, allow_nan=False, separators=(',', ':'))


def source(beats, db=(), first=0, end=None, total=None):
    total = total if total is not None else (beats[-1] + 10000 if beats else 1000)
    return dict(beat_frames=list(beats), downbeat_ordinals=list(db), total_frames=total,
                first_frame=first, end_frame=end if end is not None else total)


def matrix():
    # Fixed public arithmetic, independent of saved cohorts and their outcomes.
    for period in (9600, 10001, 24000, 71999, 72000):
        for n in (4, 9, 17):
            for pattern in ('constant', 'alternating', 'ramp'):
                beats = [1000 + period * i + (0 if pattern == 'constant' else
                         157 * (i % 2) if pattern == 'alternating' else 17 * i * i)
                         for i in range(n)]
                total = beats[-1] + 10000
                for first, end in ((0, total), (beats[0], beats[-1] + 1),
                                   (beats[1], total)):
                    for meter in (0, 3, 4):
                        yield source(beats, range(0, n, meter) if meter else (), first, end, total)


class ProjectionTests(unittest.TestCase):
    def check_source(self, value, compare_legacy=True):
        global MAX_SOURCE_SECONDS
        before = encoded(value)
        started = time.monotonic()
        actual = projection.project_window(**value)
        expected = oracle.independent_project_window(**value)
        self.assertEqual(encoded(actual), encoded(expected))
        registration.validate_example(value, actual)
        self.assertEqual(encoded(value), before)
        elapsed = time.monotonic() - started
        self.assertLess(elapsed, 60)
        MAX_SOURCE_SECONDS = max(MAX_SOURCE_SECONDS, elapsed)
        COUNTS['oracle_comparisons'] += 1
        COUNTS['status_' + actual['status']] += 1
        COUNTS['meter_' + actual['meter_status']] += 1
        if compare_legacy:
            old = legacy.project(value['beat_frames'],
                                 [value['beat_frames'][i] for i in value['downbeat_ordinals']],
                                 value['first_frame'], value['end_frame'])
            if old['selected']['kind'] == 'accepted':
                self.assertEqual(actual['status'], 'ACCEPTED')
                for key in ('anchor_frame', 'tempo_millibpm', 'confidence'):
                    self.assertEqual(actual['model'][key], old['selected'][key])
                self.assertEqual([(p['position_q32'], actual['model']['period_q32'])
                                  for p in actual['points']], representation.points_for_control(old))
                COUNTS['legacy_accepted_grid_intersections'] += 1
                if old['meter']:
                    q = old['meter']['downbeat_frame'] * projection.Q + old['meter']['downbeat_fraction_q32']
                    if value['first_frame'] * projection.Q <= q < value['end_frame'] * projection.Q:
                        self.assertEqual({k: actual['meter'][k] for k in old['meter']}, old['meter'])
                        COUNTS['legacy_owned_meter_intersections'] += 1
                    else:
                        # Representation conflict only, never a correctness count.
                        COUNTS['legacy_native_unowned_meter_conflicts'] += 1
        return actual

    def test_public_frozen_examples(self):
        for row in registration.examples():
            with self.subTest(name=row['name']):
                self.assertEqual(encoded(self.check_source(row['input'])), encoded(row['output']))

    def test_generated_exact_oracle_and_legacy_intersection(self):
        values = list(matrix())
        self.assertEqual(len(values), 405)
        for i, value in enumerate(values):
            with self.subTest(case=i):
                self.check_source(value)

    def test_lattice_only_q32_endpoints_carry_and_empty(self):
        for row in registration.EXAMPLES['lattice_only_examples']:
            first, points = projection._owned_points(row['anchor_q32'], row['period_q32'],
                row['first_frame'], row['end_frame'], row['anchor_ordinal'], row['end_frame'])
            self.assertEqual(points, row['expected_points'], row['name'])
            self.assertEqual(first, row['expected_indices'][0] if row['expected_indices'] else None)

    def test_exact_positive_and_negative_half_ties(self):
        for denominator in (1, 2, 3, 10, 4294967296):
            for numerator in range(-21, 22):
                value = Fraction(numerator, denominator)
                self.assertEqual(projection._round(value), oracle._nearest(value))
        self.assertEqual([projection._round(Fraction(x, 2)) for x in (-5, -3, -1, 1, 3, 5)],
                         [-2, -1, 0, 1, 2, 3])

    def test_meter_changes_only_meter_and_keeps_same_raw_witness(self):
        baseline = registration.examples()[1]['input']
        baseline_copy = deepcopy(baseline)
        owned = self.check_source(baseline)
        self.assertLess(owned['model']['anchor_frame'], baseline['first_frame'])
        self.assertEqual(owned['first_index'], 1)
        self.assertEqual(owned['meter']['witness_ordinals'], [2, 6, 10])
        self.assertEqual(owned['meter']['downbeat_ordinal'], 6)
        for db in ([], [2], [2, 5, 8], [2, 6, 10]):
            value = deepcopy(baseline)
            value['downbeat_ordinals'] = db
            output = self.check_source(value)
            self.assertEqual({k: v for k, v in output.items() if k not in ('meter', 'meter_status')},
                             {k: v for k, v in owned.items() if k not in ('meter', 'meter_status')})
        self.assertEqual(baseline, baseline_copy)

    def test_first_eligible_bar_is_retained(self):
        # Consecutive triples include a later different numerator; retain the first.
        output = self.check_source(source([1000 + 24000 * i for i in range(18)], [0, 3, 6, 10, 14]))
        self.assertEqual(output['meter']['numerator'], 3)
        self.assertEqual(output['meter']['witness_ordinals'], [0, 3, 6])

    def test_same_triple_right_fallback_and_no_later_triple_rescue(self):
        # Public exact construction: both residual moments vanish, so the actual
        # normal-equation fit remains A=0, b=24000. No mocked/mismatched fit.
        for right_offset in (0, 8100):
            offsets = [0] * 20
            offsets[0], offsets[3], offsets[6] = 18, 8100, right_offset
            offsets[19] = -(3 * 8100 + 6 * right_offset - sum(offsets)) // 18
            offsets[1] = -sum(offsets)
            self.assertEqual(sum(offsets), 0)
            self.assertEqual(sum(i * d for i, d in enumerate(offsets)), 0)
            beats = [24000 * i + d for i, d in enumerate(offsets)]
            output = self.check_source(source(beats, [0, 3, 6, 9, 12, 15], first=18, total=480000))
            self.assertEqual(output['fit']['rounded_anchor'], 0)
            self.assertEqual(output['fit']['period_numerator'], 24000)
            self.assertEqual(output['fit']['period_denominator'], 1)
            if right_offset == 0:
                self.assertEqual(output['meter']['witness_ordinals'], [0, 3, 6])
                self.assertEqual(output['meter']['downbeat_ordinal'], 6)
            else:
                self.assertEqual(output['meter_status'], 'NO_OWNED_WITNESS')
                self.assertIsNone(output['meter'])
                # A later raw triple [9,12,15] is perfectly aligned and owned,
                # but the contract forbids using it to rescue the first triple.
                self.assertTrue(all(beats[i] == i * 24000 for i in (9, 12, 15)))

    def test_boundary_tempos_and_abstentions(self):
        for period, status in ((9600, 'ACCEPTED'), (72000, 'ACCEPTED'),
                               (9599, 'UNSUPPORTED_TEMPO'), (72002, 'UNSUPPORTED_TEMPO')):
            output = self.check_source(source([1000 + period * i for i in range(9)]))
            self.assertEqual(output['status'], status)
        for n in range(4):
            self.assertEqual(self.check_source(source([1000 + 24000 * i for i in range(n)]))['status'],
                             'INSUFFICIENT_EVENTS')

    def test_maximum_input_and_eight_independent_windows(self):
        dense = self.check_source(source(list(range(3072)), total=1440000))
        self.assertEqual(dense['status'], 'UNSUPPORTED_TEMPO')
        beats = list(range(2990)) + [10000 + 16000 * i for i in range(82)]
        output = self.check_source(source(beats, first=10000, total=1440000))
        self.assertEqual(output['status'], 'ACCEPTED')
        self.assertEqual(output['first_observed_ordinal'], 2990)
        values = [source([1000 + 12000 * i for i in range(120)], range(0, 120, 4),
                         i * 180000, (i + 1) * 180000, 1440000) for i in range(8)]
        outputs = [self.check_source(v) for v in values]
        self.assertLessEqual(sum(v['point_count'] for v in outputs), 3072)
        self.assertEqual([encoded(self.check_source(v)) for v in reversed(values)],
                         [encoded(v) for v in reversed(outputs)])
        # The API returns independent window models, without a stitching policy.

    def test_all_validation_precedes_insufficient_abstention(self):
        class ListSubclass(list):
            pass
        invalid = [dict(beat_frames=x) for x in (None, (), ListSubclass(), [True], [0.0], [-1],
                   [1000], [2, 2], [3, 2], list(range(3073)))]
        invalid += [dict(downbeat_ordinals=x) for x in (None, (), [True], [0.0], [0], [-1], [1, 0])]
        invalid += [dict(total_frames=x) for x in (True, 0, 1440001, 1000.0)]
        invalid += [dict(first_frame=x) for x in (False, -1, 1000, 0.0)]
        invalid += [dict(end_frame=x) for x in (True, 0, 1001, 1000.0)]
        for changes in invalid:
            value = source([])
            value.update(changes)
            for function in (projection.project_window, oracle.independent_project_window):
                with self.subTest(changes=changes, function=function.__name__):
                    with self.assertRaises(ValueError):
                        function(**value)
        ordered = source([10, 20, 30], [0, 0])
        with self.assertRaises(ValueError):
            projection.project_window(**ordered)
        with self.assertRaises(TypeError):
            projection.project_window(**source([]), reference_labels=[])

    def test_lattice_only_capacity_overflow_never_partial(self):
        # Deliberately arbitrary lattice parameters, NOT high-level OLS fixtures.
        for arguments in ((0, 1, 0, 1, 0, 1), (0, 0, 0, 1, 0, 1),
                          (0, projection.Q, 0, 1, 1 << 63, 1),
                          (1 << 64, projection.Q, 1 << 32, (1 << 32) + 1, 0, (1 << 32) + 1),
                          (-projection.Q, projection.Q, -1, 0, 0, 1)):
            with self.assertRaises(ValueError):
                projection._owned_points(*arguments)

    def test_no_output_aliasing_or_input_mutation(self):
        value = registration.examples()[1]['input']
        first = self.check_source(value)
        original = encoded(first)
        second = projection.project_window(**value)
        second['points'][0]['ordinal'] = -1
        second['meter']['witness_ordinals'][0] = -1
        second['fit']['rounded_anchor'] = -1
        self.assertEqual(encoded(first), original)
        self.assertEqual(encoded(projection.project_window(**value)), original)

    def test_corruption_controls_and_lossless_json(self):
        value = registration.examples()[1]['input']
        output = self.check_source(value)
        for mutate in (lambda v: v['model'].update(anchor_frame=value['first_frame']),
                       lambda v: v['points'][0].update(position_q32=0),
                       lambda v: v['points'][0].update(ordinal=0),
                       lambda v: v.update(point_count=0),
                       lambda v: v['meter'].update(witness_ordinals=[0, 4, 8]),
                       lambda v: v['fit'].update(period_numerator=1),
                       lambda v: v.update(point_count=float(v['point_count']))):
            corrupted = deepcopy(output)
            mutate(corrupted)
            with self.assertRaises(ValueError):
                registration.validate_example(value, corrupted)
        self.assertEqual(encoded(json.loads(encoded(output))), encoded(output))
        self.assertTrue(any(p['position_q32'] > 2**49 for p in output['points']))
        def pairs(items):
            result = {}
            for key, item in items:
                if key in result:
                    raise ValueError('duplicate key')
                result[key] = item
            return result
        def nonfinite(value):
            raise ValueError('nonfinite JSON')
        for text in ('{"x":1,"x":2}', '{"x":NaN}', '{"x":Infinity}'):
            with self.assertRaises(ValueError):
                json.loads(text, object_pairs_hook=pairs, parse_constant=nonfinite)

    def test_import_and_io_isolation_fresh_process(self):
        path = ROOT / 'tools/apta_context_projection.py'
        tree = ast.parse(path.read_text())
        imports = [node for node in ast.walk(tree) if isinstance(node, (ast.Import, ast.ImportFrom))]
        self.assertEqual(len(imports), 1)
        self.assertIsInstance(imports[0], ast.ImportFrom)
        self.assertEqual(imports[0].module, 'fractions')
        forbidden = {'open', '__import__', 'eval', 'exec', 'compile', 'globals', 'locals',
                     'getattr', 'setattr', 'input', 'print', '__builtins__'}
        self.assertFalse({n.id for n in ast.walk(tree) if isinstance(n, ast.Name)} & forbidden)
        oracle_imports = [n for n in ast.walk(ast.parse(inspect.getsource(oracle)))
                          if isinstance(n, (ast.Import, ast.ImportFrom))]
        self.assertEqual([n.module for n in oracle_imports], ['fractions'])
        # Load source before blocking I/O; import and all five calls run in a new
        # isolated interpreter with only the permitted fractions import allowed.
        script = '''import builtins, fractions, pathlib, sys
text = pathlib.Path(sys.argv[1]).read_text()
code = compile(text, sys.argv[1], 'exec')
original_import = builtins.__import__
def importing(name, *args, **kwargs):
    if name != 'fractions': raise AssertionError('unexpected import')
    return original_import(name, *args, **kwargs)
def blocked(*args, **kwargs): raise AssertionError('I/O attempted')
builtins.__import__ = importing
builtins.open = blocked
builtins.input = blocked
scope = {}
exec(code, scope)
f = scope['project_window']
assert f([1000 + 24000*i for i in range(12)], [0,4,8], 288000,0,288000)['meter_status'] == 'OWNED'
y=[48001]+[48000+24000*i-160 for i in range(1,9)]
assert f([1000,25000]+y,[2,6,10],240000,48000,240000)['first_index'] == 1
assert f([v-48000 for v in y],[0,4,8],192000,0,192000)['status'] == 'SOURCE_ANCHOR_OUT_OF_DOMAIN'
assert f([],[],1000,0,1000)['status'] == 'INSUFFICIENT_EVENTS'
assert f([0,1,2,3],[],100,0,100)['status'] == 'UNSUPPORTED_TEMPO'
'''
        result = subprocess.run([sys.executable, '-I', '-c', script, str(path)],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '')
        self.assertEqual(list(inspect.signature(projection.project_window).parameters),
                         ['beat_frames', 'downbeat_ordinals', 'total_frames', 'first_frame', 'end_frame'])


if __name__ == '__main__':
    suite = unittest.main(exit=False)
    print(encoded({'counts': dict(COUNTS), 'max_source_seconds': MAX_SOURCE_SECONDS,
                   'tests_run': suite.result.testsRun, 'skips': len(suite.result.skipped)}))
    sys.exit(not suite.result.wasSuccessful())
