#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""P4 public synthetic evaluation only; no saved cohort, runner or inference."""
import ast
from collections import Counter
from copy import deepcopy
from fractions import Fraction
from itertools import product
import json
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_context_matched as matched
import apta_context_matched_oracle as oracle
import apta_context_projection as projection
import apta_1_1_context_projection as public_projection
import apta_1_1_context_matched_registration as registration
import apta_1_1_event_representation as representation
import apta_1_1_native_annotated_development as canonical_meter
import apta_1_1_neural_reference as legacy

COUNTS = Counter()
MAX_SOURCE_SECONDS = 0.0


def encoded(value):
    return json.dumps(value, sort_keys=True, allow_nan=False, separators=(',', ':'))


def truth_for(total=240000, period=24000, offset=0, numerator=4):
    beats = list(range(offset, total + 1, period))
    return {'beat_frames': beats, 'downbeat_frames': beats[::numerator],
            'meter_numerator': numerator, 'meter_denominator': 4}


def normalize_meter(meter, ordinal=None):
    if not meter:
        return None
    result = {key: meter[key] for key in ('numerator', 'denominator', 'downbeat_frame', 'downbeat_fraction_q32')}
    result['downbeat_ordinal'] = meter['downbeat_ordinal'] if ordinal is None else ordinal
    return result


def facts(supported, correct, complete, family, rep):
    """Public truth-table cell, not a claim about a fitted music example."""
    result = {'first_frame': 0, 'end_frame': 1000, 'representation': rep,
              'reference_complete': complete, 'G': supported if family.startswith('grid_') else 1,
              'M': supported if family == 'meter' else 1 if family == 'downbeat' else 0,
              'D': supported if family == 'downbeat' else 0, 'legacy_native_unowned_meter_conflict': 0,
              'correct': {key: 0 for key in matched.FAMILIES}}
    result['correct'][family] = correct
    if family == 'grid_joint':
        result['correct'].update(grid_phase=correct, grid_period=correct)
    if family == 'grid_complete_sequence':
        result['correct'].update(grid_phase=correct, grid_period=correct, grid_joint=correct)
    return result


