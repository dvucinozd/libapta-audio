#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""MC-02 fixed generated adapter over the unchanged private P5 phase engine."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
from types import ModuleType

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs/rust'
OPERATIONAL_PROTOCOL = 'e792119472e9e5276bb4f1157f27672aab0955353361e798b890ea081a7b014d'
PROJECTION_PROTOCOL = '542f5e44a46662f233a013faacdcc66e5254c579e1930cc5ec99c789c10da191'
spec = importlib.util.spec_from_file_location('_apta_mc02_engine', ROOT / 'tools/apta_1_1_context_matched_runner.py')
engine = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = engine
spec.loader.exec_module(engine)
_components, _imports, _stage, _phase = engine.components, engine.profile_imports, engine.stage, engine.phase


def components():
    path = DOCS / 'METER-CONTEXT-MC02-PROTOCOL.json'
    if engine.io.sha(path) != OPERATIONAL_PROTOCOL:
        raise ValueError('MC-02 registration changed')
    registration = engine.strict_json(path.read_bytes())
    result = _components()
    result.update({str(ROOT / n): h for n, h in registration['binding_sha256'].items()})
    result[str(path)] = OPERATIONAL_PROTOCOL
    for name in registration['implementation_paths']:
        result[str(ROOT / name)] = engine.io.sha(ROOT / name)
    engine.io.recheck(result)
    return result


def _project(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame):
    from apta_meter_context_projection import project_window
    return project_window(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame, known_gaps=[])


def _verify(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame):
    from apta_meter_context_oracle import project_window_oracle
    return project_window_oracle(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame, known_gaps=[])


def profile_imports(role):
    if role == 'evaluate':
        return _imports(role)
    if role == 'project':
        import apta_meter_context_projection as target
        original, name, function = target.p1, 'apta_context_projection', 'project_window'
        callback = _project
    elif role == 'verify':
        import apta_meter_context_oracle
        name, function = 'apta_context_projection_oracle', 'independent_project_window'
        original = sys.modules.get('_apta_mc02_frozen_' + name, sys.modules[name])
        callback = _verify
    else:
        raise ValueError('unknown phase')
    sys.modules['_apta_mc02_frozen_' + name] = original
    facade = ModuleType(name)
    facade.__file__, facade.__cached__ = str(Path(__file__).resolve()), None
    setattr(facade, function, callback)
    sys.modules[name] = facade
    if any(name.startswith(engine.FORBIDDEN) for name in sys.modules):
        raise ValueError('scoring/reference import before evaluation')


def prepare_runtime(snapshot, destination):
    snapshot, destination = engine.safe(snapshot), engine.safe(destination)
    engine.io.external(destination, ROOT)
    if engine.io.sha(snapshot / 'snapshot.json') != engine.PARENT_RUNTIME:
        raise ValueError('fixed generated parent runtime required')
    parent = engine.frozen().verify(snapshot)
    destination.mkdir(parents=True, exist_ok=False)
    source, profiles = components(), {}
    for role in engine.ROLES:
        output = destination / (role + '.runtime.json')
        command = engine.namespace(snapshot, [parent['interpreter'], '-B', '-X', 'pycache_prefix=/libapta-no-cache',
            Path(__file__), '_profile', role, output], parent)
        engine.io.supervise(command, 1800, 262144, 67108864, [destination], destination / (role + '.profile'))
        profiles[role] = engine.read(engine.leaf(output))['bindings']
    engine.frozen().verify(snapshot)
    if source != components():
        raise ValueError('source changed during runtime preparation')
    descriptor = {'format': 'libapta-context-matched-runtime-1', 'parent_snapshot': str(snapshot),
        'parent_sha256': engine.PARENT_RUNTIME, 'parent_receipt': parent, 'protocol_sha256': engine.PROTOCOL,
        'components': source, 'profiles': profiles, 'generated_only': True}
    return engine.write(destination / 'runtime.json', descriptor)


