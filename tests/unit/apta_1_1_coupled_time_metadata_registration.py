#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""C0 generated JSON/contract checks, not support/evaluator implementation."""
import copy
import hashlib
import json
import math
from pathlib import Path
import unittest
import sys

import jsonschema

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / 'docs/rust'
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time_v2 as old_records

SHA = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
LOAD = lambda path: old_records.strict_json(Path(path).read_bytes())
OP_PATH = DOCS / 'COUPLED-TIME-RUNNER-V2-METADATA-PROTOCOL.json'
AMEND_PATH = DOCS / 'COUPLED-TIME-SUPPORT-METADATA-AMENDMENT.json'
OP, AMEND = LOAD(OP_PATH), LOAD(AMEND_PATH)
SCHEMA = LOAD(DOCS / OP['operational_schema'])
SUPPORT = LOAD(DOCS / OP['support_schema'])
DEFS = SCHEMA['$defs']
OP_SHA, AMEND_SHA = SHA(OP_PATH), SHA(AMEND_PATH)


def digest(value):
    return hashlib.sha256((json.dumps(value, sort_keys=True, indent=2) + '\n').encode()).hexdigest()


def exact_types(value, spec):
    if '$ref' in spec:
        return exact_types(value, DEFS[spec['$ref'].split('/')[-1]])
    if 'anyOf' in spec:
        if value is None:
            return
        return exact_types(value, spec['anyOf'][0])
    kind = spec.get('type')
    types = {'object':dict,'array':list,'integer':int,'boolean':bool,'string':str,'null':type(None)}
    if kind in types and type(value) is not types[kind]:
        raise ValueError('wrong exact JSON type')
    if kind == 'number' and (type(value) not in (int,float) or not math.isfinite(value)):
        raise ValueError('nonfinite or nonnumeric value')
    if kind == 'object':
        for name, child in value.items():
            child_spec = spec.get('properties',{}).get(name,spec.get('additionalProperties',True))
            if isinstance(child_spec,dict): exact_types(child,child_spec)
    if kind == 'array':
        for child in value: exact_types(child,spec['items'])


def partition(value):
    if value['reference_complete_windows'] + value['reference_incomplete_windows'] != value['windows']:
        raise ValueError('support partition does not conserve windows')


def validate(value, kind):
    """Independent test contract oracle; C1 must enforce these rules at runtime."""
    exact_types(value, DEFS[kind])
    jsonschema.Draft202012Validator({'$defs':DEFS,'$ref':'#/$defs/'+kind}).validate(value)
    if kind in OP['records']:
        if (value.get('operational_registration_sha256') != OP_SHA
                or value['metadata_amendment_sha256'] != AMEND_SHA):
            raise ValueError('record authority differs')
    if kind in ('supportWindow','supportTotals'): partition(value)
    if kind == 'supportInput':
        if any(a >= b for a,b in zip(value['reference_beats'],value['reference_beats'][1:])):
            raise ValueError('reference beats not strictly ordered')
        last_end = None
        for row in value['windows']:
            if row['first_frame'] >= row['end_frame'] or last_end is not None and row['first_frame'] < last_end:
                raise ValueError('invalid window order or extent')
            last_end = row['end_frame']
    if kind == 'context':
        if (len(value['source_order']) != value['source_count'] or len(value['rows']) != value['source_count']
                or len(set(value['source_order'])) != value['source_count']):
            raise ValueError('context source count/order differs')
        for index,row in enumerate(value['rows']):
            if row['index'] != index or row['source_id'] != value['source_order'][index]:
                raise ValueError('context row identity differs')
            if (row['candidate_sha256'] is None) != (value['role'] == 'construct'):
                raise ValueError('role candidate identity differs')
        expected = [False]*4 if value['role']=='construct' else [True,True,False,False] if value['role']=='verify' else [True]*4
        # Use the registered upstream field order, independent of JSON key order.
        fields = ('construction_completion_sha256','construction_supervision_sha256',
                  'verification_completion_sha256','verification_supervision_sha256')
        if [value['upstream'][key] is not None for key in fields] != expected:
            raise ValueError('role upstream identities differ')
    if kind == 'completion':
        evaluate = value['role']=='evaluate'
        if (value['reference_support_sha256'] is not None) != evaluate or (value['evaluation_report_sha256'] is not None) != evaluate:
            raise ValueError('role proof/payload presence differs')
        if value['reference_labels_loaded'] != evaluate:
            raise ValueError('reference loaded before full evaluation')
    if kind == 'support':
        if len(value['sources']) != value['source_count']:
            raise ValueError('proof source count differs')
        totals = dict.fromkeys(('windows','reference_complete_windows','reference_incomplete_windows'),0)
        for index,source in enumerate(value['sources']):
            if source['index'] != index or source['counts']['windows'] != len(source['windows']):
                raise ValueError('proof source index/window count differs')
            subtotal = dict.fromkeys(totals,0)
            for window_index,window in enumerate(source['windows']):
                if window['index'] != window_index:
                    raise ValueError('proof window index differs')
                partition(window['counts'])
                for key in subtotal: subtotal[key] += window['counts'][key]
            if source['counts'] != subtotal: raise ValueError('proof source sums differ')
            for key in totals: totals[key] += subtotal[key]
        partition(value['counts'])
        if value['counts'] != totals or value['window_count'] != totals['windows']:
            raise ValueError('proof cohort sums differ')
    if kind == 'evaluation':
        for name in AMEND['diagnostic_groups']: partition(value['totals'][name])
        if value['status'] != ('EXPOSED_DIAGNOSTIC_PASS_ONLY' if all(value['gates'].values()) else 'REJECTED'):
            raise ValueError('scientific status differs from unchanged gates')
        if sum(row['sources'] for row in value['by_genre'].values()) != value['sources']:
            raise ValueError('family source counts differ')
        for row in value['by_genre'].values():
            if any(number > row['sources'] for key in ('legacy','candidate') for number in row[key].values()):
                raise ValueError('family macro sum outside source support')
    if kind == 'preflight':
        if value['generated_scientific_verdict'] != 'REJECTED':
            raise ValueError('counter-only fixture verdict changed')


