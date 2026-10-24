#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""C1 strict runtime/schema and actual artifact-chain controls, generated only."""
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time_v2_metadata as runner
import apta_dense_runner_support as io

spec = importlib.util.spec_from_file_location('registration_contract', Path(__file__).with_name('apta_1_1_coupled_time_metadata_registration.py'))
contract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contract)


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        self.cfg = runner.registered_settings()

    def validate(self, value, kind):
        runner.validate_record(value, self.cfg, kind)

    def test_pinned_public_registration_and_all_nine_new_formats(self):
        self.assertEqual(self.cfg['operational_registration_sha256'], contract.OP_SHA)
        self.assertEqual(self.cfg['metadata_amendment_sha256'], contract.AMEND_SHA)
        for kind in self.cfg['operational']['records']:
            value = contract.fixture(kind)
            if kind == 'completion': value['upstream'] = runner.empty_upstream()
            if kind == 'support': value = contract.proof_fixture()
            if kind == 'evaluation': value = contract.chain_fixture()[2]
            with self.subTest(kind=kind): self.validate(value, kind)

    def test_contract_oracle_and_runtime_reject_wrong_types_and_extra_fields(self):
        for kind in ('context', 'worker', 'completion', 'support', 'evaluation'):
            value = contract.fixture(kind)
            if kind == 'support': value = contract.proof_fixture()
            if kind == 'evaluation': value = contract.chain_fixture()[2]
            for field, bad in (('metadata_amendment_sha256', '0' * 64), ('operational_registration_sha256', '0' * 64), ('extra', 1)):
                changed = deepcopy(value)
                changed[field] = bad
                with self.subTest(kind=kind, field=field), self.assertRaises(ValueError): self.validate(changed, kind)

    def test_support_complements_conservation_types_and_absence(self):
        valid = {'windows': 1, 'reference_complete_windows': 1, 'reference_incomplete_windows': 0}
        self.validate(valid, 'supportWindow')
        for key in valid:
            for bad in (None, True, 0.0, -1, '0'):
                changed = dict(valid, **{key: bad})
                with self.subTest(key=key, bad=bad), self.assertRaises(ValueError): self.validate(changed, 'supportWindow')
            changed = dict(valid)
            del changed[key]
            with self.assertRaises(ValueError): self.validate(changed, 'supportWindow')
        for incomplete in (0, 1):
            changed = dict(valid, reference_complete_windows=incomplete, reference_incomplete_windows=incomplete)
            with self.assertRaises(ValueError): self.validate(changed, 'supportWindow')

    def test_generated_proof_partitions_and_bad_indices_sums(self):
        for extra in (False, True):
            proof = contract.proof_fixture(100, 6, extra)
            self.validate(proof, 'support')
            self.assertEqual(proof['counts']['reference_incomplete_windows'], 200 - int(extra))
        baseline = contract.proof_fixture(2, 6)
        changes = [lambda p: p['sources'][0].update(index=1),
                   lambda p: p['sources'][0]['windows'][0].update(index=1),
                   lambda p: p['sources'][0]['counts'].update(windows=5),
                   lambda p: p['counts'].update(reference_incomplete_windows=0),
                   lambda p: p.update(window_count=11), lambda p: p.update(source_count=1)]
        for change in changes:
            value = deepcopy(baseline)
            change(value)
            with self.assertRaises(ValueError): self.validate(value, 'support')

    def test_no_support_fields_in_numerical_contexts_or_workers(self):
        for kind in ('context', 'worker'):
            for role in ('construct', 'verify'):
                for field in ('reference_support_sha256', 'reference_complete_windows', 'reference_beats'):
                    value = contract.fixture(kind, role)
                    value[field] = '1' * 64
                    with self.assertRaises(ValueError): self.validate(value, kind)

    def test_role_upstream_and_completion_nullability(self):
        for role in ('construct', 'verify', 'evaluate'):
            context = contract.fixture('context', role)
            self.validate(context, 'context')
            changed = deepcopy(context)
            field = runner.UPSTREAM_KEYS[0]
            changed['upstream'][field] = '1' * 64 if role == 'construct' else None
            with self.assertRaises(ValueError): self.validate(changed, 'context')
            completion = contract.fixture('completion', role)
            completion['upstream'] = context['upstream']
            self.validate(completion, 'completion')
            completion['reference_support_sha256'] = None if role == 'evaluate' else '1' * 64
            with self.assertRaises(ValueError): self.validate(completion, 'completion')

    def test_old_v2_ct05_and_mixed_formats_rejected(self):
        for kind in self.cfg['operational']['records']:
            value = contract.fixture(kind)
            for old in ('libapta-coupled-time-phase-context-2', 'libapta-coupled-time-evaluation-1', 'libapta-coupled-time-authorized-attempt-1'):
                value['format'] = old
                with self.assertRaises(ValueError): self.validate(value, kind)

    def test_scientific_rejection_cannot_be_promoted_or_support_disagree(self):
        value = contract.chain_fixture()[2]
        self.validate(value, 'evaluation')
        value['status'] = 'EXPOSED_DIAGNOSTIC_PASS_ONLY'
        with self.assertRaises(ValueError): self.validate(value, 'evaluation')
        value = contract.chain_fixture()[2]
        value['totals']['ols_nominal'].update(reference_complete_windows=0, reference_incomplete_windows=1)
        with self.assertRaises(ValueError): self.validate(value, 'evaluation')

    def test_strict_duplicate_nonfinite_and_exact_paths(self):
        for raw in ('{"x":1,"x":2}', '{"x":NaN}', '{"x":Infinity}', '{"x":1e999}'):
            with self.assertRaises(ValueError): runner.strict_json(raw)
        for path in ('relative', '/tmp/../item', '/tmp/./item', '/tmp/\0item'):
            with self.assertRaises(ValueError): runner.safe_path(path)

    def test_real_settings_reject_old_tokens_and_shape_only_future_tokens(self):
        with self.assertRaisesRegex(ValueError, 'new metadata preflight'): runner.settings()
        with mock.patch.object(Path, 'is_file', return_value=True):
            with self.assertRaisesRegex(ValueError, 'content validation pending'): runner.settings()
        for key in ('preflight_record', 'authorization_record'):
            self.assertFalse((ROOT / 'docs/rust' / self.cfg['operational'][key]).exists())

    def test_import_numerical_entry_has_no_support_or_scoring_dependencies(self):
        script = "import sys;sys.path.insert(0,'tools');import apta_1_1_coupled_time_v2_metadata;assert not set(('apta_reference_support','apta_1_1_coupled_time_evaluation_v2_metadata','apta_1_1_coupled_time','apta_1_1_dense_decoder','mir_eval')) & set(sys.modules)"
        result = subprocess.run([sys.executable, '-c', script], cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_proof_payload_detail_hash_chain_and_corruptions(self):
        with tempfile.TemporaryDirectory(prefix='libapta-c1-chain-') as temp:
            attempt = Path(temp)
            (attempt / 'evaluate').mkdir()
            (attempt / 'candidates').mkdir()
            context, proof, evaluation, completion = contract.chain_fixture()
            cfg = dict(self.cfg, authorization_sha256=context['authorization_sha256'],
                       attempt_path_sha256=context['attempt_path_sha256'], protocol=deepcopy(self.cfg['protocol']))
            cfg['protocol']['input_identity'].update(proof['counts'])
            cfg['protocol']['input_identity']['ct03_label_free_inputs_sha256'] = context['input_manifest_sha256']
            rows = [{'track': context['source_order'][0], 'windows': [{'first_frame': 0, 'end_frame': 1}]}]
            payload = b'generated candidate byte control'
            digest = hashlib.sha256(payload).hexdigest()
            (attempt / 'candidates/0.json').write_bytes(payload)
            context['rows'][0]['candidate_sha256'] = digest
            completion['candidates'][0]['sha256'] = digest
            for value in (proof, evaluation, completion): value['context_sha256'] = runner.record_digest(context)
            completion['upstream'] = context['upstream']
            proof_path = attempt / 'evaluate.reference-support.json'
            io.write_new(proof_path, proof)
            for value in (evaluation, completion): value['reference_support_sha256'] = io.sha(proof_path)
            detail = {'track': rows[0]['track'], 'scores': {name: [deepcopy(evaluation['totals'][name])] for name in runner.DIAGNOSTIC_GROUPS}}
            detail['scores']['windows'] = [{'nominal': deepcopy(evaluation['totals']['candidate_nominal']),
                                            'actual': deepcopy(evaluation['totals']['candidate_actual'])}]
            detail_path = attempt / 'evaluate/0.json'
            io.write_new(detail_path, detail)
            evaluation['details'] = [{'track': rows[0]['track'], 'sha256': io.sha(detail_path)}]
            evaluation['bindings'] = {str(proof_path): io.sha(proof_path), str(detail_path): io.sha(detail_path)}
            evaluation_path = attempt / 'evaluate.support.completion.json'
            io.write_new(evaluation_path, evaluation)
            completion['evaluation_report_sha256'] = io.sha(evaluation_path)
            with mock.patch.object(runner, 'upstream', return_value=context['upstream']):
                runner.evaluation_artifacts(attempt, cfg, completion, context, rows, {})
                for field in ('reference_support_sha256', 'evaluation_report_sha256'):
                    changed = dict(completion, **{field: '0' * 64})
                    with self.assertRaises(ValueError): runner.evaluation_artifacts(attempt, cfg, changed, context, rows, {})
                changed_rows = deepcopy(rows)
                changed_rows[0]['windows'].append({'first_frame': 1, 'end_frame': 2})
                with self.assertRaises(ValueError): runner.evaluation_artifacts(attempt, cfg, completion, context, changed_rows, {})
                # Valid JSON and refreshed digest still cannot hide a detail/proof mismatch.
                detail['scores']['ols_actual'][0].update(reference_complete_windows=0, reference_incomplete_windows=1)
                detail_path.write_text(json.dumps(detail))
                evaluation['details'][0]['sha256'] = io.sha(detail_path)
                evaluation['bindings'][str(detail_path)] = io.sha(detail_path)
                evaluation_path.write_text(json.dumps(evaluation))
                completion['evaluation_report_sha256'] = io.sha(evaluation_path)
                with self.assertRaises(ValueError): runner.evaluation_artifacts(attempt, cfg, completion, context, rows, {})


if __name__ == '__main__':
    unittest.main()
