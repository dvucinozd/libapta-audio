#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated R4 preparation, archive safety and bounded instrumentation tests."""
import importlib.util
from copy import deepcopy
import io as bytesio
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time_v2_metadata as runner
import apta_dense_runner_support as io


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='libapta-r4-prep-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.driver = module('apta_coupled_time_v2_metadata_preflight')

    def test_fixed100_generated_geometry_and_topology(self):
        counts, families = [], set()
        for index in range(100):
            events, truth, windows, ordinals = self.driver.generated_source(index)
            a = events['beat_frames']
            self.assertTrue(all(type(x) is int for x in a))
            self.assertTrue(all(x < y for x, y in zip(a, a[1:])))
            self.assertGreaterEqual(a[0], 0)
            self.assertLess(a[-1], self.driver.TOTAL)
            self.assertEqual(events['downbeat_frames'], [a[i] for i in ordinals])
            self.assertEqual([w['first_frame'] for w in windows], [0] + [w['end_frame'] for w in windows[:-1]])
            self.assertEqual(windows[-1]['end_frame'], self.driver.TOTAL)
            self.assertEqual(len(windows), 6)
            self.assertEqual(self.driver.generated_source(index), (events, truth, windows, ordinals))
            counts.append(len(a))
            families.add(truth['genre'])
        self.assertEqual(min(counts), 20)
        self.assertEqual(max(counts), 148)
        self.assertEqual(len(families), 4)

    def test_archive_regular_bytes_and_unsafe_members(self):
        archive = self.root / 'regular.tar'
        with tarfile.open(archive, 'w') as stream:
            info = tarfile.TarInfo('folder/file.txt')
            info.size = 5
            stream.addfile(info, bytesio.BytesIO(b'hello'))
        destination = self.root / 'regular'
        self.driver.extract_regular_archive(archive, destination)
        self.assertEqual((destination / 'folder/file.txt').read_bytes(), b'hello')
        for index, (name, kind) in enumerate((('../escape', tarfile.REGTYPE), ('/escape', tarfile.REGTYPE),
                                             ('.git/config', tarfile.REGTYPE), ('link', tarfile.SYMTYPE),
                                             ('hard', tarfile.LNKTYPE))):
            path = self.root / ('bad%d.tar' % index)
            with tarfile.open(path, 'w') as stream:
                info = tarfile.TarInfo(name)
                info.type = kind
                info.linkname = '../outside'
                stream.addfile(info)
            with self.assertRaises(ValueError):
                self.driver.extract_regular_archive(path, self.root / ('bad%d' % index))
        self.assertFalse((self.root / 'escape').exists())

    def test_storage_and_real_tracked_inventory(self):
        mounts = '1 0 0:1 / / rw - ext4 root rw\n2 1 0:2 / /mnt/d rw - 9p D: rw\n'
        self.assertEqual(self.driver.filesystem_class('/mnt/d/generated-fixture', mounts), 'windows-mounted')
        self.assertEqual(self.driver.filesystem_class('/home/generated-fixture', mounts), 'linux')
        inventory = self.driver.source_inventory(ROOT, self.driver.git(ROOT, 'rev-parse', 'HEAD'))
        self.assertGreaterEqual(len(inventory), 1024)
        self.assertEqual(len({r['path'] for r in inventory}), len(inventory))
        self.assertGreater(sum(r['bytes'] for r in inventory), 0)

    def test_cli_has_no_private_input_or_limit_options(self):
        for flag in ('--inputs', '--labels', '--audio', '--timeout', '--count'):
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('apta_coupled_time_v2_metadata_preflight.py')),
                                     '--output', str(self.root / 'absent'), '--source-fixture', str(self.root / 'source'),
                                     flag, 'unused'], capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unrecognized arguments', result.stderr)
        self.assertFalse((self.root / 'absent').exists())

    def test_existing_campaign_directory_is_preserved(self):
        output = self.root / 'existing'
        output.mkdir()
        marker = output / 'marker'
        marker.write_bytes(b'preserved')
        with self.assertRaisesRegex(ValueError, 'exclusive'):
            self.driver.run(output, self.root / 'source')
        self.assertEqual(marker.read_bytes(), b'preserved')

    def test_full_campaign_requires_explicit_frozen_runtime(self):
        with self.assertRaisesRegex(ValueError, 'explicit frozen runtime snapshot'):
            self.driver.run(self.root / 'output', self.root / 'source')
        self.assertFalse((self.root / 'output').exists())

    def test_small_canonical_summary_processes_with_withheld_labels(self):
        source = self.root / 'source'
        source.mkdir()
        (source / 'fixture.txt').write_text('generated source\n')
        for args in (('init',), ('add', 'fixture.txt'), ('-c', 'user.name=Generated', '-c',
                     'user.email=generated@example.invalid', 'commit', '-m', 'fixture')):
            subprocess.run(['git', '-C', str(source), *args], capture_output=True, check=True)
        cfg, inputs, inventory, labels = self.driver.generate_fixture(self.root / 'generated', source, count=2)
        attempt = self.root / 'attempt'
        cfg['attempt_path_sha256'] = runner.hashlib.sha256(str(attempt).encode()).hexdigest()
        config = self.root / 'settings.json'
        io.write_new(config, cfg)
        hidden = labels.with_suffix('.hidden')
        labels.rename(hidden)
        for role, extra in (('construct', ['--inputs', inputs]), ('verify', []), ('evaluate', ['--inventory', inventory])):
            if role == 'evaluate':
                hidden.rename(labels)
            result = subprocess.run([sys.executable, str(self.driver.BOOTSTRAP), str(config), role,
                                     '--attempt', str(attempt), *map(str, extra)],
                                    capture_output=True, text=True, timeout=180)
            stderr_path = attempt / (role + '.phase.stderr')
            error = stderr_path.read_text() if stderr_path.exists() else ''
            self.assertEqual(result.returncode, 0, result.stderr + error)
        scientific = json.loads((attempt / 'evaluate.support.completion.json').read_text())
        self.assertEqual(scientific['sources'], 2)
        self.assertEqual(scientific['totals']['candidate_nominal']['windows'], 12)
        import apta_coupled_time_metadata_checks as checks
        # A numeric control, not a historical R4 identity: actual new-file
        # provenance is checked by the process/receipt tests independently.
        legacy_shape = deepcopy(scientific)
        for key in ('operational_registration_sha256', 'metadata_amendment_sha256', 'context_sha256', 'reference_support_sha256'):
            del legacy_shape[key]
        legacy_shape['format'] = 'libapta-coupled-time-evaluation-1'
        for group in runner.DIAGNOSTIC_GROUPS:
            del legacy_shape['totals'][group]['reference_incomplete_windows']
        checks.compare_scientific(legacy_shape, scientific, cfg)
        changes = [lambda value: value['totals']['old_coarse'].__setitem__('meter_correct', value['totals']['old_coarse']['meter_correct'] + 1),
                   lambda value: value['paired']['ols'].__setitem__('joint_breaks', value['paired']['ols']['joint_breaks'] + 1),
                   lambda value: value['gates'].__setitem__('zero_ols_meter_breaks', not value['gates']['zero_ols_meter_breaks']),
                   lambda value: value['by_genre'][next(iter(value['by_genre']))]['legacy'].__setitem__('beat_f1', 0.12345)]
        for change in changes:
            changed = deepcopy(scientific)
            change(changed)
            changed['status'] = 'EXPOSED_DIAGNOSTIC_PASS_ONLY' if all(changed['gates'].values()) else 'REJECTED'
            with self.assertRaises(ValueError): checks.compare_scientific(legacy_shape, changed, cfg)
        for role in ('construct', 'verify'):
            for index in range(2):
                value = json.loads((attempt / role / (str(index) + '.access.json')).read_text())
                self.assertEqual(value['format'], 'generated-access-counts-1')
                self.assertNotIn('opened', value)
                self.assertLess(len(json.dumps(value)), 1024)
                self.assertEqual(value['unselected_source_file_opens'], 0)
                self.assertEqual(value['unselected_source_file_binds'], 0)
                self.assertEqual(value['tracked_fixture_file_binds'], 0)
                self.assertGreater(value['open_calls'], 0)
                self.assertGreater(value['bind_calls'], 0)
                self.assertEqual(value['selected_observation_opens'], 8)

    def test_registered_component_closure_and_unchanged_generator(self):
        import ast
        op, components = self.driver.component_bindings()
        self.assertEqual(set(components), set(op['preserved_r4_component_sha256']) | set(op['future_preflight_components']))
        self.assertEqual(len(components), 37)
        old = module('apta_coupled_time_v2_preflight')
        for index in range(100):
            self.assertEqual(self.driver.generated_source(index), old.generated_source(index))
        def body(path, name):
            return ast.dump(next(n for n in ast.parse(path.read_text()).body if isinstance(n, ast.FunctionDef) and n.name == name))
        for name in ('generated_source', 'archive_source', 'extract_regular_archive', 'filesystem_class'):
            self.assertEqual(body(Path(self.driver.__file__), name), body(Path(old.__file__), name))

    def test_exact_six_leaf_parity_rejects_changed_old_fields_and_types(self):
        import apta_coupled_time_metadata_checks as checks
        from apta_1_1_coupled_time_evaluation_v2_metadata import add_support
        old = {'track': 'generated', 'scores': {name: [{'windows': 1, 'reference_complete_windows': 1,
                 **{key: 0 for key in ('complete_sequence_windows', 'joint_windows', 'one_to_one_phase_windows',
                 'period_correct_points', 'period_windows', 'phase_correct_points', 'phase_windows', 'points',
                 'scored_points', 'unknown_successor_points')}}] for name in runner.DIAGNOSTIC_GROUPS}}
        old['scores']['windows'] = [{'nominal': deepcopy(old['scores']['candidate_nominal'][0]),
                                     'actual': deepcopy(old['scores']['candidate_actual'][0]), 'old_value': 1}]
        old['scores']['old_unrelated'] = {'reference_incomplete_windows': 'keep'}
        new = {'track': old['track'], 'scores': add_support(old['scores'],
            [{'windows': 1, 'reference_complete_windows': 1, 'reference_incomplete_windows': 0}])}
        cfg = runner.registered_settings()
        checks.compare_source(old, new, cfg)
        for field, value in (('old_value', 2), ('old_value', 1.0), ('extra_metadata', None)):
            changed = deepcopy(new)
            changed['scores']['windows'][0][field] = value
            with self.assertRaises(ValueError): checks.compare_source(old, changed, cfg)
        for field, value in (('reference_incomplete_windows', 1), ('reference_incomplete_windows', True), ('points', 1)):
            changed = deepcopy(new)
            changed['scores']['ols_actual'][0][field] = value
            with self.assertRaises(ValueError): checks.compare_source(old, changed, cfg)
        changed = deepcopy(new)
        del changed['scores']['windows'][0]['nominal']['reference_incomplete_windows']
        with self.assertRaises(ValueError): checks.compare_source(old, changed, cfg)

    def test_scientific_parity_keeps_all_gate_family_and_paired_values(self):
        import apta_coupled_time_metadata_checks as checks
        # Exact comparison complements the actual generated payload mutations in
        # test_small_canonical_summary_processes_with_withheld_labels.
        for left, right in (({'count': 1}, {'count': True}), ({'count': 1}, {'count': 1.0}),
                            ({'gate': False}, {'gate': True}), ({'family': .5}, {'family': .500001})):
            with self.assertRaises(ValueError): checks.identical(left, right)
        checks.identical({'old': [1, True, None, .5]}, {'old': [1, True, None, .5]})

    def test_fixed_runtime_launcher_has_no_arbitrary_command_or_real_inputs(self):
        import apta_coupled_time_v2_metadata_runtime as runtime
        snapshot = Path('/generated/snapshot')
        receipt = {'interpreter': '/generated/canonical-python', 'roots': []}
        for suite in runtime.SUITES:
            commands = runtime.commands(snapshot, receipt, suite)
            self.assertTrue(commands)
            for command in commands:
                self.assertIn('-v', command)
                self.assertTrue(any('/tests/unit/' in str(arg) for arg in command))
        for flag in ('--command', '--audio', '--labels', '--inputs', '--timeout', '--count'):
            result = subprocess.run([sys.executable, runtime.__file__, 'check', '--snapshot', str(snapshot),
                                     '--suite', 'process', flag, 'unused'], capture_output=True, text=True, timeout=20)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unrecognized arguments', result.stderr)

    def test_generated_support_counts_are_measured_complements(self):
        from apta_reference_support import reference_support
        from collections import Counter
        counts = Counter()
        for index in range(100):
            _events, truth, windows, _ordinals = self.driver.generated_source(index)
            for row in reference_support(truth['beat_frames'], windows): counts.update(row)
        self.assertEqual(dict(counts), {'windows': 600, 'reference_complete_windows': 400, 'reference_incomplete_windows': 200})

    def test_saved_baseline_controls_cover_all600_without_new_scoring(self):
        import apta_coupled_time_metadata_checks as checks
        result = checks.saved_controls()
        self.assertEqual(result['sources'], 100)
        self.assertEqual(result['windows'], 600)
        self.assertEqual(result['baseline_candidate_hashes_verified'], 100)
        self.assertEqual(result['reference_support']['reference_incomplete_windows'], 200)
        self.assertEqual(result['legacy_scientific_verdict'], 'REJECTED')
        self.assertEqual(result['legacy_gate_pass_count'], 14)
        self.assertEqual(result['new_candidates'], 0)
        self.assertEqual(result['new_scoring_calls'], 0)
        self.assertFalse(result['fresh_candidate_parity_measured'])
        self.assertFalse(result['saved_labels_read'])


if __name__ == '__main__':
    unittest.main()
