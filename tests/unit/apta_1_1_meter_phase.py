#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""MC-05 public-only contract, independent oracle and preservation checks."""
import ast
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests/unit')]
import apta_context_matched as canonical
import apta_meter_phase_points as core
import apta_meter_phase_evidence as production
import apta_meter_phase_oracle as oracle

Q = 1 << 32
COMPARISONS = 0
MAX_SECONDS = 0.0


def source_args(total=240000):
    return dict(total_frames=total, sample_rate=48000, known_gaps=[])


def window(m=4, witness=0, anchor=0, tempo=120000, first=0, end=240000, ordinal=0):
    step = 48000 * 60000 * Q // tempo
    points, index = [], 0
    while anchor * Q + index * step < end * Q:
        q = anchor * Q + index * step
        if first * Q <= q:
            points.append({'position_q32': q, 'ordinal': ordinal + index})
        index += 1
    p = next((p for p in points if p['ordinal'] == witness), None)
    meter = None if witness is None else {
        'numerator': m, 'denominator': 4, 'downbeat_frame': p['position_q32'] >> 32,
        'downbeat_fraction_q32': p['position_q32'] % Q, 'downbeat_ordinal': witness}
    return {'first_frame': first, 'end_frame': end,
            'model': {'anchor_frame': anchor, 'anchor_ordinal': ordinal, 'tempo_millibpm': tempo,
                      'period_q32': step, 'confidence': 0}, 'points': points, 'meter': meter}


