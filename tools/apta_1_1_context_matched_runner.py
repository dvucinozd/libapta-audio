#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated-only P5 phase runner. C3 execution requires a separate P6 proposal."""
import argparse
from contextlib import contextmanager
import hashlib
import importlib
import json
import math
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs/rust'
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests/unit')]
import apta_dense_runner_support as io

PROTOCOL = 'c8b17d5bda059ec96260180485f18ce59712db47c9d4037c9180e1917480c6e8'
PARENT_RUNTIME = '563f613b905fc95ef349101b8d83752ebf14e91dc016d8ed9b3e285e045a6f58'
ROLES = ('project', 'verify', 'evaluate')
FORBIDDEN = ('numpy', 'scipy', 'mir_eval', 'apta_1_1_neural_reference', 'apta_1_1_event_representation',
             'apta_reference_support', 'apta_context_matched', 'apta_1_1_context_matched_evaluation',
             'apta_1_1_dense_decoder', 'apta_coupled_time')
PHASE_KEYS = ('format', 'role', 'status', 'protocol_sha256', 'runtime_sha256', 'scope_sha256',
              'source_revision', 'manifest_sha256', 'upstream', 'records', 'summary', 'source_count',
              'window_count', 'projection_count', 'reference_labels_loaded', 'self_max_rss_kib')


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON key')
            result[key] = value
        return result
    def floating(value):
        value = float(value)
        if not math.isfinite(value):
            raise ValueError('nonfinite JSON')
        return value
    def invalid(value):
        raise ValueError('nonfinite JSON')
    return json.loads(data, object_pairs_hook=pairs, parse_float=floating, parse_constant=invalid)


def keys(value, expected):
    if type(value) is not dict or set(value) != set(expected):
        raise ValueError('closed record fields differ')


def integer(value, lo, hi):
    if type(value) is not int or not lo <= value <= hi:
        raise ValueError('exact integer required')


def safe(path):
    p = Path(path)
    if not p.is_absolute() or p != p.resolve() or p.is_symlink():
        raise ValueError('absolute nonsymlink path required')
    return p


def leaf(path):
    p = safe(path)
    return {'path': str(p), 'sha256': io.sha(p)}


def read(entry):
    keys(entry, ('path', 'sha256'))
    path = safe(entry['path'])
    if type(entry['sha256']) is not str or len(entry['sha256']) != 64:
        raise ValueError('invalid digest')
    io.bind(path, entry['sha256'], {})
    value = strict_json(path.read_bytes())
    io.bind(path, entry['sha256'], {})
    return value


def write(path, value, attempt=None):
    if attempt is not None and io.tree_bytes([attempt]) + io.json_bytes(value) > 67108864:
        raise ValueError('whole-attempt output cap exceeded')
    io.write_new(safe(path), value)
    return leaf(path)


def components():
    reg = strict_json((DOCS / 'CONTEXT-ANCHOR-MATCHED-EVALUATION-PROTOCOL.json').read_bytes())
    if io.sha(DOCS / 'CONTEXT-ANCHOR-MATCHED-EVALUATION-PROTOCOL.json') != PROTOCOL:
        raise ValueError('P3 registration changed')
    result = {str(ROOT / name): digest for name, digest in reg['binding_sha256'].items()}
    p0 = strict_json((DOCS / 'CONTEXT-ANCHOR-PROJECTION-PROTOCOL.json').read_bytes())
    for section in ('reviewed_source_sha256', 'frozen_c3_component_sha256'):
        result.update({str(ROOT / name): digest for name, digest in p0[section].items()})
    p4_path = DOCS / 'CONTEXT-ANCHOR-MATCHED-EVALUATION-P4-20261024.json'
    if io.sha(p4_path) != '07aa1b0296bb37c7d4d38613d5788e9a1019b2b59a3faf36470864384f7694fc':
        raise ValueError('P4 authority changed')
    p4 = strict_json(p4_path.read_bytes())
    result.update({str(ROOT / name): digest for name, digest in p4['component_sha256'].items()})
    for name in ('tools/apta_1_1_context_matched_runner.py', 'tools/apta_1_1_context_matched_evaluation.py',
                 'tests/unit/apta_1_1_context_matched_runner.py', 'tests/unit/apta_context_projection_oracle.py',
                 'tests/unit/apta_coupled_time_v2_runtime.py',
                 'docs/rust/CONTEXT-ANCHOR-MATCHED-EVALUATION-P5-PROTOCOL.md'):
        result[str(ROOT / name)] = io.sha(ROOT / name)
    io.recheck(result)
    return result