class MatchedTests(unittest.TestCase):
    def meter(self, meter, points, first, end, truth, rep):
        inputs = deepcopy((meter, points, truth))
        actual = matched.meter_evidence(meter, points, first, end, truth, rep)
        expected = oracle.meter_evidence_oracle(meter, points, first, end, truth, rep)
        self.assertEqual(encoded(actual), encoded(expected))
        self.assertEqual((meter, points, truth), inputs)
        COUNTS['meter_oracle_comparisons'] += 1
        COUNTS['meter_' + ('supported' if actual['meter_supported'] else 'unsupported')] += 1
        return actual

    def ledger(self, left, right, comparison, family, stratum='all', level='cohort'):
        inputs = encoded([left, right])
        actual = matched.make_ledger(left, right, comparison, family, stratum, level)
        self.assertEqual(encoded(actual), encoded(oracle.ledger_oracle(left, right, comparison, family, stratum, level)))
        registration.validate(actual)
        self.assertEqual(encoded([left, right]), inputs)
        COUNTS['ledger_oracle_comparisons'] += 1
        return actual

    def context_cell(self, value, truth):
        p = projection.project_window(**value)
        evidence = self.meter(normalize_meter(p['meter']), p['points'], p['first_frame'], p['end_frame'], truth, 'context')
        points = [(b['position_q32'], p['model']['period_q32']) for b in p['points']]
        grid = representation.score_points(points, truth, p['first_frame'], p['end_frame'])
        cell = matched.make_cell(grid, evidence)
        self.assertEqual(encoded(cell), encoded(oracle.cell_oracle(grid, evidence)))
        proxy = {'first_frame': p['first_frame'], 'end_frame': p['end_frame'],
                 'selected': {'kind': 'accepted', **p['model']} if p['model'] else {'kind': 'insufficient_events'},
                 'meter': p['meter'] or {}}
        old = canonical_meter.score_window(proxy, truth, 48000)
        self.assertEqual(evidence['score'], {key: old[key] for key in matched.METER_KEYS})
        COUNTS['canonical_meter_comparisons'] += 1
        COUNTS['cell_oracle_comparisons'] += 1
        return cell

    def legacy_cell(self, value, truth):
        old = legacy.project(value['beat_frames'], [value['beat_frames'][i] for i in value['downbeat_ordinals']],
                             value['first_frame'], value['end_frame'])
        expanded = representation.points_for_control(old)
        g = next((i for i, frame in enumerate(value['beat_frames']) if value['first_frame'] <= frame < value['end_frame']), 0)
        points = [{'position_q32': q, 'ordinal': g + i} for i, (q, _) in enumerate(expanded)]
        meter = None
        if old['meter']:
            q = old['meter']['downbeat_frame'] * matched.Q + old['meter']['downbeat_fraction_q32']
            delta, remainder = divmod(q - old['selected']['anchor_frame'] * matched.Q, expanded[0][1])
            self.assertEqual(remainder, 0)
            meter = normalize_meter(old['meter'], g + delta)
        evidence = self.meter(meter, points, value['first_frame'], value['end_frame'], truth, 'legacy')
        grid = representation.score_points(expanded, truth, value['first_frame'], value['end_frame'])
        cell = matched.make_cell(grid, evidence)
        self.assertEqual(encoded(cell), encoded(oracle.cell_oracle(grid, evidence)))
        expected = canonical_meter.score_window(old, truth, 48000)
        if not evidence['legacy_native_unowned_meter_conflict']:
            self.assertEqual(evidence['score'], {key: expected[key] for key in matched.METER_KEYS})
            COUNTS['canonical_meter_comparisons'] += 1
        COUNTS['cell_oracle_comparisons'] += 1
        return cell

    def test_405_public_projection_pairs_canonical_meter_and_owned_support(self):
        global MAX_SOURCE_SECONDS
        for i, value in enumerate(public_projection.matrix()):
            started = time.monotonic()
            truth = truth_for(value['total_frames'], period=24000, offset=0 if i % 2 else 1000,
                              numerator=3 if i % 3 else 4)
            left, right = self.legacy_cell(value, truth), self.context_cell(value, truth)
            for domain in ('G', 'M', 'D'):
                self.assertLessEqual(left[domain], right[domain])
            if left['G']:
                self.assertEqual({k: left['correct'][k] for k in matched.FAMILIES[:4]},
                                 {k: right['correct'][k] for k in matched.FAMILIES[:4]})
            for family, domain in (('meter', 'M'), ('downbeat', 'D')):
                if left[domain]:
                    self.assertEqual(left['correct'][family], right['correct'][family])
            COUNTS['within_arm_no_support_loss_pairs'] += 1
            for family in ('grid_joint', 'meter', 'downbeat'):
                for stratum in ('all', 'reference_complete' if family.startswith('grid_') else 'reference_incomplete'):
                    self.ledger([left], [right], 'context_vs_legacy_original', family, stratum)
            elapsed = time.monotonic() - started
            self.assertLess(elapsed, 60)
            MAX_SOURCE_SECONDS = max(MAX_SOURCE_SECONDS, elapsed)
        self.assertEqual(i + 1, 405)

    def test_registered_examples_and_support_gain_not_shared_fix(self):
        for example in registration.examples():
            p = example['ledger']
            pair = next(row for row in matched.COMPARISONS if row[0] == p['comparison'])
            left, right = [], []
            for ls, rs, lc, rc, complete, conflict in example['rows']:
                l, r = facts(ls, lc, complete, p['family'], pair[1]), facts(rs, rc, complete, p['family'], pair[2])
                l['legacy_native_unowned_meter_conflict'] = conflict
                left.append(l)
                right.append(r)
            self.assertEqual(encoded(self.ledger(left, right, p['comparison'], p['family'])), encoded(p))

    def test_all_88_truth_states_all_comparisons_and_strata(self):
        states = 0
        for family in matched.FAMILIES:
            for ls, rs, lc, rc, complete in product(range(2), repeat=5):
                if lc > ls or rc > rs or family.startswith('grid_') and not complete and (lc or rc):
                    continue
                states += 1
                for name, lrep, rrep in matched.COMPARISONS:
                    left, right = [facts(ls, lc, complete, family, lrep)], [facts(rs, rc, complete, family, rrep)]
                    for stratum in ('all', 'reference_complete', 'reference_incomplete'):
                        self.ledger(left, right, name, family, stratum)
        self.assertEqual(states, 88)

    def test_complete_incomplete_400_200_and_unequal_group_rates(self):
        left, right = [], []
        for i in range(600):
            complete = int(i < 400)
            left.append(facts(1, int(i % 4 == 0) * complete, complete, 'grid_joint', 'context'))
            right.append(facts(int(i % 5 != 0), int(i % 5 != 0 and i % 3 == 0) * complete,
                               complete, 'grid_joint', 'context'))
        all_record = self.ledger(left, right, 'candidate_vs_original_context', 'grid_joint')
        self.assertEqual(all_record['counts']['reference_complete_windows'], 400)
        self.assertEqual(all_record['counts']['reference_incomplete_windows'], 200)
        for stratum in ('reference_complete', 'reference_incomplete'):
            self.ledger(left, right, 'candidate_vs_original_context', 'grid_joint', stratum)
        groups = [self.ledger(left[:7], right[:7], 'candidate_vs_original_context', 'grid_joint', level='group'),
                  self.ledger(left[7:], right[7:], 'candidate_vs_original_context', 'grid_joint', level='group')]
        self.assertEqual({k: sum(g['counts'][k] for g in groups) for k in matched.COUNT_KEYS}, all_record['counts'])
        rates = [Fraction(g['right_conditional_accuracy']['numerator'], g['right_conditional_accuracy']['denominator']) for g in groups]
        pooled = all_record['right_conditional_accuracy']
        self.assertNotEqual(sum(rates) / 2, Fraction(pooled['numerator'], pooled['denominator']))
        self.ledger(left[:8], right[:8], 'candidate_vs_original_context', 'grid_joint', level='source')

    def test_unowned_legacy_is_conflict_context_is_error(self):
        points = [{'position_q32': q * matched.Q, 'ordinal': i} for i, q in enumerate((1000, 25000))]
        meter = {'numerator': 4, 'denominator': 4, 'downbeat_frame': 49000,
                 'downbeat_fraction_q32': 0, 'downbeat_ordinal': 2}
        truth = truth_for()
        result = self.meter(meter, points, 0, 48000, truth, 'legacy')
        self.assertEqual(result['legacy_native_unowned_meter_conflict'], 1)
        self.assertEqual(result['score'], dict.fromkeys(matched.METER_KEYS, 0))
        with self.assertRaises(ValueError):
            matched.meter_evidence(meter, points, 0, 48000, truth, 'context')
        bad = deepcopy(meter)
        bad['downbeat_frame'] = 1001
        with self.assertRaises(ValueError):
            matched.meter_evidence(bad, points, 0, 48000, truth, 'legacy')

    def test_downbeat_support_inside_incomplete_window_and_unscored_meter(self):
        truth = truth_for(offset=1000)
        for frame, expected_d in ((1000, 1), (0, 0)):
            meter = dict(numerator=4, denominator=4, downbeat_frame=frame, downbeat_fraction_q32=0, downbeat_ordinal=0)
            result = self.meter(meter, [{'position_q32': frame * matched.Q, 'ordinal': 0}], 0, 240000, truth, 'context')
            self.assertEqual((result['reference_complete'], result['meter_supported'], result['downbeat_supported']), (0, 1, expected_d))
            self.assertEqual(result['score']['meter_correct'], 1)

    def test_q32_tolerance_endpoint_and_stable_local_period_ties(self):
        beats = [0, 10000, 21000, 33000, 46000, 60000, 75000, 91000, 108000]
        truth = dict(beat_frames=beats, downbeat_frames=[33000, 91000], meter_numerator=3, meter_denominator=4)
        for reference in (0, 15000, 33000, 62000, 91000, 108000):
            self.assertEqual(matched._local_period(beats, reference), canonical_meter.canonical._local_period(beats, reference))
        local = canonical_meter.canonical._local_period(beats, 33000)
        threshold = Fraction.from_float(local * .10)
        edge = int(threshold * matched.Q)
        for delta in (-1, 0, 1):
            q = 33000 * matched.Q + edge + delta
            meter = dict(numerator=3, denominator=4, downbeat_frame=q >> 32,
                         downbeat_fraction_q32=q & (matched.Q - 1), downbeat_ordinal=7)
            result = self.meter(meter, [{'position_q32': q, 'ordinal': 7}], 0, 120000, truth, 'context')
            proxy = {'first_frame': 0, 'end_frame': 120000, 'selected': {'kind': 'accepted',
                     'anchor_frame': 0, 'tempo_millibpm': 240000, 'confidence': 0},
                     'meter': {'kind': 'estimated', **meter}}
            expected = canonical_meter.score_window(proxy, truth, 48000)
            self.assertEqual(result['score'], {key: expected[key] for key in matched.METER_KEYS})
            COUNTS['canonical_meter_comparisons'] += 1
        # Exclusive window boundary versus inclusive reference beat-span endpoint.
        q = 108000 * matched.Q
        m = dict(numerator=3, denominator=4, downbeat_frame=108000, downbeat_fraction_q32=0, downbeat_ordinal=8)
        self.assertEqual(self.meter(m, [{'position_q32': q, 'ordinal': 8}], 0, 108001, truth, 'context')['downbeat_supported'], 1)
        with self.assertRaises(ValueError):
            matched.meter_evidence(m, [], 0, 108000, truth, 'context')

    def test_missing_meter_validates_truth_and_points_before_abstention(self):
        truth = truth_for()
        self.assertEqual(self.meter(None, [], 0, 1000, truth, 'context')['score'], dict.fromkeys(matched.METER_KEYS, 0))
        invalid_truths = [None, {}, {**truth, 'beat_frames': []}, {**truth, 'beat_frames': [0]},
                         {**truth, 'beat_frames': [0, 0]}, {**truth, 'beat_frames': [False, 10]},
                         {**truth, 'downbeat_frames': [1]}, {**truth, 'meter_numerator': True},
                         {**truth, 'meter_denominator': 0}, {**truth, 'private_labels': []}]
        for value in invalid_truths:
            with self.assertRaises(ValueError):
                matched.meter_evidence(None, [], 0, 1000, value, 'context')
        for points in ((), [{'position_q32': 1000 * matched.Q, 'ordinal': 0}],
                       [{'position_q32': 0, 'ordinal': True}], [{'position_q32': 0.0, 'ordinal': 0}],
                       [{'position_q32': 1, 'ordinal': 0}, {'position_q32': 1, 'ordinal': 1}]):
            with self.assertRaises(ValueError):
                matched.meter_evidence(None, points, 0, 1000, truth, 'context')

    def test_even_local_median_half_frames_and_cutoff_ties(self):
        tie_beats = [0, 1000, 4000, 5000, 9000, 10000, 14000]
        self.assertEqual(matched._local_period(tie_beats, 5000), 1000.0)
        self.assertEqual(matched._local_period(tie_beats, 5000),
                         canonical_meter.canonical._local_period(tie_beats, 5000))
        truth = dict(beat_frames=[0, 10001, 20003], downbeat_frames=[0], meter_numerator=4, meter_denominator=4)
        local = canonical_meter.canonical._local_period(truth['beat_frames'], 0)
        self.assertEqual(local, 10001.5)
        edge = int(Fraction.from_float(local * .10) * matched.Q)
        for delta in (-1, 0, 1):
            q = edge + delta
            meter = dict(numerator=4, denominator=4, downbeat_frame=q >> 32,
                         downbeat_fraction_q32=q & (matched.Q - 1), downbeat_ordinal=0)
            result = self.meter(meter, [{'position_q32': q, 'ordinal': 0}], 0, 30000, truth, 'context')
            proxy = {'first_frame': 0, 'end_frame': 30000, 'selected': {'kind': 'accepted',
                     'anchor_frame': 0, 'tempo_millibpm': 240000, 'confidence': 0}, 'meter': {'kind': 'estimated', **meter}}
            expected = canonical_meter.score_window(proxy, truth, 48000)
            self.assertEqual(result['score'], {key: expected[key] for key in matched.METER_KEYS})
            COUNTS['canonical_meter_comparisons'] += 1

    def test_raw_unknown_successor_cannot_be_used_as_nominal_projection_cell(self):
        truth = truth_for(total=72000)
        raw = [(0, 24000 * matched.Q), (24000 * matched.Q, 24000 * matched.Q), (48000 * matched.Q, None)]
        points = [{'position_q32': q, 'ordinal': i} for i, (q, _) in enumerate(raw)]
        evidence = self.meter(None, points, 0, 72000, truth, 'context')
        actual_score = representation.score_points(raw, truth, 0, 72000)
        self.assertEqual(actual_score['unknown_successor_points'], 1)
        with self.assertRaises(ValueError):
            matched.make_cell(actual_score, evidence)
        nominal_score = representation.score_points([(q, 24000 * matched.Q) for q, _ in raw], truth, 0, 72000)
        self.assertEqual(matched.make_cell(nominal_score, evidence)['correct']['grid_joint'], 1)

    def test_corrupt_meter_binding_and_cell_evidence_rejected(self):
        value = public_projection.source([1000 + 24000 * i for i in range(12)], [0, 4, 8])
        p = projection.project_window(**value)
        meter, truth = normalize_meter(p['meter']), truth_for(value['total_frames'])
        for change in (dict(downbeat_ordinal=99), dict(downbeat_fraction_q32=True), dict(numerator=0),
                       dict(denominator=8), dict(downbeat_frame=-1), dict(extra=1)):
            with self.assertRaises(ValueError):
                matched.meter_evidence({**meter, **change}, p['points'], 0, value['total_frames'], truth, 'context')
        evidence = self.meter(meter, p['points'], 0, value['total_frames'], truth, 'context')
        grid = representation.score_points([(b['position_q32'], p['model']['period_q32']) for b in p['points']], truth, 0, value['total_frames'])
        for change in (lambda v: v.update(point_count=0), lambda v: v.update(reference_complete=True),
                       lambda v: v['score'].update(downbeat_scored=0), lambda v: v.update(legacy_native_unowned_meter_conflict=1)):
            bad = deepcopy(evidence)
            change(bad)
            with self.assertRaises(ValueError):
                matched.make_cell(grid, bad)
        for key in grid:
            bad = deepcopy(grid)
            bad[key] = True
            with self.assertRaises(ValueError):
                matched.make_cell(bad, evidence)

    def test_cell_pairs_validate_before_filtering_and_capacity_errors(self):
        base = facts(1, 1, 1, 'grid_joint', 'context')
        for change in (lambda v: v.update(G=False), lambda v: v.update(M=2),
                       lambda v: v.update(reference_complete=0), lambda v: v.update(representation='legacy'),
                       lambda v: v.update(first_frame=1), lambda v: v['correct'].update(grid_period=0),
                       lambda v: v['correct'].update(meter=1), lambda v: v.update(extra=0)):
            bad = deepcopy(base)
            change(bad)
            with self.assertRaises(ValueError):
                matched.make_ledger([base], [bad], 'candidate_vs_original_context', 'grid_joint', 'reference_incomplete')
        for left, right, level in (([base], [], 'source'), ([base] * 9, [base] * 9, 'source'),
                                    ([base] * 601, [base] * 601, 'cohort'), ((base,), [base], 'source')):
            with self.assertRaises(ValueError):
                matched.make_ledger(left, right, 'candidate_vs_original_context', 'grid_joint', level=level)
        for options in (dict(comparison='candidate_only'), dict(family='key'), dict(stratum='supported'), dict(level='track')):
            args = dict(comparison='candidate_vs_original_context', family='grid_joint')
            args.update(options)
            with self.assertRaises(ValueError):
                matched.make_ledger([], [], **args)
        empty = self.ledger([], [], 'candidate_vs_original_context', 'meter')
        self.assertIsNone(empty['left_conditional_accuracy'])

    def test_conflicts_count_windows_once_and_output_has_no_aliases(self):
        cell = facts(0, 0, 0, 'meter', 'legacy')
        cell['legacy_native_unowned_meter_conflict'] = 1
        record = self.ledger([cell], [deepcopy(cell)], 'candidate_vs_original_legacy', 'meter')
        self.assertEqual(record['counts']['legacy_native_unowned_meter_conflicts'], 1)
        record['counts']['windows'] = 99
        again = self.ledger([cell], [cell], 'candidate_vs_original_legacy', 'meter')
        self.assertEqual(again['counts']['windows'], 1)
        corrupt = deepcopy(cell)
        corrupt['G'] = 0
        with self.assertRaises(ValueError):
            matched.make_ledger([corrupt], [cell], 'candidate_vs_original_legacy', 'meter')

    def test_maximum_reference_and_point_inputs(self):
        truth = {'beat_frames': list(range(3072)), 'downbeat_frames': list(range(0, 3072, 4)),
                 'meter_numerator': 4, 'meter_denominator': 4}
        points = [{'position_q32': i * matched.Q, 'ordinal': i} for i in range(3072)]
        meter = dict(numerator=4, denominator=4, downbeat_frame=0, downbeat_fraction_q32=0, downbeat_ordinal=0)
        self.meter(meter, points, 0, 1440000, truth, 'context')
        with self.assertRaises(ValueError):
            matched.meter_evidence(None, points + [{'position_q32': 3072 * matched.Q, 'ordinal': 3072}], 0, 1440000, truth, 'context')

    def test_no_imports_io_callbacks_or_label_file_access(self):
        path = ROOT / 'tools/apta_context_matched.py'
        tree = ast.parse(path.read_text())
        self.assertFalse(any(isinstance(n, (ast.Import, ast.ImportFrom)) for n in ast.walk(tree)))
        forbidden = {'open', '__import__', 'eval', 'exec', 'compile', 'globals', 'locals', 'getattr', 'setattr', 'input', 'print'}
        self.assertFalse({n.id for n in ast.walk(tree) if isinstance(n, ast.Name)} & forbidden)
        oracle_tree = ast.parse((ROOT / 'tests/unit/apta_context_matched_oracle.py').read_text())
        self.assertEqual({n.module for n in ast.walk(oracle_tree) if isinstance(n, ast.ImportFrom)}, {'fractions', 'statistics'})
        script = '''import builtins, pathlib, sys
text = pathlib.Path(sys.argv[1]).read_text()
code = compile(text, sys.argv[1], 'exec')
def blocked(*args, **kwargs): raise AssertionError('unexpected import or I/O')
builtins.open = blocked
builtins.__import__ = blocked
builtins.input = blocked
scope = {}
exec(code, scope)
truth={'beat_frames':[0,24000,48000], 'downbeat_frames':[0], 'meter_numerator':4,'meter_denominator':4}
e=scope['meter_evidence'](None,[],0,48000,truth,'context')
assert e['meter_supported']==0
r=scope['make_ledger']([],[],'candidate_vs_original_context','meter')
assert r['left_conditional_accuracy'] is None
'''
        result = subprocess.run([sys.executable, '-I', '-c', script, str(path)], capture_output=True, text=True, timeout=60)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, '', ''))


if __name__ == '__main__':
    suite = unittest.main(exit=False)
    print(encoded({'tests_run': suite.result.testsRun, 'failures': len(suite.result.failures),
                   'errors': len(suite.result.errors), 'skips': len(suite.result.skipped),
                   'counts': dict(COUNTS), 'max_source_seconds': MAX_SOURCE_SECONDS}))
    sys.exit(not suite.result.wasSuccessful())
