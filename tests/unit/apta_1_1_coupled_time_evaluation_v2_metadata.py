#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""C1 generated unit parity and evaluation-boundary controls, no corpus run."""
from copy import deepcopy
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time_evaluation_v2_metadata as evaluator
import apta_1_1_coupled_time_v2_metadata as runner
from apta_reference_support import reference_support


def module(name):
    spec = importlib.util.spec_from_file_location('fixture_' + name, Path(__file__).with_name(name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def strip_added(scored):
    value = deepcopy(scored)
    for name in runner.DIAGNOSTIC_GROUPS:
        for score in value[name]:
            del score['reference_incomplete_windows']
    # Canonical score_source shares objects with these aliases; JSON also tests
    # the independent deserialized shape where they are distinct dictionaries.
    for window in value['windows']:
        for name in ('nominal', 'actual'):
            window[name].pop('reference_incomplete_windows', None)
    return value


def bare_scores():
    out = {name: [{'windows': 1, 'reference_complete_windows': 1, 'old_field': {'keep': True}}]
           for name in runner.DIAGNOSTIC_GROUPS}
    out['windows'] = [{'nominal': deepcopy(out['candidate_nominal'][0]),
                       'actual': deepcopy(out['candidate_actual'][0]), 'projected': {'unchanged': True}}]
    out['unrelated'] = {'reference_incomplete_windows': 'must survive exact parity'}
    return out


class EvaluationTests(unittest.TestCase):
    def test_six_exact_additions_and_no_mutation(self):
        old = bare_scores()
        before = deepcopy(old)
        new = evaluator.add_support(old, reference_support([0, 99], [{'first_frame': 0, 'end_frame': 100}]))
        self.assertEqual(old, before)
        self.assertEqual(strip_added(new), old)
        self.assertEqual(new['unrelated'], old['unrelated'])
        self.assertIsNot(new, old)

    def test_legacy_context_disagreement_and_missing_boolean_fields_rejected(self):
        counts = [{'windows': 1, 'reference_complete_windows': 1, 'reference_incomplete_windows': 0}]
        for field, value in (('windows', True), ('windows', 2), ('reference_complete_windows', True),
                             ('reference_complete_windows', 0), ('reference_incomplete_windows', 0)):
            old = bare_scores()
            old['ols_actual'][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                evaluator.add_support(old, counts)
        for field in ('windows', 'reference_complete_windows'):
            old = bare_scores()
            del old['windows'][0]['actual'][field]
            with self.assertRaises(ValueError):
                evaluator.add_support(old, counts)

    def test_nonconserving_support_and_all_window_mismatches_rejected(self):
        for counts in ([], [{'windows': 1, 'reference_complete_windows': 1}],
                       [{'windows': 1, 'reference_complete_windows': 1, 'reference_incomplete_windows': 1}],
                       [{'windows': 1, 'reference_complete_windows': True, 'reference_incomplete_windows': 0}],
                       [{'windows': 2, 'reference_complete_windows': 1, 'reference_incomplete_windows': 1}]):
            with self.subTest(counts=counts), self.assertRaises(ValueError):
                evaluator.add_support(bare_scores(), counts)
        old = bare_scores()
        old['ols_nominal'] = []
        with self.assertRaises(ValueError):
            evaluator.add_support(old, [{'windows': 1, 'reference_complete_windows': 1, 'reference_incomplete_windows': 0}])

    def test_actual_canonical_scorer_old_fields_and_gate_parity(self):
        # Only public generated fixture geometry. No saved R4 or real inputs.
        import apta_1_1_dense_decoder as scorer
        import apta_coupled_time as numerical
        with tempfile.TemporaryDirectory(prefix='libapta-c1-unit-') as temp:
            fixture = module('apta_1_1_coupled_time_runner').RunnerTests()
            fixture.root = Path(temp)
            with mock.patch.dict(os.environ, {'APTA_CT04_CANONICAL_EVENTS': '1'}):
                fixture.fixture()
            inputs = json.loads(fixture.inputs.read_text())
            inventory = json.loads(fixture.inventory.read_text())
            truths = json.loads(fixture.labels.read_text())['tracks']
            from collections import Counter
            groups = ('old_coarse', 'old_ols', 'candidate_projected', *runner.DIAGNOSTIC_GROUPS)
            totals = [{name: Counter() for name in groups} for _ in range(2)]
            pairs = [{name: Counter() for name in ('ols', 'coarse', 'actual')} for _ in range(2)]
            means = None
            for row, full, truth in zip(inputs['sources'], inventory['sources'], truths):
                candidate = numerical.construct(*(row[key] for key in runner.PLAIN))
                events = json.loads(Path(row['events_path']).read_text())['events']
                old = scorer.score_source(candidate, events, row['windows'], truth,
                                          json.loads(Path(full['baseline_score_path']).read_text()),
                                          json.loads(Path(full['coarse_path']).read_text()))
                before = deepcopy(old)
                new = evaluator.add_support(old, reference_support(truth['beat_frames'], row['windows']))
                self.assertEqual(old, before)
                self.assertEqual(strip_added(new), old)
                self.assertEqual(strip_added(json.loads(json.dumps(new))), json.loads(json.dumps(old)))
                for index, scored in enumerate((old, new)):
                    for name in groups:
                        for score in scored[name]: totals[index][name].update(score)
                    for name in pairs[index]: pairs[index][name].update(scored['paired'][name])
                means = (old['old_event_metrics'], old['candidate_event_metrics'])
            self.assertEqual(scorer.gates(fixture.cfg['protocol'], totals[0], pairs[0], *means),
                             scorer.gates(fixture.cfg['protocol'], totals[1], pairs[1], *means))

    def test_direct_evaluation_rejects_before_inventory_or_support_import(self):
        with tempfile.TemporaryDirectory(prefix='libapta-c1-closed-') as temp:
            attempt = Path(temp) / 'attempt'
            cfg = runner.registered_settings()
            cfg['attempt_path_sha256'] = __import__('hashlib').sha256(str(attempt).encode()).hexdigest()
            with mock.patch.object(evaluator, '_read', side_effect=AssertionError('inventory read before verification')):
                with self.assertRaises((ValueError, OSError)):
                    evaluator.evaluate(attempt, Path(temp) / 'unavailable-inventory', cfg, {}, [], '1' * 40, {})

    def test_versioned_orchestration_with_canonical_generated_scores(self):
        """Unit-level evaluation plumbing; full supervised process gates are C2."""
        import apta_coupled_time as numerical
        import apta_dense_runner_support as io
        with tempfile.TemporaryDirectory(prefix='libapta-c1-evaluation-') as temp:
            fixture = module('apta_1_1_coupled_time_runner').RunnerTests()
            fixture.root = Path(temp)
            with mock.patch.dict(os.environ, {'APTA_CT04_CANONICAL_EVENTS': '1'}): fixture.fixture()
            attempt = fixture.attempt
            (attempt / 'candidates').mkdir(parents=True)
            rows = json.loads(fixture.inputs.read_text())['sources']
            cfg = dict(runner.registered_settings(), protocol=fixture.cfg['protocol'], source_root=str(fixture.source))
            # These small generated fixtures deliberately bracket every window.
            truth = json.loads(fixture.labels.read_text())['tracks']
            counts = [c for row, labels in zip(rows, truth) for c in reference_support(labels['beat_frames'], row['windows'])]
            cfg['protocol']['input_identity'].update({key: sum(c[key] for c in counts) for key in runner.SUPPORT_KEYS})
            complete = {'candidates': []}
            for index, row in enumerate(rows):
                candidate = numerical.construct(*(row[key] for key in runner.PLAIN))
                path = attempt / 'candidates' / (str(index) + '.json')
                path.write_text(json.dumps(candidate))
                complete['candidates'].append({'sha256': io.sha(path)})
            for role in ('construct', 'verify'): (attempt / (role + '.completion.json')).write_text('generated boundary stand-in')
            context = {'input_manifest_sha256': io.sha(fixture.inputs), 'window_count': len(counts),
                       'upstream': {'construction_completion_sha256': io.sha(attempt / 'construct.completion.json'),
                                    'verification_completion_sha256': io.sha(attempt / 'verify.completion.json')}}
            (attempt / 'evaluate.context.json').write_text(json.dumps(context))
            revision = io.git(fixture.source, 'rev-parse', 'HEAD')
            with mock.patch.object(evaluator, 'evaluation_boundary', return_value=(context, rows)):
                result = evaluator.evaluate(attempt, fixture.inventory, cfg, complete, rows, revision, {})
            runner.validate_record(result, cfg, 'evaluation')
            proof = json.loads((attempt / 'evaluate.reference-support.json').read_text())
            self.assertEqual(result['reference_support_sha256'], io.sha(attempt / 'evaluate.reference-support.json'))
            self.assertEqual(proof['counts'], {key: sum(c[key] for c in counts) for key in runner.SUPPORT_KEYS})
            for group in runner.DIAGNOSTIC_GROUPS:
                self.assertEqual({key: result['totals'][group][key] for key in runner.SUPPORT_KEYS}, proof['counts'])
            self.assertEqual(result['status'], 'REJECTED')

    def test_prior_receipt_check_does_not_require_future_context_bindings(self):
        """An evaluation-only binding must never be required in a prior receipt."""
        import apta_dense_runner_support as io
        with tempfile.TemporaryDirectory(prefix='libapta-c1-boundary-order-') as temp:
            attempt = Path(temp)
            context_path = attempt / 'evaluate.context.json'
            context_path.write_text('generated context')
            rows = [{'track': 'generated'}]
            frozen = {'candidates': [{'sha256': '1' * 64}]}
            original = {'input_manifest_path': '/generated/input', 'rows': [{'job_sha256': '2' * 64}]}
            context = {'input_manifest_path': '/generated/input',
                       'rows': [{'candidate_sha256': '1' * 64, 'job_sha256': '2' * 64}]}
            cfg = {'bindings': {'/generated/code': '3' * 64}, 'source_root': '/generated/source'}
            bindings = {str(context_path): io.sha(context_path), '/generated/later-verification-envelope': '4' * 64}
            roles = []

            def prior_receipt(_attempt, role, _cfg, _revision, _rows, observed):
                self.assertNotIn(str(context_path), observed)
                self.assertNotIn('/generated/later-verification-envelope', observed)
                roles.append(role)
                return frozen

            with mock.patch.object(runner, 'destination', return_value=attempt), \
                    mock.patch.object(io, 'source_snapshot', return_value='5' * 40), \
                    mock.patch.object(runner, 'header', return_value=(frozen, original)), \
                    mock.patch.object(runner, 'load_input_manifest', return_value=rows), \
                    mock.patch.object(runner, 'audit_cohort'), \
                    mock.patch.object(runner, 'full_receipt', side_effect=prior_receipt), \
                    mock.patch.object(runner, 'read_bound', return_value=context), \
                    mock.patch.object(runner, 'validate_record'), \
                    mock.patch.object(runner, 'check_identity'), \
                    mock.patch.object(runner, 'context_semantics'), \
                    mock.patch.object(io, 'recheck'), mock.patch.object(io, 'check_source'):
                self.assertEqual(evaluator.evaluation_boundary(attempt, cfg, frozen, rows, '5' * 40, bindings), (context, rows))
            self.assertEqual(roles, ['construct', 'verify'])


if __name__ == '__main__':
    unittest.main()
