#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated-only v2 process, integrity, access-scaling and parity regressions."""
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
import apta_1_1_coupled_time_v2 as runner
import apta_dense_runner_support as io


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='libapta-ct-v2-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self):
        previous = module('apta_1_1_coupled_time_runner').RunnerTests()
        previous.root = self.root
        previous.fixture()
        self.cfg = previous.cfg
        self.attempt, self.inputs, self.inventory = previous.attempt, previous.inputs, previous.inventory
        self.source, self.labels = previous.source, previous.labels
        self.schema = json.loads((ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-SCHEMA.json').read_text())
        op = json.loads((ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-PROTOCOL.json').read_text())
        self.cfg.update(schema=self.schema, operational=op,
                        operational_registration_sha256=runner.OPERATIONAL_SHA256,
                        authorization_sha256='1' * 64)
        for name in ('tools/apta_1_1_coupled_time_v2.py', 'tests/unit/apta_coupled_time_v2_runner_bootstrap.py'):
            self.cfg['bindings'][str(ROOT / name)] = io.sha(ROOT / name)
        self.settings_path = self.root / 'v2-settings.json'
        self.save()

    def save(self):
        self.settings_path.write_text(json.dumps(self.cfg, sort_keys=True, indent=2) + '\n')

    def cli(self, role, *extra):
        result = subprocess.run([sys.executable, str(Path(__file__).with_name('apta_coupled_time_v2_runner_bootstrap.py')),
                                 str(self.settings_path), role, *map(str, extra), '--attempt', str(self.attempt)],
                                capture_output=True, text=True, timeout=180)
        return result

    def stderr(self, role):
        path = self.attempt / (role + '.phase.stderr')
        return path.read_text() if path.exists() else ''

    def construct(self):
        result = self.cli('construct', '--inputs', self.inputs)
        self.assertEqual(result.returncode, 0, result.stderr + self.stderr('construct'))

    def verify(self):
        result = self.cli('verify')
        self.assertEqual(result.returncode, 0, result.stderr + self.stderr('verify'))

    def test_complete_three_process_canonical_or_named_stub(self):
        self.fixture()
        hidden = self.labels.with_suffix('.hidden')
        self.labels.rename(hidden)
        self.construct()
        self.verify()
        hidden.rename(self.labels)
        result = self.cli('evaluate', '--inventory', self.inventory)
        self.assertEqual(result.returncode, 0, result.stderr + self.stderr('evaluate'))
        complete = json.loads((self.attempt / 'evaluate.v2.completion.json').read_text())
        self.assertTrue(complete['reference_labels_loaded'])
        scientific = json.loads((self.attempt / 'evaluate.completion.json').read_text())
        self.assertEqual(complete['evaluation_report_sha256'], io.sha(self.attempt / 'evaluate.completion.json'))
        self.assertEqual(scientific['candidate_event_means'], scientific['old_event_means'])
        self.assertEqual(len(scientific['gates']), 16)
        self.assertEqual(scientific['status'], 'REJECTED')
        for role in ('construct', 'verify', 'evaluate'):
            self.assertTrue((self.attempt / (role + '.v2.supervised.json')).exists())
            self.assertFalse((self.attempt / (role + '.supervised.json')).exists())
            for path in (self.attempt / role).glob('*.json'):
                if role != 'evaluate' and path.stem.isdigit():
                    value = json.loads(path.read_text())
                    runner.validate_record(value, self.cfg, 'worker')
                    self.assertAlmostEqual(value['wall_seconds'], sum(value['timings'].values()), places=6)
        # Independently validate actual emitted records with the registered
        # Draft2020-12 library, outside the production/runtime dependency set.
        records = [self.attempt / (role + '.context.json') for role in ('construct', 'verify', 'evaluate')]
        records += [runner.completion_path(self.attempt, role) for role in ('construct', 'verify', 'evaluate')]
        records += [self.attempt / (role + '.v2.supervised.json') for role in ('construct', 'verify', 'evaluate')]
        records += [self.attempt / role / (str(i) + '.json') for role in ('construct', 'verify') for i in range(2)]
        checked = subprocess.run(['/usr/bin/python3', '-c',
            'import json,jsonschema,sys; s=json.load(open(sys.argv[1])); '
            'v=jsonschema.Draft202012Validator(s); '
            '[v.validate(json.load(open(p))) for p in sys.argv[2:]]',
            str(ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-SCHEMA.json'), *map(str, records)],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(checked.returncode, 0, checked.stderr)
        if os.environ.get('APTA_CT_V2_OPERATIONAL_REPORT'):
            io.write_new(Path(os.environ['APTA_CT_V2_OPERATIONAL_REPORT']), {
                'sources': 2, 'windows': 4, 'event_metrics': self.cfg['test_metric'],
                'status': scientific['status'], 'candidate_old_event_macros_equal': True,
                'attempt_bytes': io.tree_bytes((self.attempt,)),
                'phases': {role: {key: json.loads((self.attempt / (role + '.phase.resources.json')).read_text())[key]
                           for key in ('status', 'wall_seconds', 'max_observed_process_rss_kib')}
                           for role in ('construct', 'verify', 'evaluate')}})

    def test_missing_verification_blocks_labels(self):
        self.fixture()
        self.construct()
        self.labels.unlink()
        result = self.cli('evaluate', '--inventory', self.inventory)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.attempt / 'evaluate').exists())
        self.assertFalse((self.attempt / 'evaluate.completion.json').exists())

    def test_changed_generated_labels_preserve_candidates(self):
        self.fixture()
        self.construct()
        original = [(self.attempt / 'candidates' / (str(i) + '.json')).read_bytes() for i in range(2)]
        self.attempt = self.root / 'second-generated-attempt'
        self.cfg['attempt_path_sha256'] = hashlib_path(self.attempt)
        value = json.loads(self.labels.read_text())
        for row in value['tracks']:
            row['beat_frames'] = [123, 456]
        self.labels.write_text(json.dumps(value))
        self.save()
        self.construct()
        self.assertEqual(original, [(self.attempt / 'candidates' / (str(i) + '.json')).read_bytes() for i in range(2)])

    def test_corrupt_candidate_rejected(self):
        self.fixture()
        self.construct()
        with (self.attempt / 'candidates/0.json').open('ab') as stream:
            stream.write(b' ')
        self.assertNotEqual(self.cli('verify').returncode, 0)
        self.assertFalse((self.attempt / 'verify.v2.supervised.json').exists())

    def test_no_overwrite_and_dirty_source(self):
        self.fixture()
        self.construct()
        digest = io.sha(self.attempt / 'construct.completion.json')
        self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
        self.assertEqual(digest, io.sha(self.attempt / 'construct.completion.json'))
        (self.source / 'fixture.txt').write_text('changed\n')
        self.assertNotEqual(self.cli('verify').returncode, 0)

    def test_persistent_past_and_future_source_mutations_fail_closed(self):
        for after, target in ((0, 1), (1, 0)):
            with self.subTest(after=after, target=target), tempfile.TemporaryDirectory(dir=self.root) as temp:
                original = self.root
                self.root = Path(temp)
                self.fixture()
                rows = json.loads(self.inputs.read_text())['sources']
                self.cfg['test_mutation'] = {'after_index': after, 'kind': 'append', 'path': rows[target]['events_path']}
                self.save()
                result = self.cli('construct', '--inputs', self.inputs)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.attempt / 'construct.v2.supervised.json').exists())
                self.assertTrue((self.attempt / 'construct.INVALID.json').exists())
                self.root = original

    def test_persistent_tracked_mutation_prevents_success(self):
        self.fixture()
        self.cfg['test_mutation'] = {'after_index': 1, 'kind': 'append', 'path': str(self.source / 'fixture.txt')}
        self.save()
        self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
        self.assertFalse((self.attempt / 'construct.completion.json').exists())

    def test_worker_poisoned_binding_map_rejected(self):
        self.fixture()
        rows = json.loads(self.inputs.read_text())['sources']
        self.cfg['test_mutation'] = {'after_index': 0, 'kind': 'poison_report', 'path': rows[1]['events_path']}
        self.save()
        self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
        self.assertIn('binding scope', self.stderr('construct'))

    def test_v1_and_stale_v2_receipts_rejected(self):
        self.fixture()
        self.construct()
        path = self.attempt / 'construct.completion.json'
        value = json.loads(path.read_text())
        value['format'] = 'libapta-coupled-time-frozen-1'
        path.write_text(json.dumps(value))
        self.assertNotEqual(self.cli('verify').returncode, 0)
        self.assertFalse((self.attempt / 'verify.completion.json').exists())

    def test_stale_verification_blocks_scientific_payload(self):
        self.fixture()
        self.construct()
        self.verify()
        path = self.attempt / 'verify.v2.supervised.json'
        value = json.loads(path.read_text())
        value['completion_sha256'] = '0' * 64
        path.write_text(json.dumps(value))
        self.assertNotEqual(self.cli('evaluate', '--inventory', self.inventory).returncode, 0)
        self.assertFalse((self.attempt / 'evaluate.completion.json').exists())

    def test_exact_types_strict_json_and_paths(self):
        self.fixture()
        self.construct()
        value = json.loads((self.attempt / 'construct.context.json').read_text())
        for replacement in (True, 2.0, -1):
            bad = deepcopy(value)
            bad['source_count'] = replacement
            with self.assertRaises(ValueError):
                runner.validate_record(bad, self.cfg, 'context')
        bad = deepcopy(value)
        bad['authorization_sha256'] += '\n'
        with self.assertRaises(ValueError):
            runner.validate_record(bad, self.cfg, 'context')
        for raw in ('{"a":1,"a":2}', '{"a":NaN}', '{"a":Infinity}', '{"a":1e999}'):
            with self.assertRaises(ValueError):
                runner.strict_json(raw)
        for path in ('relative', '/generated/../other'):
            with self.assertRaises(ValueError):
                runner.safe_path(path)
        target = self.root / 'target'
        target.write_text('original')
        alias = self.root / 'alias'
        alias.symlink_to(target)
        with self.assertRaises(ValueError):
            runner.safe_path(alias)

    def test_real_public_and_internal_roles_require_new_tokens(self):
        import ast
        source = (ROOT / 'tools/apta_1_1_coupled_time_v2.py').read_text()
        self.assertNotIn('os.environ', source)
        self.assertNotIn('test_metric', source)
        for node in ast.walk(ast.parse(source)):
            if isinstance(node, ast.Import):
                self.assertFalse(any(alias.name.startswith('tests') for alias in node.names))
        base = [sys.executable, str(ROOT / 'tools/apta_1_1_coupled_time_v2.py')]
        for args in (['construct', '--inputs', str(self.root / 'none')], ['verify'],
                     ['evaluate', '--inventory', str(self.root / 'none')],
                     ['_phase', 'construct'], ['_worker', 'construct', '0', '--context-sha256', '0' * 64]):
            result = subprocess.run([*base, *args, '--attempt', str(self.root / 'disabled')], capture_output=True, text=True, timeout=60)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('real execution disabled', result.stderr)
        self.assertFalse((self.root / 'disabled').exists())

    def test_actual_parent_resource_failure_retains_invalid(self):
        for limits in ({'max_phase_seconds': .01}, {'max_process_rss_kib': 1}, {'max_attempt_output_bytes': 1}):
            with self.subTest(limits=limits), tempfile.TemporaryDirectory(dir=self.root) as temp:
                original = self.root
                self.root = Path(temp)
                self.fixture()
                self.cfg['test_phase_limits'] = limits
                self.save()
                self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
                self.assertTrue((self.attempt / 'construct.INVALID.json').exists())
                self.assertFalse((self.attempt / 'construct.v2.supervised.json').exists())
                self.root = original

    def test_actual_source_timeout_prevents_completion(self):
        self.fixture()
        self.cfg['protocol']['limits']['max_source_seconds'] = .05
        self.cfg['test_worker_delay'] = .5
        self.save()
        self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
        self.assertTrue((self.attempt / 'construct.INVALID.json').exists())
        self.assertFalse((self.attempt / 'construct.completion.json').exists())

    def test_actual_worker_rss_and_output_fail_closed(self):
        base = self.root
        for kind in ('rss', 'output'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory(dir=base) as temp:
                self.root = Path(temp)
                self.fixture()
                if kind == 'rss':
                    self.cfg['protocol']['limits']['max_process_rss_kib'] = 65536
                    self.cfg['test_worker_memory_mb'] = 96
                else:
                    self.cfg['protocol']['limits']['max_attempt_output_bytes'] = 1048576
                    self.cfg['test_worker_output_bytes'] = 2097152
                self.save()
                self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
                self.assertTrue((self.attempt / 'construct.INVALID.json').exists())
                self.assertFalse((self.attempt / 'construct.v2.supervised.json').exists())
        self.root = base

    def test_live_descendant_prevents_supervised_success(self):
        self.fixture()
        self.cfg['test_live_descendant'] = True
        self.save()
        self.assertNotEqual(self.cli('construct', '--inputs', self.inputs).returncode, 0)
        resource = json.loads((self.attempt / 'construct.phase.resources.json').read_text())
        self.assertIn('orphan', resource['failure'])
        self.assertFalse((self.attempt / 'construct.v2.supervised.json').exists())

    def test_context_wrong_role_index_digest_and_swapped_plain_job(self):
        self.fixture()
        self.construct()
        path = self.attempt / 'construct.context.json'
        original = json.loads(path.read_text())
        # Corrupt disposable generated state to model the boundary before worker0.
        for name in ('construct.completion.json', 'construct.v2.supervised.json',
                     'construct/0.json', 'candidates/0.json'):
            (self.attempt / name).unlink()
        for kind in ('role', 'index', 'digest', 'job', 'source'):
            value = deepcopy(original)
            if kind == 'role':
                value['role'] = 'verify'
            if kind == 'index':
                value['rows'][0]['index'] = 1
            if kind == 'source':
                value['rows'][0]['source_id'] = value['rows'][1]['source_id']
            if kind == 'job':
                jobpath = self.attempt / 'jobs/0.json'
                job = json.loads(jobpath.read_text())
                job['beat_frames'][0] += 1
                jobpath.write_text(json.dumps(job))
                value['rows'][0]['job_sha256'] = io.sha(jobpath)
            path.write_text(json.dumps(value, sort_keys=True, indent=2) + '\n')
            expected = '0' * 64 if kind == 'digest' else io.sha(path)
            result = self.cli('_worker', 'construct', '0', '--context-sha256', expected)
            self.assertNotEqual(result.returncode, 0, kind)

    def test_parent_global_checks_precede_completion(self):
        self.fixture()
        self.cfg['test_boundary_trace'] = True
        self.save()
        self.construct()
        self.verify()
        for role, count in (('construct', 1), ('verify', 2)):
            trace = json.loads((self.attempt / (role + '.boundary-trace.json')).read_text())
            self.assertEqual(trace.count('snapshot'), 1)
            self.assertEqual(trace.count('cohort_audit'), 1)
            self.assertEqual(trace.count('global_recheck'), count)
            self.assertEqual(trace[-2:], ['global_recheck', 'completion_written'])

    def test_snapshot_cannot_replace_registered_component_digest(self):
        self.fixture()
        self.attempt.mkdir()
        key = next(iter(self.cfg['bindings']))
        original = self.cfg['bindings'][key]

        def conflicting_snapshot(_root, bindings):
            bindings[key] = '0' * 64
            return io.git(self.source, 'rev-parse', 'HEAD')

        with mock.patch.object(runner, 'settings', return_value=self.cfg), \
                mock.patch.object(io, 'source_snapshot', side_effect=conflicting_snapshot), \
                mock.patch.object(io, 'supervise', side_effect=AssertionError('must stop before workers')):
            with self.assertRaisesRegex(ValueError, 'immutable expected binding changed'):
                runner.phase('construct', self.attempt, self.inputs)
        self.assertEqual(self.cfg['bindings'][key], original)
        self.assertFalse((self.attempt / 'construct.context.json').exists())

    def test_closed_attempt_and_completed_worker_cannot_replay(self):
        self.fixture()
        self.construct()
        context = self.attempt / 'construct.context.json'
        result = self.cli('_worker', 'construct', '0', '--context-sha256', io.sha(context))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no replay', result.stderr)
        io.write_new(self.attempt / 'verify.INVALID.json', {'status': 'INVALID'})
        self.assertNotEqual(self.cli('verify').returncode, 0)
        self.assertFalse((self.attempt / 'verify.phase.stdout').exists())

    def test_processed_candidate_mutation_prevents_verification(self):
        self.fixture()
        self.construct()
        self.cfg['test_mutation'] = {'role': 'verify', 'after_index': 1, 'kind': 'append',
                                     'path': str(self.attempt / 'candidates/0.json')}
        self.save()
        self.assertNotEqual(self.cli('verify').returncode, 0)
        self.assertFalse((self.attempt / 'verify.v2.supervised.json').exists())

    def test_generated_wrapper_parity_for_registered_shapes(self):
        import apta_coupled_time as numerical
        import apta_coupled_time_preflight as fixtures
        base = self.root
        for name in ('affine25000', 'jitter', 'ramp', 'full_bound_active', 'crossing', 'short', 'gap'):
            with self.subTest(case=name), tempfile.TemporaryDirectory(dir=base) as temp:
                self.root = Path(temp)
                self.fixture()
                manifest = json.loads(self.inputs.read_text())
                a, d, total, windows, gaps = fixtures.generated(name)
                for row in manifest['sources']:
                    row.update(beat_frames=a, downbeat_ordinals=d, total_frames=total, windows=windows, known_gaps=gaps)
                    for pathkey, hashkey in (('events_path', 'events_sha256'), ('capture_record_path', 'capture_record_sha256')):
                        path = Path(row[pathkey])
                        value = json.loads(path.read_text())
                        value['events']['beat_frames'] = a
                        value['events']['downbeat_frames'] = [a[i] for i in d]
                        if 'geometry' in value:
                            value['geometry']['windows'] = windows
                        path.write_text(json.dumps(value))
                        row[hashkey] = io.sha(path)
                manifest['bindings'] = {row[k]: row[h] for row in manifest['sources'] for k, h in
                    (('events_path', 'events_sha256'), ('capture_record_path', 'capture_record_sha256'))}
                self.inputs.write_text(json.dumps(manifest))
                self.cfg['protocol']['input_identity'].update(windows=2 * len(windows), ct03_label_free_inputs_sha256=io.sha(self.inputs))
                self.save()
                expected = json.dumps(numerical.construct(a, d, total, windows, gaps),
                                      sort_keys=True, separators=(',', ':'), allow_nan=False).encode()
                self.construct()
                self.verify()
                self.assertEqual((self.attempt / 'candidates/0.json').read_bytes(), expected)
        self.root = base

    def test_selected_source_file_access_scaling_1_10_100(self):
        base_root = self.root
        for count in (1, 10, 100):
            with self.subTest(count=count), tempfile.TemporaryDirectory(dir=base_root) as temp:
                original = base_root
                self.root = Path(temp)
                self.fixture()
                manifest = json.loads(self.inputs.read_text())
                template = manifest['sources'][0]
                rows, bound = [], {}
                for i in range(count):
                    row = deepcopy(template)
                    row['track'] = 'scaling-%d' % i
                    for pathkey, hashkey in (('events_path', 'events_sha256'), ('capture_record_path', 'capture_record_sha256')):
                        path = self.root / ('%s-%d.json' % (pathkey, i))
                        data = json.loads(Path(template[pathkey]).read_text())
                        data['track'] = row['track']
                        io.write_new(path, data)
                        row[pathkey], row[hashkey] = str(path), io.sha(path)
                        bound[str(path)] = row[hashkey]
                    rows.append(row)
                manifest['sources'], manifest['bindings'] = rows, bound
                self.inputs.write_text(json.dumps(manifest))
                self.cfg['protocol']['input_identity'].update(sources=count, windows=count * 2,
                    ct03_label_free_inputs_sha256=io.sha(self.inputs))
                self.attempt.mkdir()
                for name in ('jobs', 'candidates', 'construct'):
                    (self.attempt / name).mkdir()
                jobs = []
                for i, row in enumerate(rows):
                    path = self.attempt / 'jobs' / (str(i) + '.json')
                    io.write_new(path, {key: row[key] for key in runner.PLAIN})
                    jobs.append(io.sha(path))
                context = {'format': self.cfg['operational']['records']['context'], 'role': 'construct',
                           **runner.common(self.cfg, io.git(self.source, 'rev-parse', 'HEAD')),
                           'input_manifest_path': str(self.inputs), 'source_count': count, 'window_count': count * 2,
                           'source_order': [r['track'] for r in rows], 'upstream': runner.empty_upstream(),
                           'rows': [{'index': i, 'source_id': r['track'], 'job_sha256': jobs[i], 'candidate_sha256': None}
                                    for i, r in enumerate(rows)]}
                path = self.attempt / 'construct.context.json'
                io.write_new(path, context)
                self.save()
                result = self.cli('_worker', 'construct', str(count - 1), '--context-sha256', io.sha(path))
                self.assertEqual(result.returncode, 0, result.stderr)
                audit = json.loads((self.attempt / 'construct' / (str(count - 1) + '.access.json')).read_text())
                unselected = {r[key] for r in rows[:-1] for key in ('events_path', 'capture_record_path')}
                unselected |= {str(self.attempt / 'jobs' / (str(i) + '.json')) for i in range(count - 1)}
                self.assertFalse(unselected & set(audit['opened']))
                self.assertFalse(unselected & set(audit['bound']))
                self.assertNotIn(str(self.source / 'fixture.txt'), audit['bound'])
                own = rows[-1]['events_path']
                self.assertEqual(audit['bound'].count(own), 3)  # read before/after + final content recheck
                # Hand-sealed generated metadata is a selected-worker unit fixture,
                # not a claim of whole-cohort supervision or R4 execution.
                payload = (self.attempt / 'candidates' / (str(count - 1) + '.json')).read_bytes()
                candidate_hash = io.sha(self.attempt / 'candidates' / (str(count - 1) + '.json'))
                for i in range(count - 1):
                    (self.attempt / 'candidates' / (str(i) + '.json')).write_bytes(payload)
                complete = {'format': self.cfg['operational']['records']['completion'], 'role': 'construct',
                            **runner.common(self.cfg, context['source_revision']), 'status': 'PASS',
                            'context_sha256': io.sha(path), 'source_count': count, 'window_count': count * 2,
                            'reference_labels_loaded': False, 'upstream': runner.empty_upstream(),
                            'evaluation_report_sha256': None, 'bindings': {str(path): io.sha(path)},
                            'candidates': [{'index': i, 'source_id': r['track'], 'sha256': candidate_hash}
                                           for i, r in enumerate(rows)],
                            'worker_reports': [{'index': i, 'source_id': r['track'], 'sha256': '2' * 64}
                                               for i, r in enumerate(rows)]}
                io.write_new(self.attempt / 'construct.completion.json', complete)
                resource_path = self.attempt / 'construct.phase.resources.json'
                io.write_new(resource_path, {'status': 'PASS', 'failure': None, 'wall_seconds': 1,
                                            'max_observed_process_rss_kib': 20000, 'process_group_monitored': True})
                io.write_new(self.attempt / 'construct.v2.supervised.json', {
                    'format': self.cfg['operational']['records']['supervision'], 'role': 'construct',
                    **runner.common(self.cfg, context['source_revision']), 'status': 'PASS',
                    'context_sha256': io.sha(path), 'completion_sha256': io.sha(self.attempt / 'construct.completion.json'),
                    'resources_sha256': io.sha(resource_path)})
                vcontext = deepcopy(context)
                vcontext['role'], vcontext['upstream'] = 'verify', runner.upstream(self.attempt, 'verify')
                for row in vcontext['rows']:
                    row['candidate_sha256'] = candidate_hash
                vpath = self.attempt / 'verify.context.json'
                io.write_new(vpath, vcontext)
                (self.attempt / 'verify').mkdir()
                result = self.cli('_worker', 'verify', str(count - 1), '--context-sha256', io.sha(vpath))
                self.assertEqual(result.returncode, 0, result.stderr)
                vaudit = json.loads((self.attempt / 'verify' / (str(count - 1) + '.access.json')).read_text())
                unselected |= {str(self.attempt / 'candidates' / (str(i) + '.json')) for i in range(count - 1)}
                self.assertFalse(unselected & set(vaudit['opened']))
                self.assertFalse(unselected & set(vaudit['bound']))
                self.assertNotIn(str(self.source / 'fixture.txt'), vaudit['bound'])
                self.assertEqual(vaudit['bound'].count(own), 3)
                self.root = original


def hashlib_path(path):
    import hashlib
    return hashlib.sha256(str(path.resolve()).encode()).hexdigest()


if __name__ == '__main__':
    unittest.main()
