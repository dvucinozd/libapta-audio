#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""P0 public registration/normal-equation controls; no projector/scorer execution."""
from copy import deepcopy
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import unittest

import jsonschema

ROOT=Path(__file__).resolve().parents[2]
DOCS=ROOT/'docs/rust'
LOAD=lambda path:json.loads(Path(path).read_text())
SHA=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
PATH=DOCS/'CONTEXT-ANCHOR-PROJECTION-PROTOCOL.json'
REG=LOAD(PATH);REG_SHA=SHA(PATH)
SCHEMA=LOAD(DOCS/REG['record_schema'])
EXAMPLES=LOAD(DOCS/REG['examples'])
Q=1<<32


def rnd(value):
    value+=Fraction(1,2)
    return value.numerator//value.denominator


def validate_shape(value,kind):
    def exact(v):
        if type(v) in (bool,float): raise ValueError('exact integer fields, never booleans/floats')
        if type(v) is dict:
            for child in v.values(): exact(child)
        if type(v) is list:
            for child in v: exact(child)
    exact(value)
    try:
        jsonschema.Draft202012Validator({'$defs':SCHEMA['$defs'],'$ref':'#/$defs/'+kind}).validate(value)
    except jsonschema.ValidationError as error:
        raise ValueError('record shape differs') from error
    if kind=='input':
        beats,db=value['beat_frames'],value['downbeat_ordinals']
        if (not 0<=value['first_frame']<value['end_frame']<=value['total_frames']
                or any(a>=b for a,b in zip(beats,beats[1:]))
                or any(beat>=value['total_frames'] for beat in beats)
                or any(a>=b for a,b in zip(db,db[1:])) or any(i>=len(beats) for i in db)):
            raise ValueError('source/order/geometry differs')


def normal_equations(values):
    """Independent tiny oracle: assemble two rows and eliminate the first column."""
    matrix=[[Fraction(0) for _ in range(3)] for _ in range(2)]
    for index,value in enumerate(values):
        row=(Fraction(1),Fraction(index))
        for left in range(2):
            for right in range(2): matrix[left][right]+=row[left]*row[right]
            matrix[left][2]+=row[left]*value
    ratio=matrix[1][0]/matrix[0][0]
    for column in range(3): matrix[1][column]-=ratio*matrix[0][column]
    b=matrix[1][2]/matrix[1][1]
    a=(matrix[0][2]-matrix[0][1]*b)/matrix[0][0]
    return a,b