def frozen():
    return importlib.import_module('apta_coupled_time_v2_runtime')


def namespace(snapshot, arguments, receipt):
    command = frozen().sandbox_command(snapshot, arguments, receipt)
    at = command.index('--')
    command[at:at] = ['--ro-bind', str(ROOT), str(ROOT)]
    return command


def profile_imports(role):
    if role == 'project':
        importlib.import_module('apta_context_projection')
    elif role == 'verify':
        importlib.import_module('apta_context_projection_oracle')
    elif role == 'evaluate':
        importlib.import_module('apta_1_1_context_matched_evaluation').warm()
    else:
        raise ValueError('unknown phase')
    if role != 'evaluate' and any(name.startswith(FORBIDDEN) for name in sys.modules):
        raise ValueError('scoring/reference import before evaluation')


def runtime_profile(role, output):
    io.git(ROOT, 'rev-parse', 'HEAD')
    profile_imports(role)
    bindings = {}
    io.runtime_snapshot(bindings)
    write(output, {'role': role, 'bindings': bindings})


def prepare_runtime(snapshot, destination):
    snapshot, destination = safe(snapshot), safe(destination)
    io.external(destination, ROOT)
    if io.sha(snapshot / 'snapshot.json') != PARENT_RUNTIME:
        raise ValueError('P3-bound parent generated runtime required')
    parent = frozen().verify(snapshot)
    destination.mkdir(parents=True, exist_ok=False)
    source = components()
    profiles = {}
    for role in ROLES:
        output = destination / (role + '.runtime.json')
        command = namespace(snapshot, [parent['interpreter'], '-B', '-X', 'pycache_prefix=/libapta-no-cache',
            Path(__file__), '_profile', role, output], parent)
        io.supervise(command, 1800, 262144, 67108864, [destination], destination / (role + '.profile'))
        profiles[role] = read(leaf(output))['bindings']
    frozen().verify(snapshot)
    if source != components():
        raise ValueError('source changed during runtime preparation')
    descriptor = {'format': 'libapta-context-matched-runtime-1', 'parent_snapshot': str(snapshot),
        'parent_sha256': PARENT_RUNTIME, 'parent_receipt': parent, 'protocol_sha256': PROTOCOL,
        'components': source, 'profiles': profiles, 'generated_only': True}
    write(destination / 'runtime.json', descriptor)
    return leaf(destination / 'runtime.json')


def check_runtime(runtime, role=None):
    r = read(runtime)
    keys(r, ('format', 'parent_snapshot', 'parent_sha256', 'parent_receipt', 'protocol_sha256',
             'components', 'profiles', 'generated_only'))
    if (r['format'] != 'libapta-context-matched-runtime-1' or r['protocol_sha256'] != PROTOCOL
            or r['generated_only'] is not True or r['parent_sha256'] != PARENT_RUNTIME
            or io.sha(safe(r['parent_snapshot']) / 'snapshot.json') != PARENT_RUNTIME
            or r['components'] != components()):
        raise ValueError('runtime/source identity differs')
    parent = strict_json((safe(r['parent_snapshot']) / 'snapshot.json').read_bytes())
    if json.dumps(r['parent_receipt'], sort_keys=True) != json.dumps(parent, sort_keys=True):
        raise ValueError('embedded parent runtime receipt differs from bound snapshot')
    io.recheck(r['components'])
    if role is not None:
        io.recheck(r['profiles'][role])
        io.runtime_recheck(r['profiles'][role])
    return r


