#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""MC-07 newly generated public fixtures only; no saved MC-03 cases."""
from copy import deepcopy
from itertools import product
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests/unit')]
# Distinct module name: tool and test intentionally share a basename.
import importlib.util
spec=importlib.util.spec_from_file_location('_mc07_adapter',ROOT/'tools/apta_1_1_meter_phase_audit.py')
adapter=importlib.util.module_from_spec(spec);sys.modules[spec.name]=adapter;spec.loader.exec_module(adapter)
engine,io=adapter.engine,adapter.io
import apta_context_matched as canonical
import apta_meter_context_projection as public_projector
import apta_meter_phase_oracle as phase_oracle
from apta_context_matched_oracle import ledger_oracle

BASE=Path(os.environ.get('APTA_MC07_OUTPUT','/tmp/libapta-mc07-public-tests')).resolve()
COUNTS={'phase_oracle_comparisons':0,'ledger_oracle_comparisons':0,'negative_resource_checks':0}


def old_for(normalized, truth):
    cells={}
    for a in adapter.ARMS:
        values=[]
        for w in normalized[a]:
            evidence=canonical.meter_evidence(w['meter'],w['points'],w['first_frame'],w['end_frame'],truth,'context')
            grid=dict.fromkeys(canonical.SCORE_KEYS,0)
            grid.update(windows=1,reference_complete_windows=evidence['reference_complete'],points=len(w['points']))
            values.append(dict(normalized=canonical.make_cell(grid,evidence),grid_score=grid,
                               meter_score=evidence['score'],points=w['points'],meter=w['meter']))
        cells[a+'_context']=values
        cells[a+'_legacy']=deepcopy(values)
    return dict(cells=cells,raw_actual={'public_control':'unchanged'},event_metrics={'public_control':1})


def payload(index=0, group=0, window_count=6):
    total=1440000
    beats=[12000+24000*i for i in range(60)] if group!=3 else [400*i for i in range(3072)]
    meter=3 if group==1 else 4
    downbeats=[] if group==2 else list(range(0,len(beats),meter))
    windows=[dict(first_frame=i*240000,end_frame=(i+1)*240000) for i in range(window_count)]
    original=dict(beat_frames=beats,downbeat_ordinals=downbeats)
    coupled=dict(beat_frames=[b-1 for b in beats] if group==1 else list(beats),downbeat_ordinals=list(downbeats))
    job=dict(total_frames=total,windows=windows,known_gaps=[],original=original,coupled=coupled)
    projections={a:[public_projector.project_window(**job[a],total_frames=total,**w,known_gaps=[]) for w in windows]
                 for a in adapter.ARMS}
    ref_beats=[12000+24000*i for i in range(60)]
    truth=dict(beat_frames=ref_beats,downbeat_frames=ref_beats[::meter],meter_numerator=meter,meter_denominator=4)
    normalized=adapter.normalize_job(job,projections)
    # Public saved-format structural controls, not a grid accuracy claim.
    old=old_for(normalized,truth)
    label=dict(truth,track=f'public-{index}',genre=f'g{group}',requested_frames=total)
    return job,projections,truth,old,label


def fixture(path, sources=1):
    path.mkdir(parents=True,exist_ok=False)
    rows,labels,bindings=[],[],{}
    def write(name,value):
        entry=engine.write(path/name,value);bindings[entry['path']]=entry['sha256'];return entry
    for i in range(sources):
        job,projections,truth,old,label=payload(i,i%4)
        j=write(f'{i}.job.json',job);p=write(f'{i}.projection.json',projections)
        proof=write(f'{i}.proof.json',dict(index=i,job_sha256=j['sha256'],projection_sha256=p['sha256']))
        evaluation=write(f'{i}.evaluation.json',old)
        rows.append(dict(index=i,id=label['track'],job=j,projection=p,proof=proof,evaluation=evaluation))
        labels.append(label)
    label_entry=write('labels.json',dict(tracks=labels))
    bundle=dict(format='libapta-meter-phase-inputs-1',kind='PUBLIC_PREFLIGHT',input_roots=[str(path)],
                rows=rows,labels=label_entry,bindings=bindings,launch=None)
    return engine.write(path/'bundle.json',bundle)


