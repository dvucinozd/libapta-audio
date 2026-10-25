#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""MC-07 thin saved-format adapter. No projector/inference or numeric scorer.

Public operational preflight and a separately gated saved-generated audit reuse
the frozen namespace, binding, deadline and process supervision helpers.
"""
import argparse
import json
from pathlib import Path
import resource
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests/unit')]
import apta_1_1_meter_context_runner as saved
import apta_meter_phase_points as geometry

engine = saved.engine
io = engine.io
REGISTRATION = 'ce5ad4a39829d06ccfe89b52330c1baa2d0cc0b080b64f6ea3865af9b625ac44'
REG_PATH = ROOT / 'docs/rust/METER-PHASE-MC06-AUDIT-PROTOCOL.json'
ROLES = ('extract', 'interpret')
ARMS = ('original', 'coupled')
STRATA = ('all', 'reference_complete', 'reference_incomplete')
PROJECTION_KEYS = ('format', 'registration_sha256', 'total_frames', 'first_frame', 'end_frame',
                   'source_beat_count', 'first_observed_ordinal', 'observed_count', 'status', 'fit',
                   'model', 'first_index', 'point_count', 'points', 'meter', 'meter_status',
                   'baseline_meter', 'baseline_meter_status', 'meter_origin', 'context')


def clone(value):
    return engine.strict_json(json.dumps(value, sort_keys=True, allow_nan=False))


def registration():
    return engine.read({'path': str(REG_PATH), 'sha256': REGISTRATION})


def components():
    reg = registration()
    old = engine.read({'path': str(ROOT / reg['historical_component_registry']['path']),
                       'sha256': reg['historical_component_registry']['sha256']})['components']
    result = dict(old)
    for section in ('authority_sha256', 'source_component_sha256'):
        result.update({str(ROOT / p): d for p, d in reg[section].items()})
    result[str(REG_PATH)] = REGISTRATION
    for name in reg['readiness']['allowed_new_source_paths']:
        result[str(ROOT / name)] = io.sha(ROOT / name)
    io.recheck(result)
    return result


def normalize_job(job, projections):
    """Normalize only known saved MC-00 fields; never fit or rescue a record."""
    engine.keys(job, ('total_frames', 'windows', 'known_gaps', *ARMS))
    engine.keys(projections, ARMS)
    engine.integer(job['total_frames'], 1, geometry.MAX_FRAMES)
    if type(job['known_gaps']) is not list or job['known_gaps']:
        raise ValueError('saved gap provenance differs')
    if type(job['windows']) is not list or not 1 <= len(job['windows']) <= 8:
        raise ValueError('saved window capacity differs')
    result = {}
    for arm in ARMS:
        engine.keys(job[arm], ('beat_frames', 'downbeat_ordinals'))
        beats, downs = job[arm]['beat_frames'], job[arm]['downbeat_ordinals']
        if type(beats) is not list or type(downs) is not list or max(len(beats), len(downs)) > 3072:
            raise ValueError('saved event capacity differs')
        for values, high in ((beats, job['total_frames'] - 1), (downs, len(beats) - 1)):
            previous = -1
            for value in values:
                engine.integer(value, previous + 1, high)
                previous = value
        if type(projections[arm]) is not list or len(projections[arm]) != len(job['windows']):
            raise ValueError('saved projection/window count differs')
        normalized = []
        for window, projection in zip(job['windows'], projections[arm]):
            engine.keys(window, ('first_frame', 'end_frame'))
            engine.keys(projection, PROJECTION_KEYS)
            if (projection['format'] != 'libapta-meter-context-projection-1'
                    or projection['registration_sha256'] != saved.PROJECTION_PROTOCOL
                    or any(projection[k] != window[k] for k in window)
                    or any(type(projection[k]) is not int for k in ('total_frames','first_frame','end_frame'))
                    or projection['total_frames'] != job['total_frames']):
                raise ValueError('saved projection identity or geometry differs')
            engine.integer(projection['source_beat_count'], len(beats), len(beats))
            if type(projection['points']) is not list:
                raise ValueError('invalid saved points')
            engine.integer(projection['point_count'], len(projection['points']), len(projection['points']))
            status = projection['status']
            if status not in ('ACCEPTED', 'INSUFFICIENT_EVENTS', 'UNSUPPORTED_TEMPO',
                              'SOURCE_ANCHOR_OUT_OF_DOMAIN', 'NO_OWNED_POINTS'):
                raise ValueError('unknown saved projection status')
            meter = projection['meter']
            if status != 'ACCEPTED' and (projection['points'] or meter is not None):
                raise ValueError('unsupported projection carries owned evidence')
            if status == 'ACCEPTED' and not projection['points']:
                raise ValueError('accepted projection has no grid')
            if projection['baseline_meter'] is not None and (
                    meter != projection['baseline_meter'] or projection['meter_origin'] != 'P1'
                    or projection['meter_status'] != projection['baseline_meter_status']
                    or projection['context'] is not None):
                raise ValueError('saved P1 publication changed')
            expected_origin = None if meter is None else 'P1' if projection['baseline_meter'] is not None else 'CONTEXT'
            if projection['meter_origin'] != expected_origin:
                raise ValueError('saved meter origin differs')
            if meter is not None:
                engine.keys(meter, ('kind', *geometry.METER_KEYS, 'witness_ordinals'))
                if meter['kind'] != 'estimated':
                    raise ValueError('unsupported saved meter kind')
                meter = {k: meter[k] for k in geometry.METER_KEYS}
            normalized.append(dict(window, model=clone(projection['model']),
                                   points=clone(projection['points']), meter=meter))
        # Validates exact full model/grid/meter, absence, ownership and source cap.
        geometry.extract_source(normalized, total_frames=job['total_frames'],
                                sample_rate=48000, known_gaps=job['known_gaps'])
        result[arm] = normalized
    return result


def extract_bound(index, job_entry, projection_entry, proof_entry):
    engine.integer(index, 0, 99)
    proof = engine.read(proof_entry)
    if proof != {'index': index, 'job_sha256': job_entry['sha256'],
                 'projection_sha256': projection_entry['sha256']}:
        raise ValueError('saved verification proof does not cover job/projection')
    job, projections = engine.read(job_entry), engine.read(projection_entry)
    windows = normalize_job(job, projections)
    return {'index': index, 'job': job_entry, 'projection': projection_entry, 'proof': proof_entry,
            'total_frames': job['total_frames'], 'windows': windows,
            'statuses': {a: [p['status'] for p in projections[a]] for a in ARMS},
            'phases': {a: geometry.extract_source(windows[a], total_frames=job['total_frames'],
                        sample_rate=48000, known_gaps=[]) for a in ARMS}}


def interpret(extracted, truth, old):
    """Evaluate all K points, checking exact original context-cell parity."""
    import apta_meter_phase_evidence as diagnostic
    import apta_context_matched as canonical
    results = {}
    for arm in ARMS:
        cells = old['cells'][arm + '_context']
        rows = diagnostic.evaluate_source(extracted['windows'][arm], truth,
                                          total_frames=extracted['total_frames'],
                                          sample_rate=48000, known_gaps=[])
        if len(cells) != len(rows) or [r['phase'] for r in rows] != extracted['phases'][arm]:
            raise ValueError('frozen extraction or saved cell count differs')
        for window, row, cell in zip(extracted['windows'][arm], rows, cells):
            engine.keys(cell, ('normalized', 'grid_score', 'meter_score', 'points', 'meter'))
            p = row['published']
            if (cell['points'] != window['points'] or cell['meter'] != window['meter']
                    or cell['meter_score'] != p['score']
                    or cell['normalized'] != canonical.make_cell(cell['grid_score'], p)
                    or not p['downbeat_supported'] <= row['E_supported'] <= p['meter_supported']):
                raise ValueError('original saved M/D publication parity differs')
        results[arm] = rows
    return results


def compact(results, old):
    """Drop point arrays before cohort aggregation; original normalized cells stay intact."""
    return {arm: [dict(cell=clone(cell['normalized']), E=r['E_supported'],
                       phase_correct=r['all_correct'], K=r['phase_point_count'],
                       points=r['supported_point_count'], correct_points=r['correct_point_count'])
                  for r, cell in zip(results[arm], old['cells'][arm + '_context'])]
            for arm in ARMS}


def _compact_record(value):
    import apta_context_matched as canonical
    engine.keys(value, ('cell','E','phase_correct','K','points','correct_points'))
    canonical._cell(value['cell'])
    engine.integer(value['E'],0,1)
    for k in ('K','points','correct_points'):engine.integer(value[k],0,3072)
    if (not value['cell']['D'] <= value['E'] <= value['cell']['M']
            or not value['correct_points'] <= value['points'] <= value['K']
            or value['E'] != int(value['points']>0)
            or value['E']==0 and value['phase_correct'] is not None
            or value['E']==1 and (type(value['phase_correct']) is not int
                                  or value['phase_correct'] != int(value['points']==value['correct_points']))):
        raise ValueError('inconsistent E point/window evidence')


def e_ledger(left, right, stratum, level):
    """Finite window partitions for E only; never masquerade as a P3 family."""
    if len(left) != len(right) or stratum not in STRATA or level not in ('source', 'group', 'cohort'):
        raise ValueError('E ledger alignment differs')
    counts = {k: 0 for k in ('windows', 'reference_complete_windows', 'reference_incomplete_windows',
              'both_supported', 'left_only_supported', 'right_only_supported', 'neither_supported',
              'correct_both', 'fixes_on_both', 'breaks_on_both', 'incorrect_both',
              'left_correct_on_both', 'right_correct_on_both', 'left_only_correct', 'right_only_correct')}
    for a, b in zip(left, right):
        _compact_record(a);_compact_record(b)
        if any(a['cell'][k] != b['cell'][k] for k in ('first_frame', 'end_frame', 'reference_complete')):
            raise ValueError('E geometry or reference stratum differs')
        for v in (a, b):
            engine.integer(v['E'], 0, 1)
            if (v['E'] == 0 and v['phase_correct'] is not None
                    or v['E'] == 1 and type(v['phase_correct']) is not int):
                raise ValueError('unsupported E correctness is not null')
            if v['E']: engine.integer(v['phase_correct'], 0, 1)
        full = a['cell']['reference_complete']
        if stratum != 'all' and full != int(stratum == 'reference_complete'):
            continue
        counts['windows'] += 1
        counts['reference_complete_windows'] += full
        counts['reference_incomplete_windows'] += 1 - full
        ls, rs = a['E'], b['E']
        lc, rc = a['phase_correct'] == 1, b['phase_correct'] == 1
        support = 'both_supported' if ls and rs else 'left_only_supported' if ls else 'right_only_supported' if rs else 'neither_supported'
        counts[support] += 1
        if ls and rs:
            key = 'correct_both' if lc and rc else 'fixes_on_both' if rc else 'breaks_on_both' if lc else 'incorrect_both'
            counts[key] += 1
            counts['left_correct_on_both'] += lc
            counts['right_correct_on_both'] += rc
        elif ls: counts['left_only_correct'] += lc
        elif rs: counts['right_only_correct'] += rc
    shared = counts['both_supported']
    return {'format': 'libapta-meter-phase-E-ledger-1', 'registration_sha256': REGISTRATION,
            'domain': 'E', 'comparison': 'candidate_vs_original_context', 'stratum': stratum,
            'level': level, 'counts': counts,
            'left_conditional_accuracy': {'numerator': counts['left_correct_on_both'], 'denominator': shared} if shared else None,
            'right_conditional_accuracy': {'numerator': counts['right_correct_on_both'], 'denominator': shared} if shared else None}


def summarize(rows, groups):
    """M/D use unchanged canonical ledgers; E and point counts remain separate."""
    import apta_context_matched as canonical
    if not rows or len(rows) != len(groups) or len(rows) > 100 or any(type(g) is not str or not g for g in groups):
        raise ValueError('source/group identity differs')
    for row in rows:
        engine.keys(row,ARMS)
        if any(type(row[a]) is not list for a in ARMS) or not 1<=len(row['original'])==len(row['coupled'])<=8:
            raise ValueError('source window count differs')
    if sum(len(r['original']) for r in rows)>600:raise ValueError('cohort window cap exceeded')
    ledgers, totals, transitions = [], [], []
    levels = [('source', str(i), [i]) for i in range(len(rows))]
    levels += [('group', g, [i for i, v in enumerate(groups) if v == g]) for g in sorted(set(groups))]
    levels += [('cohort', 'all', list(range(len(rows))))]
    for level, name, indices in levels:
        arms = {a: [v for i in indices for v in rows[i][a]] for a in ARMS}
        for stratum in STRATA:
            for domain, family in (('M', 'meter'), ('D', 'downbeat')):
                ledger = canonical.make_ledger([v['cell'] for v in arms['original']],
                    [v['cell'] for v in arms['coupled']], 'candidate_vs_original_context', family, stratum, level)
                ledgers.append(dict(group=name, domain=domain, ledger=ledger))
            ledgers.append(dict(group=name, domain='E', ledger=e_ledger(arms['original'], arms['coupled'], stratum, level)))
            selected = {a: [v for v in arms[a] if stratum == 'all' or
                        v['cell']['reference_complete'] == int(stratum == 'reference_complete')] for a in ARMS}
            for arm in ARMS:
                values = selected[arm]
                counts = {k: 0 for k in ('windows','reference_complete_windows','reference_incomplete_windows',
                          'M_windows','D_windows','E_windows','K_points','E_points','correct_points')}
                patterns = {}
                for v in values:
                    if not v['cell']['D'] <= v['E'] <= v['cell']['M']:
                        raise ValueError('M/D/E inclusion differs')
                    for k in ('K', 'points', 'correct_points'):engine.integer(v[k], 0, 3072)
                    if not v['correct_points'] <= v['points'] <= v['K'] or bool(v['points']) != bool(v['E']):
                        raise ValueError('point/window denominator differs')
                    full = v['cell']['reference_complete']
                    counts['windows'] += 1
                    counts['reference_complete_windows'] += full
                    counts['reference_incomplete_windows'] += 1-full
                    for d in ('M','D'):counts[d+'_windows'] += v['cell'][d]
                    counts['E_windows'] += v['E']
                    counts['K_points'] += v['K'];counts['E_points'] += v['points']
                    counts['correct_points'] += v['correct_points']
                    pattern = f"{v['cell']['M']}{v['cell']['D']}{v['E']}"
                    patterns[pattern] = patterns.get(pattern,0)+1
                totals.append(dict(level=level, group=name, stratum=stratum, arm=arm,
                    counts=counts, support_patterns=patterns,
                    point_accuracy={'numerator':counts['correct_points'],'denominator':counts['E_points']}
                    if counts['E_points'] else None))
            cross = {}
            for a,b in zip(selected['original'],selected['coupled']):
                key = f"D{a['cell']['D']}{b['cell']['D']}_E{a['E']}{b['E']}_C{a['phase_correct']}{b['phase_correct']}"
                cross[key] = cross.get(key,0)+1
            transitions.append(dict(level=level, group=name, stratum=stratum, counts=cross,
                old_D_loss_windows=sum(a['cell']['D']==1 and b['cell']['D']==0
                                      for a,b in zip(selected['original'],selected['coupled']))))
    return dict(ledgers=ledgers, subtotals=totals, transitions=transitions,
                sources=len(rows), groups=len(set(groups)),
                windows=sum(len(r['original']) for r in rows))


def warm(role):
    if role not in ROLES:raise ValueError('unknown diagnostic role')
    engine.frozen()  # Bind reused runtime helper code, not an experiment invocation.
    if role == 'interpret':
        import apta_meter_phase_evidence
        import apta_context_matched
    elif any(n in sys.modules for n in ('apta_meter_phase_evidence','apta_context_matched')):
        raise ValueError('evaluation imports in extract process')
    json.dumps({'warm': 0}, sort_keys=True)


def runtime_membership(profile, source, snapshot):
    parent = {v['path']:v['sha256'] for v in engine.strict_json((snapshot/'inventory.json').read_bytes()) if v['kind']=='file'}
    for path,digest in profile.items():
        expected = source.get(path) if Path(path).is_relative_to(ROOT) else parent.get(path)
        if expected != digest:raise ValueError('runtime import outside bound source/parent inventory')


def prepare_runtime(snapshot, destination):
    snapshot, destination = engine.safe(snapshot), engine.safe(destination)
    io.disjoint_output(destination, [ROOT,snapshot])
    parent = engine.frozen().verify(snapshot)
    if io.sha(snapshot/'snapshot.json') != registration()['parent_snapshot_sha256']:
        raise ValueError('MC-06 parent identity differs')
    destination.mkdir(parents=True)
    source, profiles = components(), {}
    for role in ROLES:
        output = destination/(role+'.profile.json')
        command = engine.namespace(snapshot, [parent['interpreter'],'-B','-X','pycache_prefix=/libapta-no-cache',
                                    __file__,'_profile',role,str(output)],parent)
        io.supervise(command,1800,262144,67108864,[destination],destination/(role+'.profile'))
        profiles[role] = engine.read(engine.leaf(output))['bindings']
        runtime_membership(profiles[role],source,snapshot)
    engine.frozen().verify(snapshot)
    if source != components():raise ValueError('preflight source changed')
    return engine.write(destination/'runtime.json',dict(format='libapta-meter-phase-runtime-1',
        registration_sha256=REGISTRATION,parent_snapshot=str(snapshot),parent_receipt=parent,
        components=source,profiles=profiles))


def check_runtime(entry, role=None):
    descriptor = engine.read(entry)
    engine.keys(descriptor,('format','registration_sha256','parent_snapshot','parent_receipt','components','profiles'))
    snapshot = engine.safe(descriptor['parent_snapshot'])
    if (descriptor['format']!='libapta-meter-phase-runtime-1' or descriptor['registration_sha256']!=REGISTRATION
            or io.sha(snapshot/'snapshot.json')!=registration()['parent_snapshot_sha256']
            or descriptor['parent_receipt']!=engine.strict_json((snapshot/'snapshot.json').read_bytes())
            or descriptor['components']!=components() or set(descriptor['profiles'])!=set(ROLES)):
        raise ValueError('new runtime identity differs')
    for profile in descriptor['profiles'].values():runtime_membership(profile,descriptor['components'],snapshot)
    if role is not None:
        io.runtime_recheck(descriptor['profiles'][role])
    return descriptor


def validate_bundle(bundle):
    engine.keys(bundle,('format','kind','input_roots','rows','labels','bindings','launch'))
    if (bundle['format']!='libapta-meter-phase-inputs-1'
            or bundle['kind'] not in ('PUBLIC_PREFLIGHT','SAVED_C3_GENERATED')
            or type(bundle['rows']) is not list or not 1<=len(bundle['rows'])<=100
            or type(bundle['bindings']) is not dict
            or type(bundle['input_roots']) is not list or not bundle['input_roots']):
        raise ValueError('invalid diagnostic input scope')
    roots=[engine.safe(p) for p in bundle['input_roots']]
    def bound(entry):
        engine.keys(entry,('path','sha256'))
        path=engine.safe(entry['path'])
        if bundle['bindings'].get(str(path))!=entry['sha256'] or not any(path.is_relative_to(p) for p in roots):
            raise ValueError('input leaf outside registered scope')
    bound(bundle['labels'])
    ids=[]
    for i,row in enumerate(bundle['rows']):
        engine.keys(row,('index','id','job','projection','proof','evaluation'))
        engine.integer(row['index'],i,i)
        if type(row['id']) is not str or not row['id'] or row['id'] in ids:
            raise ValueError('source identity/order differs')
        ids.append(row['id'])
        for k in ('job','projection','proof','evaluation'):bound(row[k])
    for name,digest in bundle['bindings'].items():
        bound(dict(path=name,sha256=digest))
    if bundle['kind']=='PUBLIC_PREFLIGHT' and bundle['launch'] is not None:
        raise ValueError('public input carries saved launch token')


def require_launch(bundle, runtime_entry, attempt):
    if bundle['kind']=='PUBLIC_PREFLIGHT':return
    launch=bundle['launch']
    engine.keys(launch,('registration_sha256','readiness','runtime','source_revision','attempt_path'))
    ready=engine.read(launch['readiness'])
    if (launch['registration_sha256']!=REGISTRATION or launch['runtime']!=runtime_entry
            or launch['attempt_path']!=str(attempt) or str(attempt)!=registration()['new_attempt_path']
            or ready['status']!='MC07_PUBLIC_OPERATIONAL_PREFLIGHT_PASS'
            or ready['runtime']!=runtime_entry or ready['components']!=components()):
        raise ValueError('saved audit lacks exact readiness/launch binding')
    io.check_source(ROOT,launch['source_revision'])
    reg=registration();inventory=engine.read(reg['input_inventory'])
    expected=dict(inventory['mc03_attempt_files']);expected.update(inventory['c3_input_bindings'])
    for key in ('scope','manifest','report'):
        entry=reg['historical_inputs'][key];expected[entry['path']]=entry['sha256']
    if bundle['bindings']!=expected:
        raise ValueError('saved launch input inventory differs')
    scope=engine.read(reg['historical_inputs']['scope'])
    old_manifest=engine.read(reg['historical_inputs']['manifest'])
    if ([r['id'] for r in bundle['rows']]!=[r['id'] for r in scope['rows']]
            or bundle['labels']!=scope['labels'] or len(bundle['rows'])!=100):
        raise ValueError('saved launch source order/labels differ')
    completions={k:engine.read(v) for k,v in reg['historical_inputs']['completions'].items()}
    for i,row in enumerate(bundle['rows']):
        if any(row[k]!=entry for k,entry in (
                ('job',old_manifest['jobs'][i]),('projection',completions['project']['records'][i]),
                ('proof',completions['verify']['records'][i]),('evaluation',completions['evaluate']['records'][i]))):
            raise ValueError('saved launch per-source binding differs')


def limit_worker():
    # The inherited supervisor samples process groups; bwrap can create a new
    # session. A hard per-worker AS ceiling also bounds transient allocations.
    limit=262144*1024
    resource.setrlimit(resource.RLIMIT_AS,(limit,limit))
    return limit


def _worker(role, bundle_entry, attempt, runtime_entry):
    address_space_limit=limit_worker()
    warm(role)
    runtime = check_runtime(runtime_entry,role)
    bundle = engine.read(bundle_entry)
    validate_bundle(bundle)
    require_launch(bundle,runtime_entry,attempt)
    if (attempt/'INVALID.json').exists():raise ValueError('closed invalid diagnostic')
    # A call-time guard, not a private filesystem namespace. Frozen runtime/source
    # and bound input roots are additionally mounted read-only by dispatch().
    forbidden = {bundle['labels']['path'], *(r['evaluation']['path'] for r in bundle['rows'])}
    if role == 'extract':
        def guard(event,args):
            if event=='open' and isinstance(args[0],str) and str(Path(args[0]).absolute()) in forbidden:
                raise ValueError('reference/saved correctness opened before extraction freeze')
        sys.addaudithook(guard)
    records, compact_rows, groups = [], [], []
    if role == 'interpret':
        completion = engine.read(engine.leaf(attempt/'extract.complete.json'))
        if (completion['bundle']!=bundle_entry or completion['runtime']!=runtime_entry
                or completion['role']!='extract' or completion['status']!='COMPLETE'
                or len(completion['records'])!=len(bundle['rows'])
                or any(v['path']!=str(attempt/'extract'/(str(i)+'.json')) for i,v in enumerate(completion['records']))):
            raise ValueError('extraction completion identity differs')
        for entry in completion['records']:io.bind(entry['path'],entry['sha256'],{})
        labels = engine.read(bundle['labels'])
        if [v['track'] for v in labels['tracks']] != [r['id'] for r in bundle['rows']]:
            raise ValueError('reference source/order differs')
    for i,row in enumerate(bundle['rows']):
        with engine.deadline():
            extracted = extract_bound(i,row['job'],row['projection'],row['proof'])
            if role == 'extract':result = extracted
            else:
                if extracted != engine.read(completion['records'][i]):
                    raise ValueError('frozen K or input identity changed')
                label = labels['tracks'][i]
                if label['requested_frames'] != extracted['total_frames']:
                    raise ValueError('reference source domain differs')
                truth = {k:label[k] for k in ('beat_frames','downbeat_frames','meter_numerator','meter_denominator')}
                if any(type(f) is not int or not 0<=f<extracted['total_frames'] for k in ('beat_frames','downbeat_frames') for f in truth[k]):
                    raise ValueError('reference frame outside source')
                old = engine.read(row['evaluation'])
                values = interpret(extracted,truth,old)
                compact_rows.append(compact(values,old));groups.append(label['genre'])
                equal = []
                for a,b in zip(values['original'],values['coupled']):
                    same_grid = a['phase']['model'] is not None and all(
                        a['phase'][k]==b['phase'][k] for k in ('first_frame','end_frame','model','grid_points'))
                    equal.append({'same_grid':same_grid,'same_K':a['phase']['phase_points']==b['phase']['phase_points'] if same_grid else None})
                result = dict(index=i,arms=values,grid_comparisons=equal,compact=compact_rows[-1],group=label['genre'])
            records.append(engine.write(attempt/role/(str(i)+'.json'),result,attempt))
            engine.self_rss()
    summary = engine.write(attempt/'phase-report.json',summarize(compact_rows,groups),attempt) if role=='interpret' else None
    io.runtime_recheck(runtime['profiles'][role])
    io.recheck({p:d for p,d in bundle['bindings'].items() if role!='extract' or p not in forbidden})
    engine.write(attempt/(role+'.complete.json'),dict(role=role,status='COMPLETE',bundle=bundle_entry,
        runtime=runtime_entry,records=records,summary=summary,self_max_rss_kib=engine.self_rss(),
        address_space_limit_bytes=address_space_limit),attempt)


def dispatch(role, bundle_entry, attempt, runtime_entry, accounting_roots=()):
    if role not in ROLES:raise ValueError('unknown phase')
    attempt = engine.safe(attempt)
    if (attempt/'INVALID.json').exists():raise ValueError('closed invalid diagnostic')
    try:
        runtime = check_runtime(runtime_entry)
        bundle = engine.read(bundle_entry)
        validate_bundle(bundle)
        require_launch(bundle,runtime_entry,attempt)
        command = engine.namespace(Path(runtime['parent_snapshot']),
            [runtime['parent_receipt']['interpreter'],'-B','-X','pycache_prefix=/libapta-no-cache',
             __file__,'_worker',role,bundle_entry['path'],bundle_entry['sha256'],str(attempt),
             runtime_entry['path'],runtime_entry['sha256']],runtime['parent_receipt'])
        # Input parents may overlap runtime paths. Mount them first so the
        # immutable runtime/source overlays retain priority over host inputs.
        at=command.index('--ro-bind')
        # This public bundle's entire input directory is immutable to the worker.
        roots=[engine.safe(p) for p in bundle['input_roots']]
        if not any(Path(bundle_entry['path']).is_relative_to(p) for p in roots):raise ValueError('bundle outside input roots')
        for root in roots:
            if attempt.is_relative_to(root) or root.is_relative_to(attempt):raise ValueError('output/input overlap')
            command[at:at]=['--ro-bind',str(root),str(root)]
        return io.supervise(command,1800,262144,67108864,[attempt,*accounting_roots],attempt/(role+'.supervised'))
    except BaseException as error:
        if not (attempt/'INVALID.json').exists():
            engine.write(attempt/'INVALID.json',dict(status='INVALID',role=role,error=str(error)),attempt)
        raise


def run_public(bundle_entry, attempt, runtime_entry, accounting_roots=()):
    bundle=engine.read(bundle_entry)
    if bundle['kind']!='PUBLIC_PREFLIGHT':raise ValueError('public preflight cannot open saved/real cohort')
    validate_bundle(bundle)
    io.disjoint_output(attempt,[ROOT,*map(Path,bundle['input_roots']),Path(runtime_entry['path']).parent])
    io.recheck(bundle['bindings'])
    Path(attempt).mkdir(parents=True)
    for role in ROLES:dispatch(role,bundle_entry,Path(attempt),runtime_entry,accounting_roots)
    return engine.read(engine.leaf(Path(attempt)/'phase-report.json'))


def prepare_saved(destination, readiness_entry, runtime_entry):
    """Later clean-source launch only. Never called by public preflight."""
    ready=engine.read(readiness_entry)
    if (ready['status']!='MC07_PUBLIC_OPERATIONAL_PREFLIGHT_PASS'
            or ready['components']!=components() or ready['runtime']!=runtime_entry):
        raise ValueError('operational readiness does not bind this implementation')
    revision=io.git(ROOT,'rev-parse','HEAD');io.check_source(ROOT,revision)
    reg=registration()
    runtime=check_runtime(runtime_entry)
    engine.frozen().verify(Path(runtime['parent_snapshot']))
    inventory=engine.read(reg['input_inventory'])
    io.recheck(inventory['mc03_attempt_files']);io.recheck(inventory['c3_input_bindings'])
    old=reg['historical_inputs'];attempt=Path(inventory['mc03_attempt_root'])
    if (attempt/'INVALID.json').exists():raise ValueError('old attempt is invalid')
    manifest=engine.read(old['manifest'])
    scope=engine.read(old['scope'])
    if (manifest['source_revision']!=old['measurement_source_revision']
            or manifest['scope_sha256']!=old['scope']['sha256']
            or scope['kind']!='C3_FIXED_GENERATED' or scope['bindings']!=inventory['c3_input_bindings']):
        raise ValueError('saved source/scope closure differs')
    completed={role:engine.completion(attempt,role,manifest) for role in ('project','verify','evaluate')}
    for role in completed:io.bind(old['completions'][role]['path'],old['completions'][role]['sha256'],{})
    # Reuses the saved job/scoped event/proof relation; it never runs a projector.
    saved._scope_jobs_after_verification(attempt,manifest,old['scope'])
    engine.validate_scope(scope)
    inputs=dict(inventory['mc03_attempt_files']);inputs.update(inventory['c3_input_bindings'])
    for entry in (old['scope'],old['manifest'],old['report']):io.bind(entry['path'],entry['sha256'],inputs)
    destination=engine.safe(destination)
    parents=sorted({Path(p).parent for p in inputs},key=lambda p:len(p.parts))
    roots=[]
    for p in parents:
        if not any(p.is_relative_to(a) for a in roots):roots.append(p)
    io.disjoint_output(destination,[ROOT,*roots,Path(runtime_entry['path']).parent])
    destination.mkdir(parents=True)
    rows=[dict(index=i,id=row['id'],job=manifest['jobs'][i],projection=completed['project']['records'][i],
               proof=completed['verify']['records'][i],evaluation=completed['evaluate']['records'][i])
          for i,row in enumerate(scope['rows'])]
    bundle=dict(format='libapta-meter-phase-inputs-1',kind='SAVED_C3_GENERATED',
        input_roots=[str(p) for p in [*roots,destination]],rows=rows,labels=scope['labels'],bindings=inputs,
        launch=dict(registration_sha256=REGISTRATION,readiness=readiness_entry,runtime=runtime_entry,
                    source_revision=revision,attempt_path=reg['new_attempt_path']))
    validate_bundle(bundle)
    io.disjoint_output(reg['new_attempt_path'],[ROOT,*map(Path,bundle['input_roots']),Path(runtime_entry['path']).parent])
    return engine.write(destination/'bundle.json',bundle)


def _run_saved(bundle_entry, runtime_entry, accounting_roots=()):
    """One registered audit after clean pushed readiness; not a preflight action."""
    bundle=engine.read(bundle_entry)
    if bundle['kind']!='SAVED_C3_GENERATED':raise ValueError('registered saved-generated bundle required')
    validate_bundle(bundle)
    attempt=Path(registration()['new_attempt_path'])
    require_launch(bundle,runtime_entry,attempt)
    io.disjoint_output(attempt,[ROOT,*map(Path,bundle['input_roots']),Path(runtime_entry['path']).parent])
    attempt.mkdir(parents=True)
    for role in ROLES:dispatch(role,bundle_entry,attempt,runtime_entry,accounting_roots)
    report=engine.read(engine.leaf(attempt/'phase-report.json'))
    original=engine.read(registration()['historical_inputs']['report'])
    if original['pair_screen']!='PAIR_REJECTED_ON_GENERATED_DIAGNOSTICS':
        raise ValueError('original MC-03 verdict differs')
    old_ledgers={(v['group'],v['ledger']['family'],v['ledger']['stratum'],v['ledger']['level']):v['ledger']
        for v in original['ledgers'] if v['ledger']['comparison']=='candidate_vs_original_context'
        and v['ledger']['family'] in ('meter','downbeat')}
    checks=0
    for v in report['ledgers']:
        if v['domain']=='E':continue
        p=v['ledger']
        if p!=old_ledgers[(v['group'],p['family'],p['stratum'],p['level'])]:
            raise ValueError('saved canonical M/D ledger differs')
        checks+=1
    if (report['sources'],report['groups'],report['windows'],len(report['ledgers']),checks)!=(100,4,600,945,630):
        raise ValueError('incomplete registered cohort/ledger count')
    io.recheck(bundle['bindings'])
    runtime=check_runtime(runtime_entry)
    engine.frozen().verify(Path(runtime['parent_snapshot']))
    io.check_source(ROOT,bundle['launch']['source_revision'])
    close=dict(status='SAVED_GENERATED_INTERPRETATION_COMPLETE',report=engine.leaf(attempt/'phase-report.json'),
        unchanged_canonical_ledgers=checks,preserved_MC03='PAIR_REJECTED_ON_GENERATED_DIAGNOSTICS',
        candidate_or_acceptance_pass=False)
    engine.write(attempt/'close.json',close,attempt)
    return close


def run_saved(bundle_entry, runtime_entry, accounting_roots=()):
    attempt=engine.safe(registration()['new_attempt_path'])
    if attempt.exists():raise ValueError('saved attempt already exists; no retry/resume')
    try:return _run_saved(bundle_entry,runtime_entry,accounting_roots)
    except BaseException as error:
        if attempt.is_dir() and not (attempt/'INVALID.json').exists():
            engine.write(attempt/'INVALID.json',dict(status='INVALID',role='close',error=str(error)),attempt)
        raise


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True)
    profile=sub.add_parser('_profile');profile.add_argument('role',choices=ROLES);profile.add_argument('output',type=Path)
    worker=sub.add_parser('_worker');worker.add_argument('role',choices=ROLES)
    for name in ('bundle_path','bundle_sha','attempt','runtime_path','runtime_sha'):worker.add_argument(name)
    args=parser.parse_args()
    if args.action=='_profile':
        warm(args.role);io.git(ROOT,'rev-parse','HEAD');components()
        bindings={};io.runtime_snapshot(bindings)
        engine.write(args.output,dict(role=args.role,bindings=bindings))
    else:_worker(args.role,dict(path=args.bundle_path,sha256=args.bundle_sha),engine.safe(args.attempt),
                 dict(path=args.runtime_path,sha256=args.runtime_sha))


if __name__=='__main__':main()