def audit_c3(attempt, output):
    """Metadata parse + opaque binding audit, never parse reference/event leaves."""
    attempt = safe(attempt)
    parent = strict_json((DOCS / 'COUPLED-TIME-METADATA-C3-20261024.json').read_bytes())
    registered = parent['measurement']['artifact_sha256']
    containers = {name: read({'path': str(attempt / name), 'sha256': registered[name]}) for name in
        ('construct.context.json', 'construct.completion.json', 'evaluate.metadata.completion.json',
         'evaluate.support.completion.json')}
    construction = containers['construct.completion.json']
    evaluation = containers['evaluate.metadata.completion.json']
    bindings = dict(evaluation['bindings'])
    if any(name in bindings and bindings[name] != digest for name, digest in construction['bindings'].items()):
        raise ValueError('C3 metadata binding conflict')
    bindings.update(construction['bindings'])
    for name in containers:
        bindings[str(attempt / name)] = registered[name]
    io.recheck(bindings)
    context = containers['construct.context.json']
    inputs_entry = {'path': context['input_manifest_path'], 'sha256': context['input_manifest_sha256']}
    inputs = read(inputs_entry)
    inventory_path = next((name for name in bindings if name.endswith('/generated/inventory.json')), None)
    if inventory_path is None:
        raise ValueError('fixed C3 inventory missing')
    inventory = read({'path': inventory_path, 'sha256': bindings[inventory_path]})
    completion = containers['evaluate.support.completion.json']
    if len(inputs['sources']) != 100 or len(construction['candidates']) != 100 or len(completion['details']) != 100:
        raise ValueError('incomplete C3 cohort')
    rows = []
    for i, (original, full, candidate, detail) in enumerate(zip(inputs['sources'], inventory['sources'],
            construction['candidates'], completion['details'])):
        if (original['track'] != full['track'] or original['track'] != candidate['source_id']
                or original['track'] != detail['track'] or candidate['index'] != i or original['known_gaps']):
            raise ValueError('C3 source/order/gap identity differs')
        c = {'path': str(attempt / 'candidates' / (str(i) + '.json')), 'sha256': candidate['sha256']}
        d = {'path': str(attempt / 'evaluate' / (str(i) + '.json')), 'sha256': detail['sha256']}
        for entry in (c, d):
            io.bind(entry['path'], entry['sha256'], bindings)
        rows.append({'index': i, 'id': original['track'], 'total_frames': original['total_frames'],
            'windows': original['windows'], 'known_gaps': [],
            'original': {'path': original['events_path'], 'sha256': original['events_sha256']},
            'candidate': c, 'frozen': d,
            'baseline': {'path': full['baseline_score_path'], 'sha256': full['baseline_score_sha256']},
            'coarse': {'path': full['coarse_path'], 'sha256': full['coarse_sha256']}})
    labels = inventory['labels_manifest_path']
    if labels not in bindings:
        raise ValueError('labels not bound by old C3 completion')
    scope = {'format': 'libapta-context-matched-inputs-1', 'kind': 'C3_FIXED_GENERATED',
        'protocol_sha256': PROTOCOL, 'rows': rows, 'labels': {'path': labels, 'sha256': bindings[labels]},
        'bindings': bindings, 'expected_support': {'windows': 600, 'reference_complete_windows': 400,
        'reference_incomplete_windows': 200}}
    validate_scope(scope)
    io.recheck(bindings)
    write(output, scope)
    return {'sources': 100, 'windows': 600, 'bound_files': len(bindings), 'scope': leaf(output),
            'labels_parsed': False, 'event_or_score_leaves_parsed': False}