def example(spec):
    if '$ref' in spec: return example(DEFS[spec['$ref'].split('/')[-1]])
    if 'const' in spec: return copy.deepcopy(spec['const'])
    if 'anyOf' in spec: return example(spec['anyOf'][0])
    if 'enum' in spec: return spec['enum'][0]
    kind = spec.get('type')
    if kind=='object':
        properties=spec.get('properties',{})
        result={name:example(properties.get(name,spec.get('additionalProperties'))) for name in spec.get('required',())}
        if not result and spec.get('minProperties',0):
            result['/fixture/path' if 'propertyNames' in spec else 'fixture']=example(spec['additionalProperties'])
        return result
    if kind=='array': return [example(spec['items']) for _ in range(spec.get('minItems',0))]
    if kind in ('integer','number'): return spec.get('minimum',0)
    if kind=='boolean': return False
    if kind=='null': return None
    if kind=='string':
        pattern=spec.get('pattern','')
        if '{64}' in pattern: return '1'*64
        if '{40}' in pattern: return '1'*40
        if pattern.startswith('^/'): return '/fixture/path'
        return 'fixture'
    raise ValueError('unsupported generated fixture schema')


def fixture(kind, role=None):
    value=example(DEFS[kind])
    if 'operational_registration_sha256' in value: value['operational_registration_sha256']=OP_SHA
    if role is not None: value['role']=role
    if kind=='context':
        fields=('construction_completion_sha256','construction_supervision_sha256','verification_completion_sha256','verification_supervision_sha256')
        for index,key in enumerate(fields): value['upstream'][key]=None if value['role']=='construct' or value['role']=='verify' and index>=2 else '2'*64
        for row in value['rows']: row['candidate_sha256']=None if value['role']=='construct' else '3'*64
    if kind=='completion':
        evaluate=value['role']=='evaluate'
        value['reference_labels_loaded']=evaluate
        value['reference_support_sha256']='4'*64 if evaluate else None
        value['evaluation_report_sha256']='5'*64 if evaluate else None
    return value


def proof_fixture(sources=1, windows=1, extra_complete=False):
    value=fixture('support')
    value['sources']=[]
    totals={'windows':0,'reference_complete_windows':0,'reference_incomplete_windows':0}
    for index in range(sources):
        rows=[]
        for number in range(windows):
            complete = True if windows==1 else 0<number<windows-1 or extra_complete and index==0 and number==windows-1
            counts={'windows':1,'reference_complete_windows':int(complete),'reference_incomplete_windows':1-int(complete)}
            rows.append({'index':number,'counts':counts})
        subtotal={key:sum(row['counts'][key] for row in rows) for key in totals}
        value['sources'].append({'index':index,'counts':subtotal,'windows':rows})
        for key in totals: totals[key]+=subtotal[key]
    value.update(source_count=sources,window_count=totals['windows'],counts=totals)
    return value


