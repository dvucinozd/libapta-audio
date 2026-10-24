#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated runtime-copy integrity and actual read-only namespace regressions."""
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
PATH = Path(__file__).with_name('apta_coupled_time_v2_runtime.py')


def module():
    spec = importlib.util.spec_from_file_location('generated_runtime', PATH)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='libapta-frozen-runtime-')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.source = self.base / 'live'
        self.source.mkdir()
        (self.source / 'library').write_bytes(b'original library bytes')
        (self.source / 'alias').symlink_to('library')
        self.snapshot = self.base / 'snapshot'
        self.runtime = module()

    def create(self):
        return self.runtime.create(self.snapshot, [self.source])

    def test_exact_copy_links_and_exclusive_destination(self):
        receipt = self.create()
        self.assertEqual(receipt['regular_files'], 1)
        self.assertEqual(receipt['regular_bytes'], 22)
        self.assertEqual(self.runtime.verify(self.snapshot), receipt)
        with self.assertRaisesRegex(ValueError, 'exclusive'):
            self.create()
        self.assertEqual((self.source / 'library').read_bytes(), b'original library bytes')

    def test_snapshot_byte_link_and_inventory_mutations_reject(self):
        self.create()
        path = self.snapshot / 'tree' / str(self.source).lstrip('/') / 'library'
        path.write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'bytes or links'):
            self.runtime.verify(self.snapshot)
        path.write_bytes(b'original library bytes')
        link = path.with_name('alias')
        link.unlink()
        link.symlink_to('other')
        with self.assertRaisesRegex(ValueError, 'bytes or links'):
            self.runtime.verify(self.snapshot)
        link.unlink()
        link.symlink_to('library')
        (self.snapshot / 'inventory.json').write_text('[]')
        with self.assertRaisesRegex(ValueError, 'identity'):
            self.runtime.verify(self.snapshot)

    def test_host_mutation_during_copy_closes_invalid_snapshot(self):
        copy = self.runtime.shutil.copytree
        def changed(*args, **kwargs):
            result = copy(*args, **kwargs)
            (self.source / 'library').write_bytes(b'changed while copying')
            return result
        with mock.patch.object(self.runtime.shutil, 'copytree', side_effect=changed):
            with self.assertRaisesRegex(ValueError, 'during snapshot creation'):
                self.create()
        self.assertEqual(json.loads((self.snapshot / 'INVALID.json').read_text())['status'], 'INVALID')
        with self.assertRaisesRegex(ValueError, 'closed invalid'):
            self.runtime.verify(self.snapshot)

    def test_actual_namespace_hides_atomic_host_change_and_rejects_writes(self):
        self.create()
        # A running namespace reads the copied value even after the live source is
        # atomically replaced, exactly the update pattern that invalidated R4.
        code = ('from pathlib import Path; import sys; p=Path(sys.argv[1]); '
                'print("ready",flush=True); sys.stdin.readline(); '
                'assert p.read_bytes()==b"original library bytes"; '
                'p.write_bytes(b"must fail")')
        process = subprocess.Popen(self.runtime.sandbox_command(self.snapshot,
            [sys.executable, '-c', code, self.source / 'library']),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertTrue(select.select([process.stdout], [], [], 10)[0])
            self.assertEqual(process.stdout.readline().strip(), 'ready')
            replacement = self.source / 'replacement'
            replacement.write_bytes(b'updated host library')
            replacement.replace(self.source / 'library')
            _, error = process.communicate('continue\n', timeout=20)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn('Read-only file system', error)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        self.assertEqual((self.source / 'library').read_bytes(), b'updated host library')
        self.runtime.verify(self.snapshot)

    def test_namespace_release_child_failure_and_no_persistent_host_change(self):
        self.create()
        with self.assertRaisesRegex(ValueError, 'child failed'):
            self.runtime.execute(self.snapshot, [sys.executable, '-c', 'raise SystemExit(17)'])
        (self.source / 'library').write_bytes(b'host remains writable')
        self.assertEqual((self.source / 'library').read_bytes(), b'host remains writable')
        self.runtime.verify(self.snapshot)

    def test_unchanged_v2_parent_rejects_bound_runtime_mutation(self):
        path = PATH.with_name('apta_1_1_coupled_time_v2_runner.py')
        spec = importlib.util.spec_from_file_location('runtime_veto_fixture', path)
        fixture_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture_module)
        fixture = fixture_module.RunnerTests()
        fixture.root = self.base
        fixture.fixture()
        library = self.base / 'generated-runtime-library.so'
        library.write_bytes(b'generated bound runtime bytes')
        fixture.cfg['bindings'][str(library)] = self.runtime.io.sha(library)
        fixture.cfg['test_mutation'] = {'after_index': 0, 'kind': 'append', 'path': str(library)}
        fixture.save()
        fixture.labels.rename(fixture.labels.with_suffix('.withheld'))
        result = fixture.cli('construct', '--inputs', fixture.inputs)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('bound artifact identity differs', fixture.stderr('construct'))
        self.assertTrue((fixture.attempt / 'construct.INVALID.json').is_file())
        self.assertFalse((fixture.attempt / 'construct.completion.json').exists())
        self.assertFalse((fixture.attempt / 'construct.v2.supervised.json').exists())
        self.assertFalse((fixture.attempt / 'evaluate.completion.json').exists())
        self.assertFalse(fixture.labels.exists())

    def test_unsafe_roots_and_nonmetric_cli_surface(self):
        (self.base / 'symlink-root').symlink_to(self.source, target_is_directory=True)
        for paths in ([ROOT], [self.source, self.source / 'library'], [self.base / 'symlink-root']):
            with self.assertRaisesRegex(ValueError, 'disjoint regular external'):
                self.runtime.create(self.snapshot, paths)
        for option in ('--audio', '--inputs', '--labels', '--timeout', '--count', '--command'):
            result = subprocess.run([sys.executable, str(PATH), 'probe', '--snapshot', str(self.snapshot), option, 'unused'],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unrecognized arguments', result.stderr)
        self.assertFalse(self.snapshot.exists())


if __name__ == '__main__':
    unittest.main()