def validate_scope(s):
    keys(s, ('format', 'kind', 'protocol_sha256', 'rows', 'labels', 'bindings', 'expected_support'))
    if (s['format'] != 'libapta-context-matched-inputs-1' or s['protocol_sha256'] != PROTOCOL
            or s['kind'] not in ('C3_FIXED_GENERATED', 'PUBLIC_PREFLIGHT') or type(s['rows']) is not list
            or not 1 <= len(s['rows']) <= 100):
        raise ValueError('invalid generated scope')
    ids, windows = [], 0
    for i, row in enumerate(s['rows']):
        keys(row, ('index', 'id', 'total_frames', 'windows', 'known_gaps', 'original', 'candidate', 'frozen', 'baseline', 'coarse'))
        integer(row['index'], i, i)
        integer(row['total_frames'], 1, 1440000)
        if type(row['id']) is not str or not row['id'] or row['id'] in ids or row['known_gaps'] != []:
            raise ValueError('invalid source identity or unsupported gap')
        ids.append(row['id'])
        if type(row['windows']) is not list or not 1 <= len(row['windows']) <= 8:
            raise ValueError('invalid window count')
        previous = 0
        for w in row['windows']:
            keys(w, ('first_frame', 'end_frame'))
            integer(w['first_frame'], previous, row['total_frames'] - 1)
            integer(w['end_frame'], w['first_frame'] + 1, row['total_frames'])
            previous = w['end_frame']
        windows += len(row['windows'])
        for key in ('original', 'candidate', 'frozen', 'baseline', 'coarse'):
            keys(row[key], ('path', 'sha256'))
            safe(row[key]['path'])
            if s['bindings'].get(row[key]['path']) != row[key]['sha256']:
                raise ValueError('source leaf missing from bindings')
    keys(s['labels'], ('path', 'sha256'))
    if s['bindings'].get(s['labels']['path']) != s['labels']['sha256']:
        raise ValueError('labels binding absent')
    keys(s['expected_support'], ('windows', 'reference_complete_windows', 'reference_incomplete_windows'))
    for value in s['expected_support'].values():
        integer(value, 0, 600)
    if (windows > 600 or s['expected_support']['windows'] != windows
            or sum(s['expected_support'][k] for k in ('reference_complete_windows', 'reference_incomplete_windows')) != windows):
        raise ValueError('scope support denominator differs')
    if s['kind'] == 'C3_FIXED_GENERATED' and (len(ids), windows, s['expected_support']) != (100, 600,
            {'windows': 600, 'reference_complete_windows': 400, 'reference_incomplete_windows': 200}):
        raise ValueError('real or substituted cohort rejected')


def authorize(kind, scope_sha256, runtime, attempt):
    if kind == 'PUBLIC_PREFLIGHT':
        return None
    proposal = DOCS / 'CONTEXT-ANCHOR-MATCHED-EVALUATION-GENERATED-SCOPE.json'
    if kind != 'C3_FIXED_GENERATED' or not proposal.is_file():
        raise ValueError('separately tracked P6 generated-scope proposal required')
    value = strict_json(proposal.read_bytes())
    keys(value, ('format', 'protocol_sha256', 'scope_sha256', 'runtime_sha256', 'preflight_sha256',
                 'components', 'attempt_path', 'generated_only', 'single_attempt'))
    preflight = DOCS / 'CONTEXT-ANCHOR-MATCHED-EVALUATION-P5-PREFLIGHT.json'
    if (value['format'] != 'libapta-context-matched-generated-scope-1' or value['protocol_sha256'] != PROTOCOL
            or value['scope_sha256'] != scope_sha256 or value['runtime_sha256'] != runtime['sha256']
            or value['attempt_path'] != str(safe(attempt)) or value['components'] != components()
            or value['generated_only'] is not True or value['single_attempt'] is not True
            or not preflight.is_file() or io.sha(preflight) != value['preflight_sha256']):
        raise ValueError('stale/mixed generated-scope proposal')
    ready = strict_json(preflight.read_bytes())
    if (ready['status'] != 'P5_GENERATED_OPERATIONAL_PREFLIGHT_PASS'
            or ready['components'] != components() or ready['runtime_sha256'] != runtime['sha256']
            or ready['c3_scope_sha256'] != scope_sha256):
        raise ValueError('preflight does not cover generated scope')
    io.check_source(ROOT, io.git(ROOT, 'rev-parse', 'HEAD'))
    return io.sha(proposal)


