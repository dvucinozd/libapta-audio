#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""MC-02: inherit frozen process checks, add fixed adapter/binding cases."""
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests/unit')]
import apta_1_1_meter_context_runner as adapter
import apta_1_1_context_matched_runner as original_engine

# Import the test module under a distinct name: tools/ and tests/ share a basename.
import importlib.util
spec = importlib.util.spec_from_file_location('_apta_mc02_public_tests',
                                             ROOT / 'tests/unit/apta_1_1_context_matched_runner.py')
public_tests = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = public_tests
spec.loader.exec_module(public_tests)
public_tests.runner = adapter.engine
runner = adapter.engine


class MeterContextRunnerTests(public_tests.RunnerTests):
    @classmethod
    def setUpClass(cls):
        os.environ['APTA_MATCHED_P5_RUNTIME'] = os.environ['APTA_MC02_RUNTIME']
        os.environ['APTA_MATCHED_P5_OUTPUT'] = os.environ['APTA_MC02_OUTPUT']
        super().setUpClass()

    def test_15_actual_mc01_projection_records_and_identity(self):
        attempt = self.base / 'test_01_complete_public_100_source_600_window_pipeline/full-attempt'
        manifest = runner.read(runner.leaf(attempt / 'manifest.json'))
        identity = runner.read(runner.leaf(attempt / 'meter-context.identity.json'))
        self.assertEqual(identity, adapter.identity(attempt, manifest, self.runtime))
        completion = runner.completion(attempt, 'project', manifest)
        context_owned, preserved, count = 0, 0, 0
        for entry in completion['records']:
            projections = runner.read(entry)
            for arm in projections.values():
                for projection in arm:
                    self.assertEqual(projection['format'], 'libapta-meter-context-projection-1')
                    self.assertEqual(projection['registration_sha256'], adapter.PROJECTION_PROTOCOL)
                    context_owned += int(projection['meter_origin'] == 'CONTEXT')
                    preserved += int(projection['meter_origin'] == 'P1')
                    count += 1
        self.assertEqual((count, context_owned, preserved), (1200, 600, 600))
        descriptor = runner.read(self.runtime)
        for role in ('project', 'verify'):
            files = descriptor['profiles'][role]
            self.assertIn(str(ROOT / 'tools/apta_1_1_meter_context_runner.py'), files)
        verify = descriptor['profiles']['verify']
        self.assertIn(str(ROOT / 'tests/unit/apta_meter_context_oracle.py'), verify)
        self.assertIn(str(ROOT / 'tests/unit/apta_context_projection_oracle.py'), verify)
        self.assertNotIn(str(ROOT / 'tools/apta_meter_context_projection.py'), verify)
        self.assertNotIn(str(ROOT / 'tools/apta_context_projection.py'), verify)

    def test_16_old_runtime_authorization_and_engine_unchanged(self):
        old = runner.leaf(Path(os.environ['APTA_MC02_OLD_RUNTIME']).resolve())
        original_engine.check_runtime(old)
        self.assertEqual(len(original_engine.components()), 73)
        with self.assertRaises(ValueError):
            runner.check_runtime(old)
        with self.assertRaises(ValueError):
            original_engine.check_runtime(self.runtime)
        with self.assertRaisesRegex(ValueError, 'separate meter-context'):
            runner.authorize('C3_FIXED_GENERATED', '0' * 64, self.runtime, self.work / 'old-token')
        self.assertFalse((ROOT / 'docs/rust/METER-CONTEXT-GENERATED-SCOPE.json').exists())

    def test_17_corrupt_new_identity_closes_attempt(self):
        attempt = self.prepare()
        path = attempt / 'meter-context.identity.json'
        value = runner.read(runner.leaf(path))
        value['operational_protocol_sha256'] = '0' * 64
        path.write_text(json.dumps(value))
        with self.assertRaises(ValueError):
            self.project(attempt)
        self.assertTrue((attempt / 'INVALID.json').exists())
        self.assertFalse((attempt / 'project.complete.json').exists())
        with self.assertRaises(ValueError):
            self.project(attempt)

    def test_18_rehashed_job_scope_divergence_rejected_before_labels(self):
        attempt = self.prepare()
        manifest_path = attempt / 'manifest.json'
        manifest = runner.read(runner.leaf(manifest_path))
        job_path = Path(manifest['jobs'][0]['path'])
        job = runner.read(manifest['jobs'][0])
        job['coupled']['beat_frames'][0] += 1
        job_path.write_text(json.dumps(job))
        manifest['jobs'][0] = runner.leaf(job_path)
        manifest_path.write_text(json.dumps(manifest))
        (attempt / 'meter-context.identity.json').write_text(json.dumps(adapter.identity(attempt, manifest, self.runtime)))
        self.project(attempt)
        self.verify(attempt)
        label = Path(runner.read(self.scope)['labels']['path'])
        backup = label.with_suffix('.saved')
        label.rename(backup)
        try:
            with self.assertRaises(ValueError):
                runner.dispatch('evaluate', attempt, self.runtime, self.scope)
        finally:
            backup.rename(label)
        self.assertIn('staged events/geometry differ from scoped source',
                      (attempt / 'evaluate.supervised.stderr').read_text())
        self.assertFalse((attempt / 'evaluate.complete.json').exists())
        self.assertFalse((attempt / 'matched-report.json').exists())


if __name__ == '__main__':
    program = unittest.main(exit=False, verbosity=2)
    if os.environ.get('APTA_MC02_RESULT'):
        runner.write(Path(os.environ['APTA_MC02_RESULT']).resolve(),
                     {'tests': program.result.testsRun, 'failures': len(program.result.failures),
                      'errors': len(program.result.errors), 'skips': len(program.result.skipped)})
    sys.exit(not program.result.wasSuccessful())