def truth(m=4, start=0, stop=216000, step=24000, offset=0):
    beats = list(range(start, stop + 1, step))
    return {'beat_frames': beats, 'downbeat_frames': [b for b in beats if (b // step - offset) % m == 0],
            'meter_numerator': m, 'meter_denominator': 4}


class PhaseTests(unittest.TestCase):
    def compare(self, windows, reference, total=240000):
        global COMPARISONS, MAX_SECONDS
        saved = deepcopy((windows, reference))
        started = time.monotonic()
        args = source_args(total)
        self.assertEqual(core.extract_source(windows, **args), oracle.extract(windows))
        result = production.evaluate_source(windows, reference, **args)
        self.assertEqual(result, oracle.evaluate(windows, reference))
        for w, record in zip(windows, result):
            self.assertEqual(record['published'], canonical.meter_evidence(
                w['meter'], w['points'], w['first_frame'], w['end_frame'], reference, 'context'))
        self.assertEqual((windows, reference), saved)
        elapsed = time.monotonic() - started
        self.assertLess(elapsed, 60)
        MAX_SECONDS = max(MAX_SECONDS, elapsed)
        COMPARISONS += 1
        return result

    def test_01_registration_and_frozen_authorities(self):
        path = ROOT / 'docs/rust/METER-PHASE-MC05-PROTOCOL.json'
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), core.REGISTRATION_SHA256)
        record = json.loads(path.read_text())
        for p, sha in record['bindings'].items():
            self.assertEqual(hashlib.sha256((ROOT / p).read_bytes()).hexdigest(), sha, p)
        self.assertFalse(record['campaign_authorized'])
        self.assertFalse(record['private_scoring'])
        self.assertEqual((ROOT / 'VERSION').read_text().strip(), '1.0.1')

    def test_02_equivalent_witness_partial_reference(self):
        for m in (3, 4):
            a = self.compare([window(m=m)], truth(m=m, start=48000))[0]
            b = self.compare([window(m=m, witness=m)], truth(m=m, start=48000))[0]
            self.assertEqual(a['published']['downbeat_supported'], 0)
            self.assertEqual(b['published']['downbeat_supported'], 1)
            self.assertEqual({k:v for k,v in a.items() if k != 'published'},
                             {k:v for k,v in b.items() if k != 'published'})

    def test_03_parameterized_full_record_oracle(self):
        for m in (3, 4):
            for tempo in (40000, 99991, 120000, 123457, 300000):
                for anchor in (0, 6000):
                    for phase in range(m):
                        w = window(m=m, witness=phase, anchor=anchor, tempo=tempo)
                        for ref in (truth(m=m), truth(m=m, start=48000),
                                    truth(m=m, start=480000, stop=696000)):
                            self.compare([w], ref)

    def test_04_no_support_is_unknown(self):
        a = self.compare([window()], truth(start=480000, stop=696000))[0]
        self.assertEqual(a['supported_point_count'], 0)
        self.assertEqual(a['E_supported'], 0)
        self.assertIsNone(a['all_correct'])
        self.assertIsNone(a['conditional_accuracy'])
        self.assertGreater(a['phase_point_count'], 0)

    def test_05_wrong_phase_and_wrong_meter_not_best_point(self):
        wrong_phase = self.compare([window(witness=1)], truth())[0]
        self.assertEqual(wrong_phase['correct_point_count'], 0)
        self.assertEqual(wrong_phase['all_correct'], 0)
        wrong_meter = self.compare([window(m=3)], truth())[0]
        self.assertEqual(wrong_meter['published']['score']['meter_correct'], 0)
        self.assertGreater(wrong_meter['correct_point_count'], 0)
        self.assertLess(wrong_meter['correct_point_count'], wrong_meter['supported_point_count'])
        self.assertEqual(wrong_meter['all_correct'], 0)

    def test_06_cross_grid_is_not_equivalence(self):
        a = self.compare([window()], truth())[0]
        b = self.compare([window(anchor=6000)], truth())[0]
        self.assertNotEqual(a['phase']['phase_points'], b['phase']['phase_points'])
        self.assertEqual(a['all_correct'], 1)
        self.assertEqual(b['all_correct'], 0)

    def test_07_fractional_q32_carry_and_window_ownership(self):
        w = window(tempo=123457)
        p = w['points'][4]
        self.assertNotEqual(p['position_q32'] % Q, 0)
        w['meter'] = dict(w['meter'], downbeat_frame=p['position_q32'] >> 32,
                          downbeat_fraction_q32=p['position_q32'] % Q, downbeat_ordinal=4)
        self.compare([w], truth())
        # Exact fractional point falls after its whole-frame reference endpoint.
        end = p['position_q32'] >> 32
        r = {'beat_frames':[0, end], 'downbeat_frames':[0, end],
             'meter_numerator':4, 'meter_denominator':4}
        a = self.compare([w], r)[0]
        self.assertEqual(a['published']['downbeat_supported'], 0)
        for first, end in ((0, 96000), (96000, 192000), (96001, 192001)):
            self.compare([window(first=first, end=end, witness=None)], truth())

    def test_08_inclusive_reference_and_complete_mask(self):
        a = self.compare([window(end=192001)], truth(stop=192000))[0]
        self.assertEqual(a['published']['reference_complete'], 1)
        self.assertEqual(a['supported_point_count'], 3)
        b = self.compare([window()], truth(stop=192000))[0]
        self.assertEqual(b['published']['reference_complete'], 0)
        self.assertEqual(b['supported_point_count'], 3)

    def test_09_absent_meter_or_grid(self):
        for w in (window(witness=None), window(first=0,end=1,anchor=6000,witness=None),
                  {'first_frame':0, 'end_frame':240000,
                                          'model':None, 'meter':None, 'points':[]}):
            a = self.compare([w], truth())[0]
            self.assertEqual(a['phase_point_count'], 0)
            self.assertIsNone(a['all_correct'])

    def test_10_shapes_types_and_geometry_rejected(self):
        bad = []
        for key, value in (('first_frame',True), ('end_frame',240001), ('points',()),
                           ('meter',{}), ('model',{}), ('points',[{}]), ('extra',0)):
            w=window();w[key]=value;bad.append(w)
        for key, value in (('anchor_frame',-1), ('anchor_ordinal',True), ('tempo_millibpm',39999),
                           ('tempo_millibpm',300001), ('period_q32',1), ('confidence',1), ('extra',0)):
            w=window();w['model'][key]=value;bad.append(w)
        for key, value in (('numerator',True), ('numerator',5), ('denominator',8),
                           ('downbeat_frame',-1), ('downbeat_frame',1),
                           ('downbeat_fraction_q32',Q), ('downbeat_fraction_q32',1),
                           ('downbeat_ordinal',1), ('extra',0)):
            w=window();w['meter'][key]=value;bad.append(w)
        for key, value in (('ordinal',False), ('ordinal',1), ('ordinal',-1),
                           ('position_q32',0.0), ('position_q32',Q), ('extra',0)):
            w=window();w['points'][0][key]=value;bad.append(w)
        for points in (window()['points'][1:], window()['points'][::-1], [{}]*3073):
            w=window();w['points']=points;bad.append(w)
        w=window();w['model']=None;bad.append(w)
        for w in bad:
            with self.subTest(window=w), self.assertRaises(ValueError):
                core.extract_source([w], **source_args())

    def test_11_source_budgets_gaps_and_windows(self):
        for change in ({'total_frames':True}, {'total_frames':1440001}, {'total_frames':0},
                       {'sample_rate':44100}, {'sample_rate':48000.0},
                       {'known_gaps':None}, {'known_gaps':[1]}, {'known_gaps':()}):
            args=source_args();args.update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                core.extract_source([window()], **args)
        for windows in ([], (), [window()]*9, [window(),window()],
                        [window(first=96000, witness=4),window(end=96000)]):
            with self.assertRaises(ValueError): core.extract_source(windows, **source_args())
        with self.assertRaises(TypeError): core.extract_source([window()], truth=truth(), **source_args())
        w=window(witness=None);w['model']['anchor_ordinal']=(1<<63)-1
        for i,p in enumerate(w['points']):p['ordinal']=(1<<63)-1+i
        with self.assertRaises(ValueError):core.extract_source([w], **source_args())

    def test_12_largest_geometric_source_eight_windows(self):
        windows=[window(first=i*180000,end=(i+1)*180000,tempo=300000,witness=None)
                 for i in range(8)]
        for w in windows:
            p=w['points'][0]
            w['meter']={'numerator':4,'denominator':4,'downbeat_frame':p['position_q32']>>32,
                        'downbeat_fraction_q32':p['position_q32']%Q,'downbeat_ordinal':p['ordinal']}
        a=self.compare(windows, truth(stop=1440000,step=9600), total=1440000)
        self.assertEqual(len(a),8)
        self.assertEqual(sum(len(r['phase']['grid_points']) for r in a),150)
        self.assertEqual(sum(r['E_supported'] for r in a),8)

    def test_13_input_output_detachment(self):
        windows=[window()];ref=truth();saved=deepcopy((windows,ref))
        a=production.evaluate_source(windows,ref,**source_args())
        a[0]['phase']['model']['anchor_frame']=999
        a[0]['phase']['grid_points'][0]['ordinal']=999
        a[0]['phase']['phase_points'][0]['ordinal']=888
        a[0]['point_scores'][0]['point']['ordinal']=777
        a[0]['published']['score']['meter_correct']=0
        self.assertEqual((windows,ref),saved)
        self.assertEqual(production.evaluate_source(windows,ref,**source_args()),oracle.evaluate(windows,ref))

    def test_14_truth_validation_and_reference_invariance(self):
        w=[window()];before=core.extract_source(w,**source_args())
        for ref in (truth(),truth(start=48000),truth(start=480000,stop=696000)):
            production.evaluate_source(w,ref,**source_args())
            self.assertEqual(core.extract_source(w,**source_args()),before)
        for ref in (None,{},dict(truth(),extra=1),dict(truth(),beat_frames=[]),
                    dict(truth(),downbeat_frames=[1]),dict(truth(),meter_numerator=True)):
            with self.assertRaises(ValueError):production.evaluate_source(w,ref,**source_args())

    def test_15_pure_import_boundary_and_no_io(self):
        for name, allowed in (('tools/apta_meter_phase_points.py',set()),
                              ('tools/apta_meter_phase_evidence.py',{'apta_context_matched','apta_meter_phase_points'}),
                              ('tests/unit/apta_meter_phase_oracle.py',{'fractions','statistics'})):
            imports=set()
            for node in ast.walk(ast.parse((ROOT/name).read_text())):
                if isinstance(node,ast.Import):imports.update(n.name for n in node.names)
                if isinstance(node,ast.ImportFrom):imports.add(node.module)
            self.assertEqual(imports,allowed)
        code='''import sys,json
sys.path.insert(0,sys.argv[1])
import apta_meter_phase_points as c
import apta_meter_phase_evidence as e
w=json.loads(sys.argv[2]);t=json.loads(sys.argv[3])
def audit(event,args):
    if event=='open' or event.startswith(('os.','socket.','subprocess.')):raise RuntimeError(event)
sys.addaudithook(audit)
args=dict(total_frames=240000,sample_rate=48000,known_gaps=[])
c.extract_source(w,**args);e.evaluate_source(w,t,**args)
print('PURE_PASS')
'''
        p=subprocess.run([sys.executable,'-c',code,str(ROOT/'tools'),json.dumps([window()]),json.dumps(truth())],
                         capture_output=True,text=True,timeout=60,check=True,
                         env=dict(os.environ,APTA_REFERENCE_LABELS='/absent/public/reference.json'))
        self.assertEqual(p.stdout.strip(),'PURE_PASS')

    def test_16_canonical_tolerance_boundary_and_irregular_periods(self):
        for frame, correct in ((98399,1),(98400,1),(98401,0)):
            w=window(anchor=frame,first=frame,end=frame+1)
            result=self.compare([w],truth())[0]
            self.assertEqual(result['correct_point_count'],correct)
        reference={'beat_frames':[0,12000,40000,50000,80000,96000,125000,
                                  150000,175000,200000,239999],
                   'downbeat_frames':[0,50000,150000,239999],
                   'meter_numerator':4,'meter_denominator':4}
        self.compare([window()],reference)


if __name__ == '__main__':
    started=time.monotonic()
    program=unittest.main(exit=False,verbosity=2)
    report={'tests':program.result.testsRun,'failures':len(program.result.failures),
            'errors':len(program.result.errors),'skips':len(program.result.skipped),
            'full_record_oracle_comparisons':COMPARISONS,'wall_seconds':time.monotonic()-started,
            'max_call_including_oracle_seconds':MAX_SECONDS,
            'self_max_rss_kib':resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            'registration_sha256':core.REGISTRATION_SHA256,'scope':'PUBLIC_ONLY'}
    if os.environ.get('APTA_MC05_RESULT'):
        with open(os.environ['APTA_MC05_RESULT'],'x') as f:json.dump(report,f,sort_keys=True,indent=2)
    print(json.dumps(report,sort_keys=True))
    sys.exit(not program.result.wasSuccessful())