def stage(scope_entry, runtime, attempt):
    s = read(scope_entry)
    validate_scope(s)
    attempt = safe(attempt)
    io.external(attempt, ROOT)
    if attempt.exists():
        raise ValueError('new exclusive attempt required')
    authorization = authorize(s['kind'], scope_entry['sha256'], runtime, attempt)
    # No labels or saved scores are read here, even to build minimal worker jobs.
    attempt.mkdir(parents=True)
    records = []
    for i, row in enumerate(s['rows']):
        original, candidate = read(row['original']), read(row['candidate'])
        events = original['events']
        if s['kind'] == 'PUBLIC_PREFLIGHT' and (events['beat_frames'] != [1000 + 12000 * n for n in range(60)]
                or candidate['events']['beat_frames'] != events['beat_frames']):
            raise ValueError('public preflight accepts only its fixed arithmetic recipe')
        arms = {}
        for name, raw in (('original', events), ('coupled', candidate['events'])):
            beats, db = raw['beat_frames'], raw['downbeat_frames']
            indices = {v: n for n, v in enumerate(beats)}
            if any(v not in indices for v in db):
                raise ValueError('downbeat outside source event list')
            arms[name] = {'beat_frames': beats, 'downbeat_ordinals': [indices[v] for v in db]}
        if (candidate['total_frames'] != row['total_frames']
                or [{k: w[k] for k in ('first_frame', 'end_frame')} for w in candidate['windows']] != row['windows']
                or arms['coupled']['beat_frames'] and
                   (len(arms['original']['beat_frames']) != len(arms['coupled']['beat_frames'])
                    or arms['original']['downbeat_ordinals'] != arms['coupled']['downbeat_ordinals'])):
            raise ValueError('fixed candidate topology/geometry changed')
        job = {'total_frames': row['total_frames'], 'windows': row['windows'], 'known_gaps': [], **arms}
        records.append(write(attempt / 'jobs' / (str(i) + '.json'), job, attempt))
    manifest = {'format': 'libapta-context-matched-jobs-1', 'protocol_sha256': PROTOCOL,
        'source_revision': io.git(ROOT, 'rev-parse', 'HEAD'), 'scope_sha256': scope_entry['sha256'],
        'runtime_sha256': runtime['sha256'], 'jobs': records, 'source_count': len(records),
        'window_count': s['expected_support']['windows'], 'kind': s['kind'], 'authorization_sha256': authorization}
    write(attempt / 'manifest.json', manifest, attempt)
    return manifest


@contextmanager
def deadline():
    def expired(signum, frame):
        raise ValueError('source time cap exceeded')
    previous = signal.signal(signal.SIGALRM, expired)
    signal.setitimer(signal.ITIMER_REAL, 60)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)


def self_rss():
    peak = max(int(line.split()[1]) for line in Path('/proc/self/status').read_text().splitlines()
               if line.startswith(('VmRSS:', 'VmHWM:')))
    if peak > 262144:
        raise ValueError('worker self RSS/HWM cap exceeded')
    return peak