def authorize(kind, scope_sha256, runtime, attempt):
    if kind == 'PUBLIC_PREFLIGHT':
        return None
    proposal = DOCS / 'METER-CONTEXT-GENERATED-SCOPE.json'
    if kind != 'C3_FIXED_GENERATED' or not proposal.is_file():
        raise ValueError('separate meter-context generated-scope proposal required')
    value = engine.strict_json(proposal.read_bytes())
    engine.keys(value, ('format', 'scientific_protocol_sha256', 'projection_protocol_sha256',
        'operational_protocol_sha256', 'scope_sha256', 'runtime_sha256', 'preflight_sha256',
        'components', 'attempt_path', 'generated_only', 'single_attempt'))
    ready_path = DOCS / 'METER-CONTEXT-MC02-PREFLIGHT.json'
    expected = {'scientific_protocol_sha256': engine.PROTOCOL, 'projection_protocol_sha256': PROJECTION_PROTOCOL,
                'operational_protocol_sha256': OPERATIONAL_PROTOCOL}
    if (value['format'] != 'libapta-meter-context-generated-scope-1'
            or any(value[k] != v for k, v in expected.items())
            or value['scope_sha256'] != scope_sha256 or value['runtime_sha256'] != runtime['sha256']
            or value['components'] != components() or value['attempt_path'] != str(engine.safe(attempt))
            or value['generated_only'] is not True or value['single_attempt'] is not True
            or not ready_path.is_file() or engine.io.sha(ready_path) != value['preflight_sha256']):
        raise ValueError('stale/mixed meter-context authorization')
    ready = engine.strict_json(ready_path.read_bytes())
    if (ready['status'] != 'MC02_GENERATED_OPERATIONAL_PREFLIGHT_PASS'
            or any(ready[k] != v for k, v in expected.items())
            or ready['components'] != components() or ready['runtime_sha256'] != runtime['sha256']
            or ready['c3_scope_sha256'] != scope_sha256):
        raise ValueError('meter-context preflight does not cover scope')
    engine.io.check_source(ROOT, engine.io.git(ROOT, 'rev-parse', 'HEAD'))
    return engine.io.sha(proposal)


def identity(attempt, manifest, runtime):
    return {'format': 'libapta-meter-context-phase-identity-1',
            'operational_protocol_sha256': OPERATIONAL_PROTOCOL, 'projection_protocol_sha256': PROJECTION_PROTOCOL,
            'scientific_protocol_sha256': engine.PROTOCOL, 'manifest_sha256': engine.io.sha(attempt / 'manifest.json'),
            'runtime_sha256': runtime['sha256'], 'source_revision': manifest['source_revision']}


def stage(scope_entry, runtime, attempt):
    engine.check_runtime(runtime)
    attempt = engine.safe(attempt)
    manifest = _stage(scope_entry, runtime, attempt)
    engine.write(attempt / 'meter-context.identity.json', identity(attempt, manifest, runtime), attempt)
    return manifest


def _same(left, right):
    return json.dumps(left, sort_keys=True, allow_nan=False) == json.dumps(right, sort_keys=True, allow_nan=False)


def _scope_jobs_after_verification(attempt, manifest, scope_entry):
    project, verify = (engine.completion(attempt, r, manifest) for r in ('project', 'verify'))
    for i, entry in enumerate(verify['records']):
        if not _same(engine.read(entry), {'index': i, 'job_sha256': manifest['jobs'][i]['sha256'],
                                         'projection_sha256': project['records'][i]['sha256']}):
            raise ValueError('verification proof does not cover scoped job')
    if scope_entry is None or scope_entry['sha256'] != manifest['scope_sha256']:
        raise ValueError('evaluation scope differs')
    scope = engine.read(scope_entry)
    engine.validate_scope(scope)
    if len(scope['rows']) != manifest['source_count'] or scope['kind'] != manifest['kind']:
        raise ValueError('source cohort differs')
    for i, row in enumerate(scope['rows']):
        arms = {}
        for name, key in (('original', 'original'), ('coupled', 'candidate')):
            events = engine.read(row[key])['events']
            positions = {v: j for j, v in enumerate(events['beat_frames'])}
            arms[name] = {'beat_frames': events['beat_frames'],
                          'downbeat_ordinals': [positions[v] for v in events['downbeat_frames']]}
        expected = {'total_frames': row['total_frames'], 'windows': row['windows'],
                    'known_gaps': row['known_gaps'], **arms}
        if not _same(engine.read(manifest['jobs'][i]), expected):
            raise ValueError('staged events/geometry differ from scoped source')