def validate_example(source,output):
    validate_shape(source,'input');validate_shape(output,'output')
    if output['registration_sha256']!=REG_SHA: raise ValueError('registration authority differs')
    for key in ('total_frames','first_frame','end_frame'):
        if output[key]!=source[key]: raise ValueError('source geometry differs')
    indices=[i for i,beat in enumerate(source['beat_frames']) if source['first_frame']<=beat<source['end_frame']]
    if (output['source_beat_count']!=len(source['beat_frames']) or output['observed_count']!=len(indices)
            or output['first_observed_ordinal']!=(indices[0] if indices else None)):
        raise ValueError('original source indices differ')
    if len(indices)<4:
        if output['status']!='INSUFFICIENT_EVENTS': raise ValueError('insufficient observations')
        return
    a,b=normal_equations([source['beat_frames'][i] for i in indices])
    fit=output['fit']
    if fit is None: raise ValueError('fit absent')
    for prefix,expected in (('intercept',a),('period',b)):
        if (fit[prefix+'_numerator']!=expected.numerator or fit[prefix+'_denominator']!=expected.denominator):
            raise ValueError('normal-equation rational fit differs')
    A,T=rnd(a),rnd(Fraction(48000*60000,1)/b)
    if fit['rounded_anchor']!=A or fit['tempo_millibpm']!=T: raise ValueError('frozen rounding differs')
    if not 40000<=T<=300000:
        if output['status']!='UNSUPPORTED_TEMPO': raise ValueError('unsupported tempo rescued')
        return
    if not 0<=A<source['total_frames']:
        if output['status']!='SOURCE_ANCHOR_OUT_OF_DOMAIN': raise ValueError('source anchor repaired')
        return
    P=(48000*60000*Q)//T
    model={'anchor_frame':A,'anchor_ordinal':indices[0],'tempo_millibpm':T,'period_q32':P,'confidence':0}
    if output['model']!=model: raise ValueError('model changed or anchor shifted')
    # Independent bounded enumeration, not the implementation's ceiling formula.
    owned=[j for j in range(3073) if source['first_frame']*Q<=A*Q+j*P<source['end_frame']*Q]
    expected=[{'position_q32':A*Q+j*P,'ordinal':indices[0]+j} for j in owned]
    if (output['point_count']!=len(expected) or output['points']!=expected
            or output['first_index']!=(owned[0] if owned else None)):
        raise ValueError('owned lattice/ordinal/count differs')
    if output['status']!=('ACCEPTED' if owned else 'NO_OWNED_POINTS'):
        raise ValueError('ownership status differs')
    # Verify the exact selected raw bar witness independently of output metadata.
    db=[i for i in source['downbeat_ordinals'] if source['first_frame']<=source['beat_frames'][i]<source['end_frame']]
    chosen=None;eligible=None
    for left,middle,right in zip(db,db[1:],db[2:]):
        if middle-left not in (3,4) or right-middle!=middle-left: continue
        def mapped(index):
            j=rnd(Fraction((source['beat_frames'][index]-A)*Q,P));q=A*Q+j*P
            return j,q,j>=0 and abs(Fraction(q,Q)-source['beat_frames'][index])<=b/10
        if not mapped(left)[2]: continue
        eligible=(left,middle,right)
        for raw_index in eligible:
            j,q,valid=mapped(raw_index)
            if valid and source['first_frame']*Q<=q<source['end_frame']*Q:
                chosen={'kind':'estimated','numerator':middle-left,'denominator':4,'downbeat_frame':q//Q,
                         'downbeat_fraction_q32':q%Q,'downbeat_ordinal':indices[0]+j,'witness_ordinals':list(eligible)}
                break
        break
    if output['meter']!=chosen: raise ValueError('same-witness owned meter differs')
    state='OWNED' if chosen else 'NO_OWNED_WITNESS' if eligible else 'NO_ELIGIBLE_BARS'
    if output['meter_status']!=state: raise ValueError('meter state differs')


def examples():
    result=deepcopy(EXAMPLES['project_window_examples'])
    for row in result: row['output']['registration_sha256']=REG_SHA
    return result


