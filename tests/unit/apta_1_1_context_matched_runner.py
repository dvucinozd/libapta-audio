#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""P5 public arithmetic process/operational tests; never evaluate fixed C3 cases."""
from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests/unit')]
import apta_1_1_context_matched_runner as runner
import apta_dense_runner_support as io


def fixture(destination, count=2, meter=4):
    """Fixed public recipe. No model/solver, existing source or saved case read."""
    import apta_1_1_context_matched_evaluation as evaluate
    destination.mkdir(parents=True, exist_ok=False)
    beats = [1000 + 12000 * i for i in range(60)]
    events = {'beat_frames': beats, 'downbeat_frames': beats[::4]}
    windows = [{'first_frame': i * 120000, 'end_frame': (i + 1) * 120000} for i in range(6)]
    truth_beats = list(range(48000, 672001, 12000))
    rows, truths, bindings = [], [], {}
    for i in range(count):
        track = 'public-' + str(i)
        truth = {'track': track, 'genre': 'public-' + str(i % 2), 'requested_frames': 720000,
                 'beat_frames': truth_beats, 'downbeat_frames': truth_beats[::4],
                 'meter_numerator': meter, 'meter_denominator': 4}
        truths.append(truth)
        candidate = {'format': 'libapta-public-fixed-events-1', 'total_frames': 720000, 'status': 'candidate_events',
            'events': {**events, 'beat_ordinals': list(range(60)), 'downbeat_ordinals': list(range(0, 60, 4))},
            'windows': [{**w, 'status': 'candidate_events'} for w in windows]}
        controls = [evaluate.representation.ref.project(beats, events['downbeat_frames'], **{
                    'first': w['first_frame'], 'end': w['end_frame']}) for w in windows]
        scores = [evaluate.representation.ref.annotated.score_window(w, truth, 48000) for w in controls]
        saved = {'models': {'beat_this': {'event_metrics': evaluate.comparison.event_metrics(truth, events),
            'windows': [{'candidate': w, 'score': score} for w, score in zip(controls, scores)]}}, 'coarse': scores}
        coarse = {'requested_frames': 720000, 'sample_rate': 48000, 'windows': controls}
        frozen = evaluate.metadata.add_support(evaluate.old.score_source(candidate, events, windows, truth, saved, coarse),
                                               evaluate.reference_support(truth_beats, windows))
        values = {'original': {'format': 'libapta-public-original-events-1', 'track': track, 'events': events},
                  'candidate': candidate, 'baseline': saved, 'coarse': coarse, 'frozen': {'track': track, 'scores': frozen}}
        row = {'index': i, 'id': track, 'total_frames': 720000, 'windows': windows, 'known_gaps': []}
        for name, value in values.items():
            entry = runner.write(destination / name / (str(i) + '.json'), value)
            row[name] = entry
            bindings[entry['path']] = entry['sha256']
        rows.append(row)
    labels = runner.write(destination / 'labels.json', {'tracks': truths})
    bindings[labels['path']] = labels['sha256']
    scope = {'format': 'libapta-context-matched-inputs-1', 'kind': 'PUBLIC_PREFLIGHT', 'protocol_sha256': runner.PROTOCOL,
        'rows': rows, 'labels': labels, 'bindings': bindings, 'expected_support': {'windows': count * 6,
        'reference_complete_windows': count * 4, 'reference_incomplete_windows': count * 2}}
    runner.validate_scope(scope)
    return runner.write(destination / 'scope.json', scope)


class RunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = Path(os.environ['APTA_MATCHED_P5_OUTPUT']).resolve()
        cls.base.mkdir(parents=True, exist_ok=False)
        cls.runtime = runner.leaf(Path(os.environ['APTA_MATCHED_P5_RUNTIME']).resolve())
        runner.check_runtime(cls.runtime)
        cls.scope = fixture(cls.base / 'public-inputs')

    def setUp(self):
        self.work = self.base / self._testMethodName
        self.work.mkdir()

    def prepare(self, name='attempt', scope=None):
        path = self.work / name
        runner.stage(scope or self.scope, self.runtime, path)
        return path

    def project(self, attempt):
        return runner.dispatch('project', attempt, self.runtime)

    def verify(self, attempt):
        return runner.dispatch('verify', attempt, self.runtime)

    def test_01_complete_public_100_source_600_window_pipeline(self):
        scope = fixture(self.work / 'full-public-inputs', 100)
        report = runner.run_public(scope, self.runtime, self.work / 'full-attempt')
        self.assertEqual((report['sources'], report['windows'], report['reference_complete_windows'],
                          report['reference_incomplete_windows'], report['evaluated_cells']), (100, 600, 400, 200, 2400))
        self.assertEqual(report['pair_screen'], 'PAIR_NOT_REJECTED_ON_GENERATED_DIAGNOSTICS')
        attempt = self.work / 'full-attempt'
        manifest = runner.read(runner.leaf(attempt / 'manifest.json'))
        records = [runner.completion(attempt, role, manifest) for role in runner.ROLES]
        self.assertEqual([r['reference_labels_loaded'] for r in records], [False, False, True])
        self.assertEqual([r['projection_count'] for r in records], [1200] * 3)
        self.assertTrue(all(0 < r['self_max_rss_kib'] <= 262144 for r in records))
        pids = [runner.read(runner.leaf(attempt / (role + '.supervised.resources.json')))['root_pid'] for role in runner.ROLES]
        self.assertEqual(len(set(pids)), 3)
        self.assertLess(io.tree_bytes([attempt]), 67108864)
        self.assertEqual(len(report['ledgers']), (100 + 2 + 1) * 4 * 6 * 3)
        self.assertEqual(len(report['cell_totals_by_level']), 103)
        for table in report['cell_totals_by_level']:
            for cell in table['cells'].values():
                for subtotal in cell.values():
                    self.assertEqual(subtotal['windows'], subtotal['reference_complete_windows'] + subtotal['reference_incomplete_windows'])

    def test_02_missing_changed_labels_do_not_reach_projection_or_verification(self):
        entry = runner.read(self.scope)['labels']
        label = Path(entry['path'])
        backup = label.with_suffix('.saved')
        label.rename(backup)
        try:
            absent = self.prepare('absent-labels')
            self.project(absent)
            self.verify(absent)
        finally:
            backup.rename(label)
        changed_scope = fixture(self.work / 'changed-labels-inputs', meter=3)
        changed = self.prepare('changed-labels', changed_scope)
        self.project(changed)
        self.verify(changed)
        for i in range(2):
            self.assertEqual((absent / 'project' / (str(i) + '.json')).read_bytes(),
                             (changed / 'project' / (str(i) + '.json')).read_bytes())

    def test_03_evaluation_requires_complete_independent_verification(self):
        attempt = self.prepare()
        self.project(attempt)
        with self.assertRaises(ValueError):
            runner.dispatch('evaluate', attempt, self.runtime, self.scope)
        self.assertTrue((attempt / 'INVALID.json').is_file())
        self.assertFalse((attempt / 'matched-report.json').exists())
        with self.assertRaises(ValueError):
            self.verify(attempt)

    def test_04_corrupt_projection_cannot_verify_or_resume(self):
        for rehash in (False, True):
            attempt = self.prepare('rehash' if rehash else 'stale')
            self.project(attempt)
            path = attempt / 'project/0.json'
            value = runner.strict_json(path.read_bytes())
            value['original'][0]['point_count'] += 1
            path.write_text(json.dumps(value))
            if rehash:
                complete = runner.strict_json((attempt / 'project.complete.json').read_bytes())
                complete['records'][0] = runner.leaf(path)
                (attempt / 'project.complete.json').write_text(json.dumps(complete))
            with self.assertRaises(ValueError):
                self.verify(attempt)
            self.assertFalse((attempt / 'verify.complete.json').exists())
            if rehash:
                self.assertIn('independent projection replay differs', (attempt / 'verify.supervised.stderr').read_text())

    def test_05_job_reference_hint_and_source_order_rejected(self):
        for case in ('extra', 'duplicate'):
            attempt = self.prepare(case)
            manifest = runner.read(runner.leaf(attempt / 'manifest.json'))
            if case == 'extra':
                path = Path(manifest['jobs'][0]['path'])
                value = runner.strict_json(path.read_bytes())
                value['reference_labels'] = []
                path.write_text(json.dumps(value))
                manifest['jobs'][0] = runner.leaf(path)
            else:
                manifest['jobs'][1] = manifest['jobs'][0]
            (attempt / 'manifest.json').write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                self.project(attempt)

    def test_06_corrupt_runtime_and_component_identities_rejected(self):
        for name in ('components', 'profiles'):
            descriptor = runner.read(self.runtime)
            if name == 'components':
                descriptor[name][str(ROOT / 'tools/apta_context_projection.py')] = '0' * 64
            else:
                key = next(iter(descriptor['profiles']['project']))
                descriptor['profiles']['project'][key] = '0' * 64
            corrupt = runner.write(self.work / (name + '.json'), descriptor)
            if name == 'components':
                with self.assertRaises(ValueError):
                    runner.check_runtime(corrupt)
            else:
                attempt = self.work / 'runtime-attempt'
                runner.stage(self.scope, corrupt, attempt)
                with self.assertRaises(ValueError):
                    runner.dispatch('project', attempt, corrupt)

    def test_07_scope_wrong_types_unknown_real_and_mixed_tokens_rejected(self):
        original = runner.read(self.scope)
        for change in (lambda v: v.update(kind='REAL_GTZAN'), lambda v: v.update(extra=True),
                       lambda v: v['rows'][0].update(index=False), lambda v: v['rows'][0].update(known_gaps=[{'first_frame': 0, 'end_frame': 1}]),
                       lambda v: v['rows'][1].update(id=v['rows'][0]['id']),
                       lambda v: v['expected_support'].update(reference_incomplete_windows=0)):
            value = deepcopy(original)
            change(value)
            with self.assertRaises(ValueError):
                runner.validate_scope(value)
        value = deepcopy(original)
        value['kind'] = 'C3_FIXED_GENERATED'
        with self.assertRaises(ValueError):
            runner.validate_scope(value)
        with self.assertRaises(ValueError):
            runner.authorize('C3_FIXED_GENERATED', '0' * 64, self.runtime, self.work / 'unauthorized')

    def test_08_completion_shapes_bool_missing_and_stale_chain_rejected(self):
        attempt = self.prepare()
        self.project(attempt)
        manifest = runner.read(runner.leaf(attempt / 'manifest.json'))
        path = attempt / 'project.complete.json'
        original = path.read_bytes()
        for change in (lambda v: v.update(source_count=True), lambda v: v.pop('records'),
                       lambda v: v.update(runtime_sha256='0' * 64), lambda v: v.update(extra=1),
                       lambda v: v.update(format='libapta-coupled-time-completion-1')):
            value = runner.strict_json(original)
            change(value)
            path.write_text(json.dumps(value))
            with self.assertRaises(ValueError):
                runner.completion(attempt, 'project', manifest)
        path.write_bytes(original)

    def test_09_failed_legacy_parity_publishes_no_evaluation_completion(self):
        scope = fixture(self.work / 'corrupt-legacy')
        body = runner.read(scope)
        path = Path(body['rows'][0]['frozen']['path'])
        value = runner.strict_json(path.read_bytes())
        value['scores']['old_event_metrics']['beat_f1'] = 0.0
        path.write_text(json.dumps(value))
        body['rows'][0]['frozen'] = runner.leaf(path)
        body['bindings'][str(path)] = io.sha(path)
        scope = runner.write(self.work / 'bad-scope.json', body)
        attempt = self.prepare(scope=scope)
        self.project(attempt)
        self.verify(attempt)
        with self.assertRaises(ValueError):
            runner.dispatch('evaluate', attempt, self.runtime, scope)
        self.assertFalse((attempt / 'evaluate.complete.json').exists())
        self.assertFalse((attempt / 'matched-report.json').exists())

    def test_10_supervisor_timeout_rss_output_are_failures(self):
        cases = [('timeout', [sys.executable, '-c', 'import time;time.sleep(1)'], .05, 262144, 1048576),
                 ('rss', [sys.executable, '-c', 'import time;x=bytearray(8000000);time.sleep(1)'], 5, 1024, 1048576),
                 ('output', [sys.executable, '-c', 'print("x"*100000)'], 5, 262144, 2048)]
        for name, command, seconds, rss, cap in cases:
            directory = self.work / name
            directory.mkdir()
            with self.assertRaises(ValueError):
                io.supervise(command, seconds, rss, cap, [directory], directory / 'negative')
            record = runner.read(runner.leaf(directory / 'negative.resources.json'))
            self.assertEqual(record['status'], 'INVALID')

    def test_11_strict_json_and_repeated_destination(self):
        for raw in ('{"a":1,"a":2}', '{"a":NaN}', '{"a":1e10000}'):
            with self.assertRaises(ValueError):
                runner.strict_json(raw)
        self.assertEqual(runner.strict_json('{"q":9007199254740993}')['q'], 9007199254740993)
        attempt = self.prepare()
        with self.assertRaises(ValueError):
            runner.stage(self.scope, self.runtime, attempt)

    def test_12_actual_label_blind_runtime_profiles_exclude_scorers(self):
        descriptor = runner.read(self.runtime)
        for role in ('project', 'verify'):
            self.assertFalse(any(Path(name).stem.startswith(runner.FORBIDDEN) for name in descriptor['profiles'][role]))
        self.assertTrue(any(Path(name).stem == 'apta_1_1_context_matched_evaluation' for name in descriptor['profiles']['evaluate']))

    def test_13_context_support_gain_does_not_override_shared_correctness_break(self):
        import apta_1_1_context_matched_evaluation as evaluate
        import apta_context_projection as projection
        import apta_context_projection_oracle as oracle
        windows = [{'first_frame': 48000, 'end_frame': 240000}]
        original = [1000, 25000] + [48000 + 24000 * i for i in range(9)]
        original[-1] -= 1
        candidate_beats = [1000, 25000, 48001] + [48000 + 24000 * i - 160 for i in range(1, 9)]
        db = [2, 6, 10]
        events = {'beat_frames': original, 'downbeat_frames': [original[i] for i in db]}
        new = {'beat_frames': candidate_beats, 'downbeat_frames': [candidate_beats[i] for i in db]}
        candidate = {'status': 'candidate_events', 'total_frames': 264000, 'events': new,
                     'windows': [{**windows[0], 'status': 'candidate_events'}]}
        truth = {'track': 'public-context', 'genre': 'public-context', 'requested_frames': 264000,
                 'beat_frames': list(range(0, 240001, 24000)), 'downbeat_frames': [48000, 144000, 240000],
                 'meter_numerator': 4, 'meter_denominator': 4}
        control = evaluate.representation.ref.project(original, events['downbeat_frames'], 48000, 240000)
        score = evaluate.representation.ref.annotated.score_window(control, truth, 48000)
        saved = {'models': {'beat_this': {'event_metrics': evaluate.comparison.event_metrics(truth, events),
                 'windows': [{'candidate': control, 'score': score}]}}, 'coarse': [score]}
        coarse = {'requested_frames': 264000, 'sample_rate': 48000, 'windows': [control]}
        frozen = {'scores': evaluate.metadata.add_support(evaluate.old.score_source(candidate, events, windows,
            truth, saved, coarse), evaluate.reference_support(truth['beat_frames'], windows))}
        projections = {}
        for arm, beats in (('original', original), ('coupled', candidate_beats)):
            p = projection.project_window(beats, db, 264000, 48000, 240000)
            self.assertEqual(p, oracle.independent_project_window(beats, db, 264000, 48000, 240000))
            projections[arm] = [p]
        source = evaluate.evaluate_source(events, candidate, windows, truth, saved, coarse, frozen, projections)
        self.assertEqual(source['cells']['coupled_legacy'][0]['normalized']['G'], 0)
        self.assertEqual(source['cells']['coupled_context'][0]['normalized']['G'], 1)
        report = evaluate.summarize([source], ['public-context'])
        self.assertEqual(report['pair_screen'], 'PAIR_REJECTED_ON_GENERATED_DIAGNOSTICS')
        self.assertIn('grid_complete_sequence_shared_correctness_break', report['pair_screen_faults'])
        gain = next(row['ledger'] for row in report['ledgers'] if row['ledger']['level'] == 'cohort'
                    and row['ledger']['comparison'] == 'context_vs_legacy_coupled'
                    and row['ledger']['family'] == 'meter' and row['ledger']['stratum'] == 'all')
        self.assertEqual(gain['counts']['right_only_supported'], 1)
        self.assertEqual(gain['counts']['fixes_on_both'], 0)
        for cell in report['cell_totals'].values():
            self.assertTrue(cell['reference_incomplete'])
            self.assertTrue(all(value == 0 for value in cell['reference_incomplete'].values()))

    def test_14_embedded_parent_runtime_cannot_override_bound_interpreter(self):
        value = runner.read(self.runtime)
        value['parent_receipt']['interpreter'] = '/usr/bin/false'
        corrupt = runner.write(self.work / 'parent-override.json', value)
        with self.assertRaisesRegex(ValueError, 'embedded parent runtime receipt differs'):
            runner.check_runtime(corrupt)


if __name__ == '__main__':
    unittest.main()