def phase(role, attempt, runtime, scope_entry=None):
    attempt = engine.safe(attempt)
    if (attempt / 'INVALID.json').exists():
        raise ValueError('closed invalid attempt; no resume')
    engine.check_runtime(runtime)
    manifest = engine.read(engine.leaf(attempt / 'manifest.json'))
    engine.keys(manifest, ('format', 'protocol_sha256', 'source_revision', 'scope_sha256', 'runtime_sha256',
                           'jobs', 'source_count', 'window_count', 'kind', 'authorization_sha256'))
    engine.integer(manifest['source_count'], 1, 100)
    engine.integer(manifest['window_count'], 1, 600)
    if (manifest['protocol_sha256'] != engine.PROTOCOL or manifest['runtime_sha256'] != runtime['sha256']
            or manifest['source_revision'] != engine.io.git(ROOT, 'rev-parse', 'HEAD')
            or manifest['format'] != 'libapta-context-matched-jobs-1'
            or type(manifest['jobs']) is not list or len(manifest['jobs']) != manifest['source_count']):
        raise ValueError('meter-context manifest identity differs')
    if authorize(manifest['kind'], manifest['scope_sha256'], runtime, attempt) != manifest['authorization_sha256']:
        raise ValueError('meter-context authorization differs')
    for i, entry in enumerate(manifest['jobs']):
        if entry['path'] != str(attempt / 'jobs' / (str(i) + '.json')):
            raise ValueError('job order/path differs')
    expected = identity(attempt, manifest, runtime)
    if not _same(engine.read(engine.leaf(attempt / 'meter-context.identity.json')), expected):
        raise ValueError('meter-context phase identity differs')
    if role == 'evaluate':
        _scope_jobs_after_verification(attempt, manifest, scope_entry)
    _phase(role, attempt, runtime, scope_entry)
    if not _same(engine.read(engine.leaf(attempt / 'meter-context.identity.json')), expected):
        raise ValueError('meter-context phase identity changed')


def dispatch(role, attempt, runtime, scope_entry=None, accounting_roots=()):
    if role not in engine.ROLES:
        raise ValueError('unknown phase')
    attempt = engine.safe(attempt)
    if (attempt / 'INVALID.json').exists():
        raise ValueError('closed invalid attempt; no resume')
    descriptor = engine.check_runtime(runtime)
    args = [descriptor['parent_receipt']['interpreter'], '-B', '-X', 'pycache_prefix=/libapta-no-cache',
            Path(__file__), '_phase', role, attempt, runtime['path'], runtime['sha256']]
    if scope_entry:
        args += [scope_entry['path'], scope_entry['sha256']]
    try:
        return engine.io.supervise(engine.namespace(descriptor['parent_snapshot'], args, descriptor['parent_receipt']),
            1800, 262144, 67108864, [attempt, *accounting_roots], attempt / (role + '.supervised'))
    except BaseException as error:
        if not (attempt / 'INVALID.json').exists():
            engine.write(attempt / 'INVALID.json', {'status': 'INVALID', 'role': role, 'error': str(error)}, attempt)
        raise


engine.components, engine.profile_imports, engine.authorize = components, profile_imports, authorize
engine.prepare_runtime, engine.stage, engine.phase, engine.dispatch = prepare_runtime, stage, phase, dispatch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('_profile', '_phase'))
    parser.add_argument('role', choices=engine.ROLES)
    parser.add_argument('paths', nargs='+')
    args = parser.parse_args()
    if args.action == '_profile' and len(args.paths) == 1:
        engine.runtime_profile(args.role, engine.safe(args.paths[0]))
    elif args.action == '_phase' and len(args.paths) in (3, 5):
        attempt, path, digest = args.paths[:3]
        scope = {'path': args.paths[3], 'sha256': args.paths[4]} if len(args.paths) == 5 else None
        phase(args.role, attempt, {'path': path, 'sha256': digest}, scope)
    else:
        raise ValueError('invalid fixed internal invocation')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps({'status': 'INVALID', 'error': str(error)}), file=sys.stderr)
        sys.exit(2)