class RegistrationTests(unittest.TestCase):
    def test_all_registered_document_and_source_hashes(self):
        for name,digest in REG['binding'].items(): self.assertEqual(SHA(DOCS/name),digest,name)
        for name,digest in REG['reviewed_source_sha256'].items(): self.assertEqual(SHA(ROOT/name),digest,name)
        for name,digest in REG['frozen_c3_component_sha256'].items(): self.assertEqual(SHA(ROOT/name),digest,name)
        self.assertEqual(len(REG['frozen_c3_component_sha256']),37)

    def test_local_schema_is_closed_without_remote_references(self):
        jsonschema.Draft202012Validator.check_schema(SCHEMA)
        def inspect(value):
            if type(value) is dict:
                if '$ref' in value: self.assertTrue(value['$ref'].startswith('#/$defs/'))
                if value.get('type')=='object': self.assertIs(value['additionalProperties'],False)
                for child in value.values(): inspect(child)
            if type(value) is list:
                for child in value: inspect(child)
        inspect(SCHEMA)

    def test_all_public_examples_independent_fit_and_owned_witness(self):
        self.assertEqual(EXAMPLES['scope'],'PUBLIC_ARITHMETIC_ONLY_NO_R5_CASE')
        self.assertFalse(EXAMPLES['production_projector_called']);self.assertFalse(EXAMPLES['scorer_called'])
        self.assertEqual(len(examples()),5)
        for row in examples():
            with self.subTest(row=row['name']): validate_example(row['input'],row['output'])
        context=examples()[1]
        self.assertLess(context['output']['model']['anchor_frame'],context['input']['first_frame'])
        self.assertEqual(context['output']['first_index'],1)
        self.assertEqual(context['output']['meter']['downbeat_ordinal'],6)
        self.assertEqual(context['output']['meter']['witness_ordinals'],[2,6,10])

    def test_exact_q32_endpoint_and_ordinal_ledger(self):
        self.assertEqual(len(EXAMPLES['lattice_only_examples']),6)
        for row in EXAMPLES['lattice_only_examples']:
            A,P,F,E,g=(row[k] for k in ('anchor_q32','period_q32','first_frame','end_frame','anchor_ordinal'))
            enumerated=[j for j in range(3073) if F*Q<=A+j*P<E*Q]
            self.assertEqual(enumerated,row['expected_indices'],row['name'])
            lo=max(0,-(-(F*Q-A)//P));hi=(E*Q-1-A)//P
            self.assertEqual(list(range(lo,hi+1)),enumerated)
            self.assertEqual([{'position_q32':A+j*P,'ordinal':g+j} for j in enumerated],row['expected_points'])

    def test_missing_boolean_float_duplicate_out_of_source_inputs(self):
        baseline=examples()[1]['input']
        corruptions=[lambda v:v.update(first_frame=True),lambda v:v.update(total_frames=240000.0),
                     lambda v:v.update(first_frame=v['end_frame']),lambda v:v.update(end_frame=v['total_frames']+1),
                     lambda v:v.update(beat_frames=[1,1]),lambda v:v.update(beat_frames=[2,1]),
                     lambda v:v.update(beat_frames=[240000]),lambda v:v.update(downbeat_ordinals=[3071]),
                     lambda v:v.update(downbeat_ordinals=[6,2]),lambda v:v.update(reference_labels=[]),
                     lambda v:v.pop('beat_frames')]
        for corruption in corruptions:
            value=deepcopy(baseline);corruption(value)
            with self.assertRaises(ValueError): validate_shape(value,'input')

    def test_shifted_anchor_extra_point_relabeling_and_meter_borrow_rejected(self):
        row=examples()[1]
        mutations=[lambda v:v['model'].update(anchor_frame=row['input']['first_frame']),
                   lambda v:v['points'][0].update(ordinal=0),lambda v:v.update(point_count=9),
                   lambda v:v['points'][0].update(position_q32=0),lambda v:v.update(registration_sha256='0'*64),
                   lambda v:v['meter'].update(numerator=3),lambda v:v['meter'].update(downbeat_ordinal=2),
                   lambda v:v['meter'].update(witness_ordinals=[0,4,8]),
                   lambda v:v['fit'].update(rounded_anchor=v['fit']['rounded_anchor']+1),
                   lambda v:v.update(unknown_scoring_field=True)]
        for mutate in mutations:
            value=deepcopy(row['output']);mutate(value)
            with self.assertRaises(ValueError): validate_example(row['input'],value)

    def test_source_negative_anchor_and_tempo_are_not_repaired(self):
        for row in examples()[2:]:
            value=deepcopy(row['output']);value['status']='ACCEPTED'
            with self.assertRaises(ValueError): validate_example(row['input'],value)

    def test_projection_axis_and_all_execution_gates_remain_separate(self):
        self.assertEqual(REG['decision'],'PROJECTION_REPRESENTATION_AXIS_KEEP_COUPLED_CANDIDATE_FIXED')
        for key in ('execution_enabled','implementation_present','candidate_constructed','scoring_run',
                    'real_execution_enabled','real_authorization_created','native_activation','physical_p4','release'):
            self.assertIs(REG[key],False,key)
        self.assertEqual(REG['frozen_scientific_control']['legacy_verdict'],'REJECTED')
        self.assertEqual(REG['frozen_scientific_control']['legacy_gate_pass_count'],14)
        self.assertFalse(REG['frozen_scientific_control']['new_musical_acceptance_criteria_registered'])
        self.assertEqual(REG['stage']['next'],'P1_PURE_GENERATED_PROJECTOR_AND_INDEPENDENT_ORACLE')

    def test_consumer_snapshot_and_native_pin_are_explicit(self):
        consumer=REG['consumer_snapshot']
        self.assertEqual(consumer['revision'],'df7f8f4aea3852cafb660ea67dae26f1610287ec')
        self.assertEqual(consumer['libapta_pin'],'25d09de364e9507532c0508c838a8fecf0772d13')
        self.assertFalse(consumer['provider_pin_or_checkout_modified'])
        self.assertFalse(consumer['current_consumer_built']);self.assertFalse(consumer['hardware_qualified'])
        self.assertEqual(len(consumer['files']),4)


if __name__=='__main__':
    unittest.main()