def extract_memory(job,projections,index=0):
    windows=adapter.normalize_job(job,projections)
    import apta_meter_phase_points as geometry
    return dict(index=index,total_frames=job['total_frames'],windows=windows,
                phases={a:geometry.extract_source(windows[a],total_frames=job['total_frames'],
                        sample_rate=48000,known_gaps=[]) for a in adapter.ARMS})


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        BASE.mkdir(parents=True,exist_ok=True)
        cls.base=BASE/'core';cls.base.mkdir(exist_ok=False)

    def test_01_frozen_authorities_and_two_file_boundary(self):
        reg=adapter.registration()
        self.assertEqual(reg['readiness']['allowed_new_source_paths'],[
            'tools/apta_1_1_meter_phase_audit.py','tests/unit/apta_1_1_meter_phase_audit.py'])
        self.assertFalse(reg['readiness']['execution_authorized'])
        for p,d in reg['source_component_sha256'].items():self.assertEqual(io.sha(ROOT/p),d,p)
        self.assertGreaterEqual(len(adapter.components()),83)

    def test_02_saved_format_full_record_oracle_and_immutability(self):
        for group in range(4):
            job,projections,truth,old,label=payload(group,group)
            before=deepcopy((job,projections,truth,old))
            extracted=extract_memory(job,projections)
            result=adapter.interpret(extracted,truth,old)
            for a in adapter.ARMS:
                self.assertEqual(extracted['phases'][a],phase_oracle.extract(extracted['windows'][a]))
                self.assertEqual(result[a],phase_oracle.evaluate(extracted['windows'][a],truth))
                COUNTS['phase_oracle_comparisons']+=1
            self.assertEqual((job,projections,truth,old),before)

    def test_03_normalization_rejects_geometry_meter_and_extra_fields(self):
        job,p,truth,old,_=payload()
        changes=[('first_frame',1),('first_frame',False),('first_frame',0.0),
                 ('end_frame',1),('total_frames',True),('point_count',999),
                 ('source_beat_count',1),('format','old'),('registration_sha256','0'*64),
                 ('meter_origin','CONTEXT'),('status','UNKNOWN'),('extra',0)]
        for key,value in changes:
            v=deepcopy(p);v['original'][0][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):adapter.normalize_job(job,v)
        for where,key,value in (('points','ordinal',1),('points','position_q32',0),
                                ('meter','downbeat_fraction_q32',1),('model','period_q32',1)):
            v=deepcopy(p)
            if where=='points':v['original'][0][where][0][key]=value
            else:v['original'][0][where][key]=value
            with self.assertRaises(ValueError):adapter.normalize_job(job,v)
        for change in ({'known_gaps':[1]},{'windows':job['windows'][::-1]},
                       {'original':dict(job['original'],downbeat_ordinals=[True])}):
            bad=deepcopy(job);bad.update(change)
            with self.assertRaises(ValueError):adapter.normalize_job(bad,p)

    def test_04_absence_and_model_empty_window(self):
        for group in (2,3):
            job,p,truth,old,_=payload(group=group)
            r=adapter.interpret(extract_memory(job,p),truth,old)
            self.assertTrue(all(v['all_correct'] is None for v in r['original']))
        job,p,*_=payload(window_count=1)
        job['windows']=[dict(first_frame=0,end_frame=1)]
        for a in adapter.ARMS:
            v=p[a][0]
            v.update(first_frame=0,end_frame=1,status='NO_OWNED_POINTS',points=[],point_count=0,
                     meter=None,baseline_meter=None,meter_origin=None,meter_status='NOT_EVALUATED',context=None)
        result=adapter.normalize_job(job,p)
        self.assertEqual(result['original'][0]['points'],[])
        self.assertIsNotNone(result['original'][0]['model'])

    def test_05_original_cell_and_frozen_K_parity_rejects_changes(self):
        job,p,truth,old,_=payload();x=extract_memory(job,p)
        for field in ('meter_score','points','meter','normalized'):
            wrong=deepcopy(old);wrong['cells']['original_context'][0][field]={}
            with self.assertRaises((ValueError,KeyError)):adapter.interpret(x,truth,wrong)
        bad=deepcopy(x);bad['phases']['original'][0]['phase_points']=[]
        with self.assertRaises(ValueError):adapter.interpret(bad,truth,old)

    def test_06_proof_job_binding_and_swapped_index(self):
        entry=fixture(self.base/'proof')
        row=engine.read(entry)['rows'][0]
        x=adapter.extract_bound(0,row['job'],row['projection'],row['proof'])
        self.assertEqual(x['index'],0)
        with self.assertRaises(ValueError):adapter.extract_bound(1,row['job'],row['projection'],row['proof'])
        data=engine.read(row['job']);data['original']['beat_frames'][0]+=1
        Path(row['job']['path']).write_text(json.dumps(data))
        with self.assertRaises(ValueError):adapter.extract_bound(0,row['job'],row['projection'],row['proof'])

    def test_07_all_finite_E_support_correctness_partitions(self):
        job,p,truth,old,_=payload();result=adapter.compact(adapter.interpret(extract_memory(job,p),truth,old),old)
        template=result['original'][0]
        states=[]
        for full,supported,good in product((0,1),(0,1),(0,1)):
            if not supported and good:continue
            v=deepcopy(template);v['cell']['reference_complete']=full
            v['cell']['D']=0;v['cell']['correct']['downbeat']=0
            v.update(E=supported,phase_correct=good if supported else None,
                     K=3,points=2 if supported else 0,correct_points=2 if good else 0)
            states.append(v)
        for left,right,stratum in product(states,states,adapter.STRATA):
            right=deepcopy(right);right['cell']['reference_complete']=left['cell']['reference_complete']
            actual=adapter.e_ledger([left],[right],stratum,'source')
            # Test-only projection into the existing set oracle; production E is
            # separately named and never passed to the canonical P3 screen.
            cells=[]
            for v in (left,right):
                c=deepcopy(v['cell']);c['D']=v['E'];c['correct']['downbeat']=v['phase_correct'] or 0;cells.append(c)
            expected=ledger_oracle([cells[0]],[cells[1]],'candidate_vs_original_context','downbeat',stratum,'source')
            for k,value in actual['counts'].items():self.assertEqual(value,expected['counts'][k])
            for key in ('left_conditional_accuracy','right_conditional_accuracy'):self.assertEqual(actual[key],expected[key])
            COUNTS['ledger_oracle_comparisons']+=1

    def test_08_bad_E_denominators_nulls_alignment(self):
        job,p,t,old,_=payload();r=adapter.compact(adapter.interpret(extract_memory(job,p),t,old),old)
        for change in ({'E':0},{'phase_correct':None},{'correct_points':999},{'K':False},
                       {'points':0},{'phase_correct':False}):
            v=deepcopy(r['original']);v[0].update(change)
            with self.assertRaises(ValueError):adapter.e_ledger(v,r['coupled'],'all','source')
        v=deepcopy(r['coupled']);v[0]['cell']['first_frame']=1
        with self.assertRaises(ValueError):adapter.e_ledger(r['original'],v,'all','source')

    def test_09_hierarchical_conservation_unequal_groups(self):
        rows=[];groups=['a','a','b']
        for g in (0,1,2):
            job,p,t,old,_=payload(group=g);rows.append(adapter.compact(adapter.interpret(extract_memory(job,p),t,old),old))
        report=adapter.summarize(rows,groups)
        self.assertEqual(len(report['ledgers']),(3+2+1)*9)
        for arm in adapter.ARMS:
            subset=[x for x in report['subtotals'] if x['arm']==arm]
            for stratum in adapter.STRATA:
                cohort=next(v['counts'] for v in subset if v['level']=='cohort' and v['stratum']==stratum)
                for level in ('source','group'):
                    for key,value in cohort.items():
                        self.assertEqual(value,sum(v['counts'][key] for v in subset if v['level']==level and v['stratum']==stratum))
            all_counts=next(v['counts'] for v in subset if v['level']=='cohort' and v['stratum']=='all')
            for key,value in all_counts.items():
                self.assertEqual(value,sum(v['counts'][key] for v in subset if v['level']=='cohort' and v['stratum']!='all'))
        self.assertNotEqual(report['subtotals'][0]['counts']['windows'],report['subtotals'][0]['counts']['K_points'])

    def test_10_strict_json_symlink_output_and_private_gates(self):
        for raw in ('{"x":1,"x":2}','{"x":NaN}','{"x":1e999}'):
            with self.assertRaises(ValueError):engine.strict_json(raw)
        # A deliberately invalid link must not sit in the supervised output
        # tree, where the outer integrity watcher correctly rejects it too.
        with tempfile.TemporaryDirectory(prefix='libapta-mc07-link-') as folder:
            path=Path(folder)/'target';path.write_text('public');link=Path(folder)/'link';link.symlink_to(path)
            with self.assertRaises(ValueError):io.bind(link,io.sha(path),{})
            with self.assertRaises(ValueError):io.disjoint_output(path,[Path(folder)])
        bad=engine.write(self.base/'bad-ready.json',dict(status='NOT_READY'))
        with self.assertRaises(ValueError):adapter.prepare_saved(self.base/'saved-disallowed',bad,dict(path='x',sha256='0'*64))

    def test_11_hard_source_deadline(self):
        original=engine.signal.setitimer
        def shortened(which,seconds):return original(which,.001 if seconds else 0)
        with patch.object(engine.signal,'setitimer',side_effect=shortened):
            with self.assertRaisesRegex(ValueError,'source time cap'):
                with engine.deadline():time.sleep(.1)

    def test_12_wrong_meter_phase_is_not_best_point_or_joint_correctness(self):
        job,p,t,_,_=payload(group=1)
        t['meter_numerator']=4;t['downbeat_frames']=t['beat_frames'][::4]
        extracted=extract_memory(job,p);old=old_for(extracted['windows'],t)
        r=adapter.interpret(extracted,t,old)['original'][0]
        self.assertEqual(r,phase_oracle.evaluate(extracted['windows']['original'],t)[0])
        self.assertEqual(r['published']['score']['meter_correct'],0)
        self.assertGreater(r['correct_point_count'],0)
        self.assertLess(r['correct_point_count'],r['supported_point_count'])
        self.assertEqual(r['all_correct'],0)
        COUNTS['phase_oracle_comparisons']+=1

    def test_13_fractional_Q32_saved_format_round_trip(self):
        job,p,_,_,_=payload()
        beats=[1000+10001*i for i in range(60)]
        for a in adapter.ARMS:job[a]=dict(beat_frames=list(beats),downbeat_ordinals=list(range(0,60,4)))
        p={a:[public_projector.project_window(**job[a],total_frames=job['total_frames'],**w,known_gaps=[])
              for w in job['windows']] for a in adapter.ARMS}
        t=dict(beat_frames=beats,downbeat_frames=beats[::4],meter_numerator=4,meter_denominator=4)
        x=extract_memory(job,p);old=old_for(x['windows'],t);r=adapter.interpret(x,t,old)
        self.assertTrue(any(v['position_q32']%4294967296 for w in x['windows']['original'] for v in w['points']))
        for a in adapter.ARMS:
            self.assertEqual(r[a],phase_oracle.evaluate(x['windows'][a],t));COUNTS['phase_oracle_comparisons']+=1


class OperationalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime=engine.leaf(Path(os.environ['APTA_MC07_RUNTIME']).resolve())
        cls.base=BASE/'operational';cls.base.mkdir(exist_ok=False)
        cls.descriptor=adapter.check_runtime(cls.runtime)
        cls.python=cls.descriptor['parent_receipt']['interpreter']

    def test_01_full_public_100_source_600_window_two_phase_pipeline(self):
        self.__class__.bundle=fixture(self.base/'full-inputs',100)
        self.__class__.attempt=self.base/'full-attempt'
        report=adapter.run_public(self.bundle,self.attempt,self.runtime,[BASE])
        self.assertEqual((report['sources'],report['groups'],report['windows'],len(report['ledgers'])),(100,4,600,945))
        self.assertEqual(len(report['subtotals']),630)
        io.recheck(engine.read(self.bundle)['bindings'])
        for role in adapter.ROLES:
            complete=engine.read(engine.leaf(self.attempt/(role+'.complete.json')))
            self.assertEqual(len(complete['records']),100)
            self.assertLessEqual(complete['self_max_rss_kib'],262144)
            self.assertEqual(complete['address_space_limit_bytes'],262144*1024)
        # Independent public oracle on every frozen/executed record, not just a sample.
        row_values=[];groups=[]
        for i,row in enumerate(engine.read(self.bundle)['rows']):
            frozen=engine.read(engine.leaf(self.attempt/'extract'/(str(i)+'.json')))
            actual=engine.read(engine.leaf(self.attempt/'interpret'/(str(i)+'.json')))
            job,p,truth,old,label=payload(i,i%4)
            for arm in adapter.ARMS:
                self.assertEqual(actual['arms'][arm],phase_oracle.evaluate(frozen['windows'][arm],truth))
                COUNTS['phase_oracle_comparisons']+=1
            if i%4 in (1,3):self.assertTrue(all(not p['same_grid'] and p['same_K'] is None for p in actual['grid_comparisons']))
            row_values.append(actual['compact']);groups.append(actual['group'])
        for entry in report['ledgers']:
            p=entry['ledger'];indices=list(range(100)) if p['level']=='cohort' else (
                [int(entry['group'])] if p['level']=='source' else [i for i,g in enumerate(groups) if g==entry['group']])
            a=[x for i in indices for x in row_values[i]['original']]
            b=[x for i in indices for x in row_values[i]['coupled']]
            if entry['domain']=='E':
                converted=[]
                for side in (a,b):
                    values=[]
                    for v in side:
                        c=deepcopy(v['cell']);c['D']=v['E'];c['correct']['downbeat']=v['phase_correct'] or 0;values.append(c)
                    converted.append(values)
                expected=ledger_oracle(*converted,'candidate_vs_original_context','downbeat',p['stratum'],p['level'])
            else:
                expected=ledger_oracle([x['cell'] for x in a],[x['cell'] for x in b],
                    'candidate_vs_original_context',p['family'],p['stratum'],p['level'])
            for key,value in p['counts'].items():self.assertEqual(value,expected['counts'][key])
            COUNTS['ledger_oracle_comparisons']+=1

    def test_02_extract_without_reference_or_saved_evaluation_access(self):
        bundle=fixture(self.base/'blind-inputs',2);b=engine.read(bundle)
        moved=[]
        for entry in [b['labels'],*(r['evaluation'] for r in b['rows'])]:
            p=Path(entry['path']);q=p.with_suffix('.hidden');p.rename(q);moved.append((p,q))
        attempt=self.base/'blind-attempt';attempt.mkdir()
        try:adapter.dispatch('extract',bundle,attempt,self.runtime,[BASE])
        finally:
            for p,q in moved:q.rename(p)
        self.assertTrue((attempt/'extract.complete.json').exists())
        adapter.dispatch('interpret',bundle,attempt,self.runtime,[BASE])

    def test_03_stale_proof_closes_invalid_and_cannot_resume(self):
        bundle=fixture(self.base/'stale-inputs');b=engine.read(bundle)
        p=Path(b['rows'][0]['proof']['path']);p.write_text('{}')
        attempt=self.base/'stale-attempt';attempt.mkdir()
        with self.assertRaises(ValueError):adapter.dispatch('extract',bundle,attempt,self.runtime,[BASE])
        self.assertTrue((attempt/'INVALID.json').exists())
        with self.assertRaises(ValueError):adapter.dispatch('extract',bundle,attempt,self.runtime,[BASE])
        self.assertFalse((attempt/'extract.complete.json').exists())

    def test_04_swapped_reference_order_is_invalid_after_freeze(self):
        bundle=fixture(self.base/'swapped-inputs',2);b=engine.read(bundle)
        labels=engine.read(b['labels']);labels['tracks'].reverse()
        p=Path(b['labels']['path']);p.write_text(json.dumps(labels));b['labels']=engine.leaf(p)
        b['bindings'][str(p)]=b['labels']['sha256']
        Path(bundle['path']).write_text(json.dumps(b));bundle=engine.leaf(Path(bundle['path']))
        attempt=self.base/'swapped-attempt';attempt.mkdir()
        adapter.dispatch('extract',bundle,attempt,self.runtime,[BASE])
        with self.assertRaises(ValueError):adapter.dispatch('interpret',bundle,attempt,self.runtime,[BASE])
        self.assertTrue((attempt/'INVALID.json').exists())

    def test_05_changed_source_and_foreign_import_profile_rejected(self):
        for change in ('components','profiles'):
            d=deepcopy(self.descriptor)
            if change=='components':d['components'][str(ROOT/'tools/apta_1_1_meter_phase_audit.py')]='0'*64
            else:d['profiles']['extract']['/tmp/unregistered-mc07-import.py']='0'*64
            entry=engine.write(self.base/(change+'.runtime.json'),d)
            with self.assertRaises(ValueError):adapter.check_runtime(entry)
        descriptor=self.descriptor
        profile=descriptor['profiles']['extract']
        self.assertNotIn(str(ROOT/'tools/apta_meter_phase_evidence.py'),profile)
        self.assertIn(str(ROOT/'tools/apta_meter_phase_points.py'),profile)

    def test_06_namespace_input_is_read_only(self):
        target=Path(engine.read(self.bundle)['rows'][0]['job']['path'])
        text=target.read_bytes()
        command=engine.namespace(Path(self.descriptor['parent_snapshot']),[self.python,'-c',
            "import sys;open(sys.argv[1],'w').write('changed')",str(target)],self.descriptor['parent_receipt'])
        at=command.index('--');root=target.parent;command[at:at]=['--ro-bind',str(root),str(root)]
        result=subprocess.run(command,capture_output=True,timeout=60)
        self.assertNotEqual(result.returncode,0);self.assertEqual(target.read_bytes(),text)

    def test_07_parent_supervisor_rejects_timeout_RSS_and_bytes(self):
        for name,code,seconds,rss,limit in (
            ('timeout','import time;time.sleep(10)',.05,262144,67108864),
            ('rss','import time;x=bytearray(60*1024*1024);time.sleep(1)',10,20000,67108864),
            ('bytes',"print('x'*65536)",10,262144,1024)):
            folder=self.base/name;folder.mkdir()
            with self.assertRaises(ValueError):io.supervise([self.python,'-c',code],seconds,rss,limit,[folder],folder/'probe')
            COUNTS['negative_resource_checks']+=1

    def test_08_input_mounts_cannot_shadow_frozen_runtime(self):
        captured=[]
        attempt=self.base/'mount-order-attempt';attempt.mkdir()
        def observe(command,*args,**kwargs):captured.extend(command);return {'status':'PASS'}
        with patch.object(io,'supervise',side_effect=observe):
            adapter.dispatch('extract',self.bundle,attempt,self.runtime,[BASE])
        root=engine.read(self.bundle)['input_roots'][0]
        self.assertLess(captured.index(root),captured.index(self.descriptor['parent_snapshot']))

    def test_09_worker_hard_address_space_ceiling(self):
        code="import sys;sys.path.insert(0,sys.argv[1]);import apta_1_1_meter_phase_audit as a;a.limit_worker();x=bytearray(300*1024*1024)"
        command=engine.namespace(Path(self.descriptor['parent_snapshot']),[self.python,'-B','-X',
            'pycache_prefix=/libapta-no-cache','-c',code,str(ROOT/'tools')],self.descriptor['parent_receipt'])
        p=subprocess.run(command,capture_output=True,text=True,timeout=60)
        self.assertNotEqual(p.returncode,0);self.assertIn('MemoryError',p.stderr)
        COUNTS['negative_resource_checks']+=1


if __name__=='__main__':
    started=time.monotonic();program=unittest.main(exit=False,verbosity=2)
    report=dict(tests=program.result.testsRun,failures=len(program.result.failures),errors=len(program.result.errors),
                skips=len(program.result.skipped),counts=COUNTS,wall_seconds=time.monotonic()-started,
                self_max_rss_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
                scope='NEW_PUBLIC_FIXTURES_ONLY',saved_MC03_cases_opened=False)
    if os.environ.get('APTA_MC07_RESULT'):io.write_new(Path(os.environ['APTA_MC07_RESULT']),report)
    print(json.dumps(report,sort_keys=True));sys.exit(not program.result.wasSuccessful())
