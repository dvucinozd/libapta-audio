#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated CT04 end-to-end isolation, integrity and operational checks."""
from copy import deepcopy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time as runner
import apta_dense_runner_support as io


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='libapta-ct04-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def fixture(self):
        import apta_1_1_neural_comparison as comparison
        oldtests = module('apta_1_1_dense_decoder_runner')
        helper = oldtests.RunnerTests()
        helper.root = self.root
        helper.config = json.loads((ROOT / 'docs/rust/DENSE-DECODER-PROTOCOL.json').read_text())
        canonical = os.environ.get('APTA_CT04_CANONICAL_EVENTS') == '1'
        dense, legacy, prepared = helper.fixture(count=2, event_metric=comparison.event_metrics if canonical else oldtests.metric_stub)
        p = json.loads((ROOT / 'docs/rust/COUPLED-TIME-DEVELOPMENT-PROTOCOL.json').read_text())
        p['input_identity'].update(helper.config['inputs'])
        p['baseline'] = helper.config['baseline']
        oldreport = json.loads((legacy / 'scoring/report.json').read_text())
        p['baseline_event_means'] = oldreport['event_means']['beat_this']
        source = self.root / 'source'
        source.mkdir()
        (source / 'fixture.txt').write_text('generated source\n')
        for args in (('init',), ('add', 'fixture.txt'), ('-c', 'user.name=Generated test', '-c',
                     'user.email=generated@example.invalid', 'commit', '-m', 'fixture')):
            subprocess.run(['git', '-C', str(source), *args], check=True, capture_output=True)
        rows = []
        bindings = {str((legacy / 'scoring/report.json').resolve()): io.sha(legacy / 'scoring/report.json')}
        input_bindings = {}
        for record in oldreport['details']:
            track = record['track']
            events_path, capture_path = legacy / 'beat_this' / (track + '.json'), dense / (track + '.json')
            events = json.loads(events_path.read_text())
            captured = json.loads(capture_path.read_text())
            a, d = events['events']['beat_frames'], events['events']['downbeat_frames']
            windows = [{k: w[k] for k in ('first_frame', 'end_frame')} for w in captured['geometry']['windows']]
            row = {'track': track, 'events_path': str(events_path), 'events_sha256': io.sha(events_path),
                   'capture_record_path': str(capture_path), 'capture_record_sha256': io.sha(capture_path),
                   'source_audio_sha256': events['audio_sha256'], 'total_frames': windows[-1]['end_frame'],
                   'windows': windows, 'beat_frames': a, 'downbeat_ordinals': [a.index(value) for value in d], 'known_gaps': []}
            for name in (events_path, capture_path):
                input_bindings[str(name)] = io.sha(name)
            for field, path in (('coarse', legacy / 'scoring' / (track + '-coarse.json')),
                                ('baseline_score', legacy / 'scoring' / (track + '.json'))):
                row[field + '_path'] = str(path)
                row[field + '_sha256'] = io.sha(path)
                bindings[str(path)] = io.sha(path)
            rows.append(row)
        inventory = {'sources': rows, 'bindings': {**bindings, **input_bindings},
                     'labels_manifest_path': str(prepared / 'manifest.private.json')}
        original_freeze = self.root / 'original-freeze.json'
        io.write_new(original_freeze, {'bindings': {str(ROOT / name): io.sha(ROOT / name)
                     for name in p['component_sha256']}})
        p['input_identity']['original_freeze_sha256'] = io.sha(original_freeze)
        inventory['bindings'][str(original_freeze)] = io.sha(original_freeze)
        inventory_path = self.root / 'inventory.json'
        io.write_new(inventory_path, inventory)
        plain_keys = set(rows[0]) - {'coarse_path', 'coarse_sha256', 'baseline_score_path', 'baseline_score_sha256'}
        inputs = {'format': 'libapta-coupled-time-label-free-inputs-1', 'scope': 'generated',
                  'registration_parent_revision': 'generated', 'source_sample_rate': 48000,
                  'sources': [{k: value for k, value in row.items() if k in plain_keys} for row in rows],
                  'bindings': input_bindings, 'input_order': 'fixed', 'protocol_sha256': p['synthetic_protocol_sha256'],
                  'candidate_constructed': False, 'execution_enabled': False}
        inputs_path = self.root / 'inputs.json'
        io.write_new(inputs_path, inputs)
        p['input_identity']['ct03_private_inventory_sha256'] = io.sha(inventory_path)
        p['input_identity']['ct03_label_free_inputs_sha256'] = io.sha(inputs_path)
        attempt = self.root / 'attempt'
        cfg = {'protocol': p, 'source_root': str(source), 'registration_sha256': runner.REGISTRATION_SHA256,
               'test_metric': 'canonical' if canonical else 'stub',
               'attempt_path_sha256': runner.digest_path(attempt),
               'bindings': {str(ROOT / name): io.sha(ROOT / name) for name in
                            set(runner.RUNTIME_COMPONENTS) | set(p['component_sha256'])}}
        self.cfg, self.source, self.attempt = cfg, source, attempt
        self.inputs, self.inventory = inputs_path, inventory_path
        self.labels = prepared / 'manifest.private.json'
        self.settings_path = self.root / 'settings.json'
        io.write_new(self.settings_path, cfg)

    def cli(self, role, *extra):
        return subprocess.run([sys.executable, str(Path(__file__).with_name('apta_coupled_runner_bootstrap.py')),
                               str(self.settings_path), role, '--attempt', str(self.attempt), *map(str, extra)],
                              capture_output=True, text=True, timeout=180)

    def construct(self):
        result = self.cli('construct', '--inputs', self.inputs)
        self.assertEqual(result.returncode, 0, result.stderr + self.stderr('construct'))

    def verify(self):
        result = self.cli('verify')
        self.assertEqual(result.returncode, 0, result.stderr + self.stderr('verify'))

    def stderr(self, role):
        path = self.attempt / (role + '.phase.stderr')
        return path.read_text() if path.exists() else ''

    def test_complete_three_process_phases_with_absent_then_restored_labels(self):
        self.fixture()
        hidden = self.root / 'hidden-labels.json'
        self.labels.rename(hidden)
        self.construct()
        self.verify()
        hidden.rename(self.labels)
        result = self.cli('evaluate', '--inventory', self.inventory)
        self.assertEqual(result.returncode, 0, result.stderr + self.stderr('evaluate'))
        complete = json.loads((self.attempt / 'evaluate.completion.json').read_text())
        self.assertEqual(complete['status'], 'REJECTED')  # Original absolute real gates unchanged.
        self.assertEqual(complete['candidate_event_means'], complete['old_event_means'])
        self.assertEqual(complete['sources'], 2)
        self.assertEqual(len(complete['gates']), 16)
        resources = {}
        for role in ('construct', 'verify', 'evaluate'):
            data = json.loads((self.attempt / (role + '.phase.resources.json')).read_text())
            self.assertEqual(data['status'], 'PASS')
            resources[role] = {key: data[key] for key in ('wall_seconds', 'max_observed_process_rss_kib',
                                                        'poll_seconds', 'process_group_monitored')}
        if os.environ.get('APTA_CT04_OPERATIONAL_REPORT'):
            io.write_new(Path(os.environ['APTA_CT04_OPERATIONAL_REPORT']), {
                'generated_sources': 2, 'generated_windows': 4, 'phases': resources,
                'scoring_verdict': complete['status'], 'unchanged_old_new_event_macros': True,
                'event_metrics': self.cfg['test_metric'],
                'attempt_output_bytes': io.tree_bytes((self.attempt,)),
                'all_16_original_gates_retained': True})

    def test_missing_verification_stops_before_labels_or_scoring_import(self):
        self.fixture()
        self.construct()
        self.labels.unlink()
        result = self.cli('evaluate', '--inventory', self.inventory)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.attempt / 'evaluate').exists())
        self.assertNotIn('labels', self.stderr('evaluate'))

    def test_label_changes_cannot_change_constructed_candidate_bytes(self):
        self.fixture()
        self.construct()
        original = [(self.attempt / 'candidates' / (str(i) + '.json')).read_bytes() for i in range(2)]
        old_attempt = self.attempt
        self.attempt = self.root / 'second-generated-attempt'
        labels = json.loads(self.labels.read_text())
        for truth in labels['tracks']:
            truth['beat_frames'] = [123, 456]
            truth['downbeat_frames'] = [123]
        self.labels.write_text(json.dumps(labels))
        self.cfg['attempt_path_sha256'] = runner.digest_path(self.attempt)
        self.settings_path.write_text(json.dumps(self.cfg))
        self.construct()
        for i in range(2):
            self.assertEqual(original[i], (self.attempt / 'candidates' / (str(i) + '.json')).read_bytes())
        self.assertTrue((old_attempt / 'construct.completion.json').exists())

    def test_corrupt_candidate_rejected_by_verification(self):
        self.fixture()
        self.construct()
        path = self.attempt / 'candidates/0.json'
        value = json.loads(path.read_text())
        value['events']['beat_frames'][0] += 1
        path.write_text(json.dumps(value))
        result = self.cli('verify')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.attempt / 'verify.completion.json').exists())

    def test_partial_freeze_or_stale_verification_cannot_score(self):
        self.fixture()
        self.construct()
        self.verify()
        path = self.attempt / 'verify.completion.json'
        value = json.loads(path.read_text())
        value['candidate_freeze_sha256'] = '0' * 64
        path.write_text(json.dumps(value))
        result = self.cli('evaluate', '--inventory', self.inventory)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.attempt / 'evaluate').exists())

    def test_no_overwrite_dirty_source_and_stale_component(self):
        self.fixture()
        self.construct()
        before = io.sha(self.attempt / 'construct.completion.json')
        self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
        self.assertEqual(io.sha(self.attempt / 'construct.completion.json'), before)
        (self.source / 'fixture.txt').write_text('dirty\n')
        self.assertNotEqual(self.cli('verify').returncode, 0)
        self.assertFalse((self.attempt / 'verify.completion.json').exists())

    def test_bad_input_hash_and_extra_label_field_fail_before_candidate_freeze(self):
        self.fixture()
        data = json.loads(self.inputs.read_text())
        data['sources'][0]['truth'] = []
        self.inputs.write_text(json.dumps(data))
        # Even an independently bound malformed generated schema is rejected.
        self.cfg['protocol']['input_identity']['ct03_label_free_inputs_sha256'] = io.sha(self.inputs)
        self.settings_path.write_text(json.dumps(self.cfg))
        result = self.cli('construct', '--inputs', self.inputs)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.attempt / 'construct.completion.json').exists())

    def test_unbound_input_modification_rejected(self):
        self.fixture()
        self.inputs.write_text(self.inputs.read_text() + ' ')
        self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
        self.assertFalse((self.attempt / 'construction.json').exists())

    def test_partial_cohort_is_rejected_even_with_updated_manifest_binding(self):
        self.fixture()
        value = json.loads(self.inputs.read_text())
        value['sources'].pop()
        self.inputs.write_text(json.dumps(value))
        self.cfg['protocol']['input_identity']['ct03_label_free_inputs_sha256'] = io.sha(self.inputs)
        self.settings_path.write_text(json.dumps(self.cfg))
        self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
        self.assertFalse((self.attempt / 'construct.completion.json').exists())

    def test_tampered_worker_job_rejected_before_numerics(self):
        self.fixture()
        self.construct()
        path = self.attempt / 'jobs/0.json'
        value = json.loads(path.read_text())
        value['truth'] = []
        path.write_text(json.dumps(value))
        result = self.cli('_worker', 'verify', '0')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.attempt / 'verify/0.json').exists())

    def test_stale_component_identity_rejected_before_new_phase(self):
        self.fixture()
        self.construct()
        key = str(ROOT / 'tools/apta_coupled_time.py')
        self.cfg['bindings'][key] = '0' * 64
        self.settings_path.write_text(json.dumps(self.cfg))
        self.assertNotEqual(self.cli('verify').returncode, 0)
        self.assertFalse((self.attempt / 'verify.completion.json').exists())

    def test_label_mutation_fails_evaluation_binding(self):
        self.fixture()
        self.construct()
        self.verify()
        self.labels.write_text(self.labels.read_text() + ' ')
        self.assertNotEqual(self.cli('evaluate', '--inventory', self.inventory).returncode, 0)
        self.assertFalse((self.attempt / 'evaluate.completion.json').exists())

    def test_baseline_mismatch_cannot_publish_verdict(self):
        self.fixture()
        self.construct()
        self.verify()
        self.cfg['protocol']['baseline']['ols_nominal_joint_windows'] += 1
        self.settings_path.write_text(json.dumps(self.cfg))
        self.assertNotEqual(self.cli('evaluate', '--inventory', self.inventory).returncode, 0)
        self.assertFalse((self.attempt / 'evaluate.completion.json').exists())

    def test_phase_import_boundary_without_evaluator_dependencies(self):
        import ast
        tree = ast.parse((ROOT / 'tools/apta_1_1_coupled_time.py').read_text())
        for node in tree.body:
            if isinstance(node, ast.Import):
                self.assertFalse(any(alias.name in ('apta_coupled_time', 'apta_coupled_time_verify',
                    'apta_1_1_dense_decoder', 'apta_1_1_neural_comparison') for alias in node.names))
        self.assertNotIn('settings_path', (ROOT / 'tools/apta_1_1_coupled_time.py').read_text())

    def test_parent_enforces_timeout_rss_output_and_preserves_invalid(self):
        self.fixture()
        original = deepcopy(self.cfg)
        for kind, key, value in (('timeout', 'max_phase_seconds', .001), ('rss', 'max_process_rss_kib', 1),
                                 ('output', 'max_attempt_output_bytes', 1024)):
            self.attempt = self.root / ('cap-' + kind)
            cfg = deepcopy(original)
            cfg['attempt_path_sha256'] = runner.digest_path(self.attempt)
            cfg['test_phase_limits'] = {key: value}
            self.settings_path.write_text(json.dumps(cfg))
            self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
            self.assertFalse((self.attempt / 'construct.completion.json').exists())
            report = json.loads((self.attempt / 'construct.phase.resources.json').read_text())
            self.assertEqual(report['status'], 'INVALID')
            self.assertIn({'timeout': 'wall-time', 'rss': 'RSS', 'output': 'output'}[kind], report['failure'])

    def test_public_all_roles_remain_disabled_without_real_scope(self):
        for args in (('construct', '--inputs', '/absent'), ('verify',), ('evaluate', '--inventory', '/absent'),
                     ('_phase', 'construct', '--inputs', '/absent'), ('_worker', 'construct', '0')):
            output = self.root / ('never-' + str(len(args)))
            result = subprocess.run([sys.executable, str(ROOT / 'tools/apta_1_1_coupled_time.py'),
                                     *args, '--attempt', str(output)], capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('disabled', result.stderr)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