def completion(attempt, role, manifest):
    p = attempt / (role + '.complete.json')
    value = read(leaf(p))
    keys(value, PHASE_KEYS)
    if (value['format'] != 'libapta-context-matched-phase-1' or value['role'] != role or value['status'] != 'COMPLETE'
            or any(value[k] != manifest[k] for k in ('protocol_sha256', 'runtime_sha256', 'scope_sha256', 'source_revision', 'source_count', 'window_count'))
            or value['manifest_sha256'] != io.sha(attempt / 'manifest.json')
            or value['reference_labels_loaded'] is not (role == 'evaluate')
            or type(value['records']) is not list or len(value['records']) != manifest['source_count']):
        raise ValueError('phase completion identity/count differs')
    integer(value['projection_count'], manifest['window_count'] * 2, manifest['window_count'] * 2)
    integer(value['source_count'], manifest['source_count'], manifest['source_count'])
    integer(value['window_count'], manifest['window_count'], manifest['window_count'])
    integer(value['self_max_rss_kib'], 1, 262144)
    expected = {'project': None, 'verify': None}
    if role != 'project':
        expected['project'] = io.sha(attempt / 'project.complete.json')
    if role == 'evaluate':
        expected['verify'] = io.sha(attempt / 'verify.complete.json')
    if value['upstream'] != expected:
        raise ValueError('stale/mixed phase chain')
    for i, entry in enumerate(value['records']):
        if entry['path'] != str(attempt / role / (str(i) + '.json')):
            raise ValueError('phase record order/path differs')
        io.bind(safe(entry['path']), entry['sha256'], {})
    return value