def chain_fixture():
    context=fixture('context','evaluate')
    proof=proof_fixture()
    proof.update(context_sha256=digest(context),input_manifest_sha256=context['input_manifest_sha256'],
                 candidate_freeze_sha256=context['upstream']['construction_completion_sha256'],
                 verification_sha256=context['upstream']['verification_completion_sha256'])
    evaluation=fixture('evaluation')
    evaluation.update(context_sha256=digest(context),reference_support_sha256=digest(proof),
                      candidate_freeze_sha256=proof['candidate_freeze_sha256'],verification_sha256=proof['verification_sha256'])
    evaluation['gates']=copy.deepcopy(LOAD(DOCS/'COUPLED-TIME-RUNNER-V2-R4-FROZEN-20261024.json')['scientific_gates'])
    evaluation['status']='REJECTED'
    for name,counters in evaluation['totals'].items():
        counters['windows']=1
        if name in AMEND['diagnostic_groups']: counters.update(proof['counts'])
    completion=fixture('completion','evaluate')
    completion.update(context_sha256=digest(context),reference_support_sha256=digest(proof),evaluation_report_sha256=digest(evaluation))
    return context,proof,evaluation,completion


def linked(context,proof,evaluation,completion):
    for kind,value in zip(('context','support','evaluation','completion'),(context,proof,evaluation,completion)): validate(value,kind)
    for value in (proof,evaluation,completion):
        if value['source_revision']!=context['source_revision'] or value['context_sha256']!=digest(context):
            raise ValueError('source/context linkage differs')
    if proof['source_count']!=context['source_count'] or proof['window_count']!=context['window_count'] or evaluation['sources']!=context['source_count']:
        raise ValueError('source/window linkage differs')
    if proof['input_manifest_sha256']!=context['input_manifest_sha256']:
        raise ValueError('input linkage differs')
    for key,upstream in (('candidate_freeze_sha256','construction_completion_sha256'),('verification_sha256','verification_completion_sha256')):
        if proof[key]!=context['upstream'][upstream] or evaluation[key]!=proof[key]:
            raise ValueError('upstream freeze/verify linkage differs')
    if evaluation['reference_support_sha256']!=digest(proof) or completion['reference_support_sha256']!=digest(proof) or completion['evaluation_report_sha256']!=digest(evaluation):
        raise ValueError('support/payload hash chain differs')
    for name,counts in evaluation['totals'].items():
        if counts['windows']!=proof['window_count']: raise ValueError('legacy window denominator differs')
        if name in AMEND['diagnostic_groups'] and any(counts[key]!=proof['counts'][key] for key in proof['counts']):
            raise ValueError('diagnostic/proof support differs')


def authorization_references(authorization, preflight):
    # Reference consistency only; actual future real readiness additionally needs
    # content/status validation of the three separately registered scope reports.
    validate(authorization,'authorization'); validate(preflight,'preflight')
    if authorization['preflight_sha256']!=digest(preflight) or authorization['preflight_source_revision']!=preflight['source_revision']:
        raise ValueError('authorization references a different metadata preflight')


