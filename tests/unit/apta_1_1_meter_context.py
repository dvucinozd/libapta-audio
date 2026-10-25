#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""MC-01 public arithmetic only; never open saved cohort cases or labels."""
import ast
from collections import Counter
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests/unit')]
import apta_context_projection as p1
import apta_meter_context_projection as production
import apta_meter_context_oracle as oracle

COUNTS = Counter()
MAX_CALL_SECONDS = 0.0


def encoded(value):
    return json.dumps(value, sort_keys=True, allow_nan=False)


def source(beats=None, downbeats=None, first=0, end=None, total=None):
    beats = list(beats if beats is not None else [1000 + 12000 * i for i in range(24)])
    total = total if total is not None else beats[-1] + 12000
    return dict(beat_frames=beats, downbeat_ordinals=list(downbeats if downbeats is not None else range(0, len(beats), 4)),
                total_frames=total, first_frame=first, end_frame=total if end is None else end, known_gaps=[])


def boundary(meter, period=12000, shift=0, window=1):
    first = meter * period // 2
    beats = [first + shift + i * period for i in range(24)]
    width = 5 * meter * period // 2
    return source(beats, range(0, 24, meter), window * width, (window + 1) * width,
                  max(beats[-1] + period, 2 * width))


class MeterContextTests(unittest.TestCase):
    def compare(self, inputs):
        global MAX_CALL_SECONDS
        saved = deepcopy(inputs)
        started = time.monotonic()
        actual = production.project_window(**inputs)
        expected = oracle.project_window_oracle(**inputs)
        self.assertEqual(encoded(actual), encoded(expected))
        self.assertEqual(inputs, saved)
        old = p1.project_window(**{k: v for k, v in inputs.items() if k != 'known_gaps'})
        self.assertEqual(set(actual), set(old) | {'baseline_meter', 'baseline_meter_status', 'meter_origin', 'context'})
        for key in set(old) - {'format', 'registration_sha256', 'meter', 'meter_status'}:
            self.assertEqual(actual[key], old[key], key)
        self.assertEqual(actual['baseline_meter'], old['meter'])
        self.assertEqual(actual['baseline_meter_status'], old['meter_status'])
        if old['meter'] is not None:
            self.assertEqual(actual['meter'], old['meter'])
            self.assertEqual(actual['meter_status'], old['meter_status'])
            self.assertEqual(actual['meter_origin'], 'P1')
            self.assertIsNone(actual['context'])
            COUNTS['preserved_owned_meters'] += 1
        if actual['meter'] is not None:
            m = actual['meter']
            q = m['downbeat_frame'] * 4294967296 + m['downbeat_fraction_q32']
            self.assertIn((q, m['downbeat_ordinal']), {(p['position_q32'], p['ordinal']) for p in actual['points']})
            self.assertTrue(inputs['first_frame'] * 4294967296 <= q < inputs['end_frame'] * 4294967296)
            if actual['meter_origin'] == 'CONTEXT':
                self.assertEqual(m['witness_ordinals'][1] - m['witness_ordinals'][0], m['numerator'])
                self.assertEqual(m['witness_ordinals'][2] - m['witness_ordinals'][1], m['numerator'])
                COUNTS['context_meters'] += 1
        if actual['context'] is not None:
            selected = actual['context']['downbeat_ordinals']
            self.assertLessEqual(sum(inputs['beat_frames'][i] < inputs['first_frame'] for i in selected), 2)
            self.assertLessEqual(sum(inputs['beat_frames'][i] >= inputs['end_frame'] for i in selected), 2)
            self.assertEqual(actual['context']['examined_triples'], max(0, len(selected) - 2))
        elapsed = time.monotonic() - started
        self.assertLess(elapsed, 60)
        MAX_CALL_SECONDS = max(MAX_CALL_SECONDS, elapsed)
        COUNTS['oracle_comparisons'] += 1
        COUNTS['status_' + actual['meter_status']] += 1
        return actual

    def test_01_registration_bindings_and_allowed_files(self):
        path = ROOT / 'docs/rust/METER-CONTEXT-PROTOCOL.json'
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), production.REGISTRATION_SHA256)
        contract = json.loads(path.read_text())
        for name, digest in contract['binding_sha256'].items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), digest, name)
        self.assertFalse(contract['campaign_authorized'])
        self.assertEqual(oracle.REGISTRATION_SHA256, production.REGISTRATION_SHA256)

    def test_02_boundary_crossings_both_meters_and_directions(self):
        for meter in (3, 4):
            for left_shift, right_shift in ((0, -1), (-1, 0)):
                for window in (0, 1):
                    left = self.compare(boundary(meter, shift=left_shift, window=window))
                    right = self.compare(boundary(meter, shift=right_shift, window=window))
                    self.assertIsNotNone(left['meter'])
                    self.assertIsNotNone(right['meter'])
                    self.assertEqual((left['meter']['numerator'], right['meter']['numerator']), (meter, meter))
                    self.assertEqual(left['meter']['downbeat_ordinal'] % meter, right['meter']['downbeat_ordinal'] % meter)
        # The one-frame shift reproduces the old local evidence loss arithmetically.
        left, right = boundary(4), boundary(4, shift=-1)
        self.assertIsNotNone(p1.project_window(**{k: v for k, v in left.items() if k != 'known_gaps'})['meter'])
        self.assertIsNone(p1.project_window(**{k: v for k, v in right.items() if k != 'known_gaps'})['meter'])

    def test_03_parameterized_public_matrix(self):
        for meter in (3, 4):
            for period in (9600, 10001, 12000, 24000):
                for jitter in (0, 157):
                    beats = [1000 + i * period + jitter * (i % 2) for i in range(24)]
                    for start in (0, 3, 5, 8):
                        for length in (6, 9):
                            for delta in (-1, 0, 1):
                                for phase in (0, 1):
                                    self.compare(source(beats, range(phase, 24, meter),
                                                        max(0, beats[start] + delta), beats[start + length] + delta))

    def test_04_owned_meter_preserved_despite_wider_conflict(self):
        result = self.compare(source(downbeats=[0, 4, 8, 11, 14, 17], first=1000, end=110000))
        self.assertEqual(result['meter_origin'], 'P1')
        self.assertIsNone(result['context'])

    def test_05_conflicting_numerators_abstain(self):
        result = self.compare(source(downbeats=[0, 3, 6, 10, 14], first=73000, end=133000))
        self.assertEqual(result['meter_status'], 'AMBIGUOUS_CONTEXT')
        self.assertIsNone(result['meter'])
        self.assertEqual({x['numerator'] for x in result['context']['hypotheses']}, {3, 4})

    def test_06_conflicting_equal_meter_phases_abstain(self):
        result = self.compare(source(downbeats=[0, 4, 8, 9, 13, 17], first=97000, end=157000))
        self.assertEqual(result['meter_status'], 'AMBIGUOUS_CONTEXT')
        self.assertEqual(result['context']['hypotheses'], [{'numerator': 4, 'phase_modulo': 0}, {'numerator': 4, 'phase_modulo': 1}])

    def test_07_out_of_tolerance_and_half_tie_context(self):
        for displacement in (2000, 6000):
            inputs = source(first=97000, end=181000)
            for i in (0, 4, 16, 20):
                inputs['beat_frames'][i] += displacement
            result = self.compare(inputs)
            self.assertEqual(result['meter_status'], 'NO_ELIGIBLE_CONTEXT')
            self.assertIsNone(result['meter'])

    def test_08_context_bounds_and_latency_metadata(self):
        result = self.compare(source(downbeats=[0, 3, 6, 9, 12, 15, 18, 21], first=109000, end=169000))
        self.assertEqual(result['context']['downbeat_ordinals'], [3, 6, 9, 12, 15, 18])
        self.assertGreater(result['context']['lookahead_frames'], 0)
        self.assertGreater(result['context']['lookbehind_frames'], 0)

    def test_09_q32_fraction_and_exclusive_endpoints(self):
        fractions = 0
        for meter in (3, 4):
            for period in (10001, 11777, 24001):
                for window in (0, 1):
                    result = self.compare(boundary(meter, period, -1, window))
                    self.assertIsNotNone(result['meter'])
                    fractions += int(result['meter']['downbeat_fraction_q32'] != 0)
        self.assertGreater(fractions, 0)
        for first, end in ((0, 1000), (1000, 1001), (1000, 49000), (49000, 97000)):
            self.compare(source(first=first, end=end))

    def test_10_empty_insufficient_and_source_end(self):
        self.compare(source([], [], total=1000))
        for n in (1, 2, 3, 4, 8):
            self.compare(source([1000 + 12000 * i for i in range(n)], []))
        self.compare(source(end=277001, total=277001))

    def test_11_input_types_geometry_order_and_gap_rejection(self):
        invalid = [{'beat_frames': [True, 1000, 2000, 3000]}, {'beat_frames': [1000, 1000]},
                   {'downbeat_ordinals': [0, 0]}, {'downbeat_ordinals': [False]},
                   {'downbeat_ordinals': [24]}, {'total_frames': True}, {'first_frame': 1.0},
                   {'end_frame': 0}, {'total_frames': 1440001}, {'beat_frames': list(range(3073))}]
        for change in invalid:
            inputs = source();inputs.update(change)
            for fn in (production.project_window, oracle.project_window_oracle):
                with self.assertRaises(ValueError): fn(**inputs)
        for gaps in (None, False, (), [1], [{'first_frame': 1, 'end_frame': 2}]):
            inputs = source();inputs['known_gaps'] = gaps
            for fn in (production.project_window, oracle.project_window_oracle):
                with self.assertRaises(ValueError): fn(**inputs)
        with self.assertRaises(TypeError): production.project_window(**{k: v for k, v in source().items() if k != 'known_gaps'})
        with self.assertRaises(TypeError): production.project_window(**source(), truth={})

    def test_12_maximum_source_events_and_window_point_budget(self):
        inputs = source([1000 + 400 * i for i in range(3072)], range(0, 3072, 4), total=1440000)
        result = self.compare(inputs)
        self.assertEqual(result['status'], 'UNSUPPORTED_TEMPO')
        self.assertIsNone(result['context'])
        points = 0
        for i in range(8):
            result = self.compare(source(first=i * 36000, end=(i + 1) * 36000, total=288000))
            points += result['point_count']
        self.assertLessEqual(points, 3072)

    def test_13_pure_imports_and_fresh_process_no_io(self):
        tree = ast.parse((ROOT / 'tools/apta_meter_context_projection.py').read_text())
        imports = [node.module if isinstance(node, ast.ImportFrom) else name.name
                   for node in ast.walk(tree) if isinstance(node, (ast.Import, ast.ImportFrom))
                   for name in (node.names if isinstance(node, ast.Import) else [None])]
        self.assertEqual(set(imports), {'fractions', 'apta_context_projection'})
        oracle_tree = ast.parse((ROOT / 'tests/unit/apta_meter_context_oracle.py').read_text())
        self.assertNotIn('apta_meter_context_projection', ast.unparse(oracle_tree))
        code = """import sys
sys.path.insert(0,sys.argv[1]);import apta_meter_context_projection as m
def audit(event,args):
 if event in ('open','subprocess.Popen','socket.__new__'):raise RuntimeError('unexpected I/O')
sys.addaudithook(audit)
r=m.project_window([23999+i*12000 for i in range(24)],list(range(0,24,4)),324000,120000,240000,known_gaps=[])
assert r['meter_origin']=='CONTEXT';print('PURE_PASS')
"""
        result = subprocess.run([sys.executable, '-B', '-X', 'pycache_prefix=/libapta-no-cache', '-c', code,
                                 str(ROOT / 'tools')], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'PURE_PASS')

    def test_14_labels_environment_and_determinism(self):
        inputs = boundary(4, shift=-1)
        a = self.compare(inputs)
        previous = os.environ.get('APTA_REFERENCE_LABELS')
        try:
            os.environ['APTA_REFERENCE_LABELS'] = '/absent/public/labels.json'
            b = self.compare(inputs)
        finally:
            if previous is None: os.environ.pop('APTA_REFERENCE_LABELS', None)
            else: os.environ['APTA_REFERENCE_LABELS'] = previous
        self.assertEqual(encoded(a), encoded(b))

    def test_15_owned_meter_matches_existing_meter_oracle(self):
        from apta_context_matched import meter_evidence
        from apta_context_matched_oracle import meter_evidence_oracle
        for meter in (3, 4):
            inputs = boundary(meter, shift=-1)
            result = self.compare(inputs)
            truth = {'beat_frames': inputs['beat_frames'],
                     'downbeat_frames': [inputs['beat_frames'][i] for i in inputs['downbeat_ordinals']],
                     'meter_numerator': meter, 'meter_denominator': 4}
            raw = {k: result['meter'][k] for k in ('numerator', 'denominator', 'downbeat_frame', 'downbeat_fraction_q32', 'downbeat_ordinal')}
            args = raw, result['points'], inputs['first_frame'], inputs['end_frame'], truth, 'context'
            self.assertEqual(meter_evidence(*args), meter_evidence_oracle(*args))


if __name__ == '__main__':
    program = unittest.main(exit=False, verbosity=2)
    if os.environ.get('APTA_MC01_RESULT'):
        import apta_dense_runner_support as io
        bindings = {};io.runtime_snapshot(bindings)
        result = {'tests': program.result.testsRun, 'failures': len(program.result.failures),
                  'errors': len(program.result.errors), 'skips': len(program.result.skipped),
                  'counts': dict(COUNTS), 'max_public_call_seconds_including_oracle': MAX_CALL_SECONDS,
                  'self_max_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                  'runtime_sha256_by_path': bindings}
        io.write_new(Path(os.environ['APTA_MC01_RESULT']), result)
    sys.exit(not program.result.wasSuccessful())