def phase(role, attempt, runtime, scope_entry=None):
    attempt = safe(attempt)
    if (attempt / 'INVALID.json').exists():
        raise ValueError('closed invalid attempt; no resume')
    manifest = read(leaf(attempt / 'manifest.json'))
    keys(manifest, ('format', 'protocol_sha256', 'source_revision', 'scope_sha256', 'runtime_sha256', 'jobs', 'source_count', 'window_count', 'kind', 'authorization_sha256'))
    if (manifest['format'] != 'libapta-context-matched-jobs-1' or manifest['protocol_sha256'] != PROTOCOL
            or manifest['runtime_sha256'] != runtime['sha256'] or manifest['source_revision'] != io.git(ROOT, 'rev-parse', 'HEAD')):
        raise ValueError('phase manifest/source identity differs')
    integer(manifest['source_count'], 1, 100)
    integer(manifest['window_count'], 1, 600)
    if len(manifest['jobs']) != manifest['source_count'] or (attempt / (role + '.complete.json')).exists():
        raise ValueError('incomplete or repeated phase')
    if authorize(manifest['kind'], manifest['scope_sha256'], runtime, attempt) != manifest['authorization_sha256']:
        raise ValueError('phase authorization differs')
    for i, entry in enumerate(manifest['jobs']):
        if entry['path'] != str(attempt / 'jobs' / (str(i) + '.json')):
            raise ValueError('job source order/path differs')
    upstream = {'project': None, 'verify': None}
    project, verify = None, None
    if role != 'project':
        project = completion(attempt, 'project', manifest)
        upstream['project'] = io.sha(attempt / 'project.complete.json')
    if role == 'evaluate':
        verify = completion(attempt, 'verify', manifest)
        upstream['verify'] = io.sha(attempt / 'verify.complete.json')
        for i, proof_entry in enumerate(verify['records']):
            proof = read(proof_entry)
            if proof != {'index': i, 'job_sha256': manifest['jobs'][i]['sha256'],
                         'projection_sha256': project['records'][i]['sha256']}:
                raise ValueError('verification proof does not cover projection')
        if scope_entry is None or scope_entry['sha256'] != manifest['scope_sha256']:
            raise ValueError('evaluation scope differs')
    if role != 'evaluate':
        parent_snapshot_json = safe(read(runtime)['parent_snapshot']) / 'snapshot.json'
        def audit(event, args):
            if event == 'import' and args[0].startswith(FORBIDDEN):
                raise ValueError('scoring import in label-blind phase')
            if event == 'open' and isinstance(args[0], (str, bytes)):
                path = Path(args[0].decode() if isinstance(args[0], bytes) else args[0]).absolute()
                if path.suffix == '.json' and not (path.is_relative_to(attempt) or path.is_relative_to(DOCS)
                                                  or path == Path(runtime['path']) or path == parent_snapshot_json):
                    raise ValueError('unselected data access in label-blind phase')
        sys.addaudithook(audit)
    profile_imports(role)
    check_runtime(runtime, role)
    records, results, groups = [], [], []
    scope, labels = None, None
    if role == 'evaluate':
        scope = read(scope_entry)
        validate_scope(scope)
        if scope['kind'] != manifest['kind']:
            raise ValueError('evaluation scope class differs')
        labels = read(scope['labels'])
        if [r['track'] for r in labels['tracks']] != [r['id'] for r in scope['rows']]:
            raise ValueError('reference source order differs')
    observed_windows = 0
    for i, entry in enumerate(manifest['jobs']):
        with deadline():
            job = read(entry)
            keys(job, ('total_frames', 'windows', 'known_gaps', 'original', 'coupled'))
            if job['known_gaps'] != []:
                raise ValueError('gap handling is not registered for fixed C3')
            observed_windows += len(job['windows'])
            if role in ('project', 'verify'):
                fn = (sys.modules['apta_context_projection'].project_window if role == 'project'
                      else sys.modules['apta_context_projection_oracle'].independent_project_window)
                result = {}
                for arm in ('original', 'coupled'):
                    keys(job[arm], ('beat_frames', 'downbeat_ordinals'))
                    result[arm] = [fn(**job[arm], total_frames=job['total_frames'], **w) for w in job['windows']]
                if sum(p['point_count'] for p in result['original']) > 3072 or sum(p['point_count'] for p in result['coupled']) > 3072:
                    raise ValueError('whole-source point budget exceeded')
                if role == 'verify':
                    actual = read(project['records'][i])
                    if json.dumps(result, sort_keys=True) != json.dumps(actual, sort_keys=True):
                        raise ValueError('independent projection replay differs')
                    result = {'index': i, 'job_sha256': entry['sha256'], 'projection_sha256': project['records'][i]['sha256']}
            else:
                row, truth = scope['rows'][i], labels['tracks'][i]
                if row['windows'] != job['windows'] or row['total_frames'] != job['total_frames']:
                    raise ValueError('evaluation source geometry differs')
                if (truth['requested_frames'] != row['total_frames'] or any(type(frame) is not int or
                    not 0 <= frame < row['total_frames'] for field in ('beat_frames', 'downbeat_frames') for frame in truth[field])):
                    raise ValueError('reference source domain differs')
                original, candidate = read(row['original']), read(row['candidate'])
                result = sys.modules['apta_1_1_context_matched_evaluation'].evaluate_source(original['events'], candidate,
                    row['windows'], truth, read(row['baseline']), read(row['coarse']), read(row['frozen']), read(project['records'][i]))
                results.append(result)
                groups.append(truth['genre'])
            records.append(write(attempt / role / (str(i) + '.json'), result, attempt))
            self_rss()
    if observed_windows != manifest['window_count']:
        raise ValueError('incomplete window cohort')
    summary = None
    if role == 'evaluate':
        report = sys.modules['apta_1_1_context_matched_evaluation'].summarize(results, groups)
        if {k: report[k] for k in scope['expected_support']} != scope['expected_support']:
            raise ValueError('reference support cohort differs')
        summary = write(attempt / 'matched-report.json', report, attempt)
    check_runtime(runtime, role)
    for entry in manifest['jobs'] + records:
        io.bind(entry['path'], entry['sha256'], {})
    if role != 'project':
        completion(attempt, 'project', manifest)
    if role == 'evaluate':
        completion(attempt, 'verify', manifest)
        io.recheck(scope['bindings'])
        io.bind(scope_entry['path'], scope_entry['sha256'], {})
    result = {'format': 'libapta-context-matched-phase-1', 'role': role, 'status': 'COMPLETE',
        'protocol_sha256': PROTOCOL, 'runtime_sha256': runtime['sha256'], 'scope_sha256': manifest['scope_sha256'],
        'source_revision': manifest['source_revision'], 'manifest_sha256': io.sha(attempt / 'manifest.json'),
        'upstream': upstream, 'records': records, 'summary': summary, 'source_count': manifest['source_count'],
        'window_count': manifest['window_count'], 'projection_count': observed_windows * 2,
        'reference_labels_loaded': role == 'evaluate', 'self_max_rss_kib': self_rss()}
    write(attempt / (role + '.complete.json'), result, attempt)