class RegistrationTests(unittest.TestCase):
    def test_schema_and_parent_bindings(self):
        for value in (SCHEMA,SUPPORT): jsonschema.Draft202012Validator.check_schema(value)
        for registration in (OP,AMEND):
            for name,digest_value in registration['binding'].items(): self.assertEqual(SHA(DOCS/name),digest_value,name)
        self.assertEqual(OP['metadata_amendment_sha256'],AMEND_SHA)
        self.assertFalse(OP['execution_enabled']); self.assertFalse(AMEND['execution_enabled'])
        self.assertEqual(OP['stage']['next'],'C1')

    def test_frozen_science_and_r4_components_preserved(self):
        old=LOAD(DOCS/'COUPLED-TIME-RUNNER-V2-PROTOCOL.json')
        for name,digest_value in OP['preserved_r4_component_sha256'].items(): self.assertEqual(SHA(ROOT/name),digest_value,name)
        self.assertEqual(len(OP['immutable_component_sha256']),13)
        for key in ('immutable_component_sha256','baseline','baseline_event_means','development_gates','limits','cohort','full_preflight'):
            self.assertEqual(OP[key],old[key],key)
        self.assertEqual(set(OP['records'])-set(old['records']),{'evaluation','support'})

    def test_new_record_shapes_and_roles(self):
        for kind in ('worker','supervision','invalid','preflight','authorization'):
            validate(fixture(kind),kind)
        for role in ('construct','verify','evaluate'):
            validate(fixture('context',role),'context'); validate(fixture('completion',role),'completion')
        for kind,value in zip(('context','support','evaluation','completion'),chain_fixture()): validate(value,kind)

    def test_complementary_window_and_aggregate_examples(self):
        for complete in (0,1): validate({'windows':1,'reference_complete_windows':complete,'reference_incomplete_windows':1-complete},'supportWindow')
        for extra,expected in ((False,(400,200)),(True,(401,199))):
            value=proof_fixture(100,6,extra); validate(value,'support')
            self.assertEqual((value['counts']['reference_complete_windows'],value['counts']['reference_incomplete_windows']),expected)

    def test_missing_boolean_float_negative_and_nonconserving_counts(self):
        base={'windows':1,'reference_complete_windows':1,'reference_incomplete_windows':0}
        for name in base:
            for invalid in (None,True,False,1.0,-1,2,'1'):
                value=copy.deepcopy(base); value[name]=invalid
                with self.assertRaises((ValueError,jsonschema.ValidationError),msg=str((name,invalid))): validate(value,'supportWindow')
            value=copy.deepcopy(base); del value[name]
            with self.assertRaises(jsonschema.ValidationError): validate(value,'supportWindow')
        for complete,incomplete in ((0,0),(1,1)):
            with self.assertRaises((ValueError,jsonschema.ValidationError)): validate({'windows':1,'reference_complete_windows':complete,'reference_incomplete_windows':incomplete},'supportWindow')
        with self.assertRaises(ValueError): validate({'windows':600,'reference_complete_windows':400,'reference_incomplete_windows':0},'supportTotals')

    def test_source_window_index_and_sum_corruption(self):
        mutations=(lambda p:p['sources'].append(copy.deepcopy(p['sources'][0])),
                   lambda p:p['sources'][0].update(index=1),lambda p:p['sources'][0]['windows'][0].update(index=1),
                   lambda p:p['sources'][0]['counts'].update(reference_complete_windows=0),
                   lambda p:p['counts'].update(windows=2),lambda p:p.update(window_count=2))
        for mutation in mutations:
            value=proof_fixture();mutation(value)
            with self.assertRaises((ValueError,jsonschema.ValidationError)): validate(value,'support')

    def test_input_shape_order_and_unavailable_reference(self):
        valid={'reference_beats':[0,100,200],'windows':[{'first_frame':0,'end_frame':1},{'first_frame':100,'end_frame':201}]}
        validate(valid,'supportInput')
        variants=[]
        for beats in (None,[],[True],[0.0],[1,1],[2,1],[-1,2]):
            value=copy.deepcopy(valid);value['reference_beats']=beats;variants.append(value)
        for first,end in ((1,1),(2,1)):
            value=copy.deepcopy(valid);value['windows'][0].update(first_frame=first,end_frame=end);variants.append(value)
        value=copy.deepcopy(valid);value['windows'][1]['first_frame']=0;variants.append(value)
        for value in variants:
            with self.assertRaises((ValueError,jsonschema.ValidationError)): validate(value,'supportInput')

    def test_old_v2_and_ct05_formats_rejected(self):
        old=LOAD(DOCS/'COUPLED-TIME-RUNNER-V2-PROTOCOL.json')
        for kind in old['records']:
            value=fixture(kind);value['format']=old['records'][kind]
            with self.assertRaises(jsonschema.ValidationError): validate(value,kind)
        value=fixture('authorization');value['format']='libapta-coupled-time-authorized-attempt-1'
        with self.assertRaises(jsonschema.ValidationError): validate(value,'authorization')

    def test_wrong_authority_and_unexpected_fields_rejected(self):
        for kind in OP['records']:
            value=proof_fixture() if kind=='support' else chain_fixture()[2] if kind=='evaluation' else fixture(kind)
            for name in ('metadata_amendment_sha256','operational_registration_sha256'):
                wrong=copy.deepcopy(value);wrong[name]='f'*64
                with self.assertRaises((ValueError,jsonschema.ValidationError)): validate(wrong,kind)
            wrong=copy.deepcopy(value);wrong['unregistered_field']=0
            with self.assertRaises(jsonschema.ValidationError): validate(wrong,kind)

    def test_role_isolation_and_no_support_in_worker_or_context(self):
        for kind in ('worker','context'):
            value=fixture(kind);value['reference_support_sha256']='1'*64
            with self.assertRaises(jsonschema.ValidationError): validate(value,kind)
        for role in ('construct','verify'):
            value=fixture('completion',role);value['reference_support_sha256']='1'*64
            with self.assertRaises(ValueError): validate(value,'completion')
        value=fixture('completion','evaluate');value['reference_support_sha256']=None
        with self.assertRaises(ValueError): validate(value,'completion')
        for role in ('construct','verify','evaluate'):
            value=fixture('context',role);value['upstream']['verification_supervision_sha256']=None if role=='evaluate' else '1'*64
            with self.assertRaises(ValueError): validate(value,'context')

    def test_actual_generated_hash_chain_and_mixed_artifact_rejection(self):
        linked(*chain_fixture())
        edits=((1,'context_sha256'),(1,'input_manifest_sha256'),(1,'candidate_freeze_sha256'),
               (1,'verification_sha256'),(2,'reference_support_sha256'),(3,'reference_support_sha256'),
               (3,'evaluation_report_sha256'),(2,'source_revision'))
        for index,name in edits:
            chain=chain_fixture();chain[index][name]='f'*(40 if name=='source_revision' else 64)
            with self.assertRaises(ValueError): linked(*chain)
        chain=chain_fixture();chain[2]['totals']['candidate_actual'].update(reference_complete_windows=0,reference_incomplete_windows=1)
        chain[3]['evaluation_report_sha256']=digest(chain[2])
        with self.assertRaises(ValueError): linked(*chain)

    def test_missing_proof_and_scientific_verdict_promotion_rejected(self):
        value=chain_fixture()[2];del value['reference_support_sha256']
        with self.assertRaises(jsonschema.ValidationError): validate(value,'evaluation')
        value=chain_fixture()[2];value['status']='EXPOSED_DIAGNOSTIC_PASS_ONLY'
        with self.assertRaises(ValueError): validate(value,'evaluation')
        value=fixture('preflight');value['generated_scientific_verdict']='EXPOSED_DIAGNOSTIC_PASS_ONLY'
        with self.assertRaises(ValueError): validate(value,'preflight')
        value=fixture('preflight');value['reference_support']['reference_incomplete_windows']=0
        with self.assertRaises(jsonschema.ValidationError): validate(value,'preflight')

    def test_authorization_needs_new_preflight_and_explicit_future_scope_reports(self):
        preflight=fixture('preflight'); authorization=fixture('authorization')
        authorization.update(preflight_sha256=digest(preflight),preflight_source_revision=preflight['source_revision'])
        authorization_references(authorization,preflight)
        for name in ('projection_scope_decision_sha256','historical_real_runtime_audit_sha256','real_scope_sha256','prior_failed_attempt_sha256'):
            value=copy.deepcopy(authorization); del value[name]
            with self.assertRaises(jsonschema.ValidationError): validate(value,'authorization')
        value=copy.deepcopy(authorization);value['preflight_sha256']=SHA(DOCS/'COUPLED-TIME-RUNNER-V2-PREFLIGHT.json')
        with self.assertRaises(ValueError): authorization_references(value,preflight)
        self.assertFalse(OP['real_execution_enabled'])
        self.assertEqual(OP['projection_decision'],'UNDECIDED')

    def test_parity_removes_only_six_new_leaves(self):
        groups=('ols_nominal','ols_actual','candidate_nominal','candidate_actual')
        expected={'scores.'+name+'[*].reference_incomplete_windows' for name in groups}
        expected|={'scores.windows[*].nominal.reference_incomplete_windows','scores.windows[*].actual.reference_incomplete_windows'}
        self.assertEqual(set(AMEND['allowed_per_source_additions']),expected)
        self.assertEqual(set(AMEND['diagnostic_groups']),set(groups))
        required=set(DEFS['preflight']['properties']['component_sha256']['required'])
        self.assertEqual(required,set(OP['preserved_r4_component_sha256'])|set(OP['future_preflight_components']))
        self.assertEqual(len(OP['future_preflight_components']),len(set(OP['future_preflight_components'])))
        self.assertEqual(set(OP['future_preflight_components']),set(OP['implementation_paths'])|{OP['registration_test_path']})

    def test_finite_duplicate_free_json_required(self):
        for text in ('{"count":1,"count":2}','{"count":NaN}','{"count":Infinity}','{"count":1e999}'):
            with self.assertRaises(ValueError): old_records.strict_json(text)

    def test_no_current_metadata_tokens_or_future_implementation_qualification(self):
        self.assertFalse((DOCS/OP['preflight_record']).exists())
        self.assertFalse((DOCS/OP['authorization_record']).exists())
        self.assertFalse(OP['stage']['C1_implemented']);self.assertFalse(OP['stage']['C3_full_preflight_run'])
        self.assertEqual(OP['projection_decision'],'UNDECIDED')
        self.assertEqual(OP['historical_real_runtime_metadata'],'NOT_AUDITED')
        self.assertEqual(set(OP['required_generated_gates']),set(DEFS['preflight']['properties']['gates']['required']))


if __name__ == '__main__':
    unittest.main()
