#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated R4 preparation, archive safety and bounded instrumentation tests."""
import importlib.util
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
import apta_1_1_coupled_time_v2 as runner
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
        self.driver = module('apta_coupled_time_v2_preflight')

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
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('apta_coupled_time_v2_preflight.py')),
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
        scientific = json.loads((attempt / 'evaluate.completion.json').read_text())
        self.assertEqual(scientific['sources'], 2)
        self.assertEqual(scientific['totals']['candidate_nominal']['windows'], 12)
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


if __name__ == '__main__':
    unittest.main()