def dispatch(role, attempt, runtime, scope_entry=None, accounting_roots=()):
    if role not in ROLES:
        raise ValueError('unknown phase')
    if (safe(attempt) / 'INVALID.json').exists():
        raise ValueError('closed invalid attempt; no resume')
    r = check_runtime(runtime)
    arguments = [r['parent_receipt']['interpreter'], '-B', '-X', 'pycache_prefix=/libapta-no-cache',
        Path(__file__), '_phase', role, safe(attempt), runtime['path'], runtime['sha256']]
    if scope_entry:
        arguments += [scope_entry['path'], scope_entry['sha256']]
    try:
        return io.supervise(namespace(r['parent_snapshot'], arguments, r['parent_receipt']),
            1800, 262144, 67108864, [attempt, *accounting_roots], safe(attempt) / (role + '.supervised'))
    except BaseException as error:
        if not (safe(attempt) / 'INVALID.json').exists():
            write(safe(attempt) / 'INVALID.json', {'status': 'INVALID', 'role': role, 'error': str(error)}, safe(attempt))
        raise


def run_public(scope_entry, runtime, attempt):
    if read(scope_entry)['kind'] != 'PUBLIC_PREFLIGHT':
        raise ValueError('public preflight cannot dispatch C3/real data')
    stage(scope_entry, runtime, attempt)
    for role in ROLES:
        dispatch(role, attempt, runtime, scope_entry if role == 'evaluate' else None,
                 accounting_roots=[safe(scope_entry['path']).parent])
    return read(leaf(safe(attempt) / 'matched-report.json'))


def run_registered(scope_entry, runtime, attempt):
    if read(scope_entry)['kind'] != 'C3_FIXED_GENERATED':
        raise ValueError('only P3-bound fixed generated C3 scope is eligible')
    r = check_runtime(runtime)
    authorize('C3_FIXED_GENERATED', scope_entry['sha256'], runtime, attempt)
    frozen().verify(safe(r['parent_snapshot']))
    stage(scope_entry, runtime, attempt)
    for role in ROLES:
        dispatch(role, attempt, runtime, scope_entry if role == 'evaluate' else None)
    frozen().verify(safe(r['parent_snapshot']))
    return read(leaf(safe(attempt) / 'matched-report.json'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('_profile', '_phase'))
    p.add_argument('role', choices=ROLES)
    p.add_argument('paths', nargs='+')
    a = p.parse_args()
    if a.action == '_profile' and len(a.paths) == 1:
        runtime_profile(a.role, safe(a.paths[0]))
    elif a.action == '_phase' and len(a.paths) in (3, 5):
        attempt, runtime_path, digest = a.paths[:3]
        scope = {'path': a.paths[3], 'sha256': a.paths[4]} if len(a.paths) == 5 else None
        phase(a.role, attempt, {'path': runtime_path, 'sha256': digest}, scope)
    else:
        raise ValueError('invalid internal invocation')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps({'status': 'INVALID', 'error': str(error)}), file=sys.stderr)
        sys.exit(2)
