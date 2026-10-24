#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Registered coupled-time v2 orchestration. Real dispatch needs R4 + new scope."""
from array import array
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
import time

import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
OPERATIONAL_SHA256 = '9c252c0734c43d3856146b2915648e6273d38060edf635fce717298131a2370e'
REGISTRATION_SHA256 = '681fb6f20eb93b2817b7feee683e6f442c142573fe9e4c4860875b209237d4d6'
PLAIN = ('beat_frames', 'downbeat_ordinals', 'total_frames', 'windows', 'known_gaps')
SOURCE_KEYS = {'track', 'events_path', 'events_sha256', 'capture_record_path', 'capture_record_sha256',
               'source_audio_sha256', *PLAIN}
UPSTREAM_KEYS = ('construction_completion_sha256', 'construction_supervision_sha256',
                 'verification_completion_sha256', 'verification_supervision_sha256')


def strict_json(payload):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON field')
            result[key] = value
        return result

    def nonfinite(_value):
        raise ValueError('nonfinite JSON')
    value = json.loads(payload, object_pairs_hook=pairs, parse_constant=nonfinite)

    def finite(item):
        if type(item) is float and not math.isfinite(item):
            raise ValueError('nonfinite JSON number')
        if type(item) is dict:
            for child in item.values():
                finite(child)
        if type(item) is list:
            for child in item:
                finite(child)
    finite(value)
    return value


def safe_path(value):
    raw = str(value)
    if not raw.startswith('/') or '\0' in raw or any(p in ('.', '..') for p in raw.split('/')):
        raise ValueError('absolute nontraversing Linux path required')
    path = Path(raw)
    if any(p.is_symlink() for p in (path, *path.parents)):
        raise ValueError('symlink path forbidden')
    return path


def read_bound(path, digest, bindings):
    path = safe_path(path)
    io.bind(path, digest, bindings)
    value = strict_json(path.read_bytes())
    io.bind(path, digest, bindings)
    return value


def validate_record(value, cfg, kind=None):
    """Validate the closed subset used by the pinned local schema, with exact types."""
    schema = cfg['schema']
    if kind is None:
        kinds = {v: k for k, v in cfg['operational']['records'].items()}
        kind = kinds.get(value.get('format')) if type(value) is dict else None
        if kind is None:
            raise ValueError('unknown record format')

    def check(v, spec):
        if '$ref' in spec:
            return check(v, schema['$defs'][spec['$ref'].split('/')[-1]])
        if 'anyOf' in spec:
            if v is None:
                return
            return check(v, spec['anyOf'][0])
        types = {'object': dict, 'array': list, 'string': str, 'integer': int, 'boolean': bool}
        t = spec.get('type')
        if t in types and type(v) is not types[t]:
            raise ValueError('wrong exact record type')
        if t == 'number' and (type(v) not in (int, float) or not math.isfinite(v)):
            raise ValueError('nonfinite or nonnumeric field')
        if 'const' in spec and (type(v) is not type(spec['const']) or v != spec['const']):
            raise ValueError('record constant differs')
        if 'enum' in spec and v not in spec['enum']:
            raise ValueError('record enum differs')
        if t in ('integer', 'number'):
            if v < spec.get('minimum', -math.inf) or v > spec.get('maximum', math.inf):
                raise ValueError('record numeric limit')
        if t == 'string':
            if not spec.get('minLength', 0) <= len(v) <= spec.get('maxLength', math.inf):
                raise ValueError('record string length')
            if 'pattern' in spec and re.fullmatch(spec['pattern'], v) is None:
                raise ValueError('record string pattern')
        if t == 'array':
            if not spec.get('minItems', 0) <= len(v) <= spec.get('maxItems', math.inf):
                raise ValueError('record array length')
            if spec.get('uniqueItems') and len({json.dumps(x, sort_keys=True) for x in v}) != len(v):
                raise ValueError('duplicate record array value')
            for item in v:
                check(item, spec['items'])
        if t == 'object':
            if not set(spec.get('required', ())) <= set(v) or len(v) < spec.get('minProperties', 0):
                raise ValueError('missing record fields')
            props = spec.get('properties', {})
            for name, item in v.items():
                if 'propertyNames' in spec:
                    check(name, spec['propertyNames'])
                child = props.get(name, spec.get('additionalProperties', True))
                if child is False:
                    raise ValueError('extra record field')
                if isinstance(child, dict):
                    check(item, child)
    check(value, schema['$defs'][kind])


def settings():
    docs, bindings = ROOT / 'docs/rust', {}
    op = read_bound(docs / 'COUPLED-TIME-RUNNER-V2-PROTOCOL.json', OPERATIONAL_SHA256, bindings)
    for name, digest in op['binding'].items():
        io.bind(docs / name, digest, bindings)
    for name, digest in op['immutable_component_sha256'].items():
        io.bind(ROOT / name, digest, bindings)
    p = read_bound(docs / 'COUPLED-TIME-DEVELOPMENT-PROTOCOL.json', REGISTRATION_SHA256, bindings)
    schema = read_bound(docs / 'COUPLED-TIME-RUNNER-V2-SCHEMA.json',
                        op['binding']['COUPLED-TIME-RUNNER-V2-SCHEMA.json'], bindings)
    cfg = {'operational': op, 'protocol': p, 'schema': schema, 'bindings': bindings,
           'registration_sha256': REGISTRATION_SHA256, 'operational_registration_sha256': OPERATIONAL_SHA256,
           'source_root': str(ROOT)}
    preflight_path, auth_path = docs / op['preflight_record'], docs / op['authorization_record']
    if not preflight_path.is_file() or not auth_path.is_file():
        raise ValueError('v2 R4 preflight and distinct authorization required; real execution disabled')
    preflight = read_bound(preflight_path, io.sha(preflight_path), bindings)
    auth = read_bound(auth_path, io.sha(auth_path), bindings)
    validate_record(preflight, cfg, 'preflight')
    validate_record(auth, cfg, 'authorization')
    if (preflight['operational_registration_sha256'] != OPERATIONAL_SHA256
            or auth['operational_registration_sha256'] != OPERATIONAL_SHA256
            or auth['preflight_sha256'] != io.sha(preflight_path)
            or auth['preflight_source_revision'] != preflight['source_revision']):
        raise ValueError('stale v2 preflight/authorization')
    required = {**op['immutable_component_sha256'], **{name: None for name in op['future_preflight_components']}}
    if not set(required) <= set(preflight['component_sha256']):
        raise ValueError('incomplete v2 component binding')
    for name, digest in preflight['component_sha256'].items():
        if name in op['immutable_component_sha256'] and digest != op['immutable_component_sha256'][name]:
            raise ValueError('scientific component changed')
        io.bind(ROOT / name, digest, bindings)
    cfg.update(authorization_sha256=io.sha(auth_path), attempt_path_sha256=auth['attempt_path_sha256'])
    return cfg


def command(*args):
    return [sys.executable, str(Path(__file__).resolve()), *map(str, args)]


def destination(attempt, cfg):
    attempt = safe_path(attempt)
    if hashlib.sha256(str(attempt.resolve()).encode()).hexdigest() != cfg['attempt_path_sha256']:
        raise ValueError('unauthorized attempt destination')
    io.external(attempt, ROOT)
    if ROOT.is_relative_to(attempt):
        raise ValueError('output encloses source')
    if attempt.is_dir() and any(attempt.glob('*.INVALID.json')):
        raise ValueError('attempt already closed INVALID; no replay')
    return attempt


def common(cfg, revision):
    return {'source_revision': revision, 'registration_sha256': cfg['registration_sha256'],
            'operational_registration_sha256': cfg['operational_registration_sha256'],
            'authorization_sha256': cfg['authorization_sha256'], 'attempt_path_sha256': cfg['attempt_path_sha256'],
            'input_manifest_sha256': cfg['protocol']['input_identity']['ct03_label_free_inputs_sha256']}


def record_digest(value):
    return hashlib.sha256((json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + '\n').encode()).hexdigest()


def check_identity(record, cfg, revision):
    if any(record.get(key) != value for key, value in common(cfg, revision).items()):
        raise ValueError('record authority/source differs')


def write_record(attempt, path, value, cfg, kind):
    validate_record(value, cfg, kind)
    if io.tree_bytes((attempt,)) + io.json_bytes(value) > cfg['protocol']['limits']['max_attempt_output_bytes']:
        raise ValueError('attempt output cap exceeded')
    io.write_new(safe_path(path), value)


def load_input_manifest(path, cfg, bindings):
    p = cfg['protocol']
    value = read_bound(path, p['input_identity']['ct03_label_free_inputs_sha256'], bindings)
    keys = {'format', 'scope', 'registration_parent_revision', 'source_sample_rate', 'sources', 'bindings',
            'input_order', 'protocol_sha256', 'candidate_constructed', 'execution_enabled'}
    if (set(value) != keys or value['format'] != 'libapta-coupled-time-label-free-inputs-1'
            or type(value['source_sample_rate']) is not int or value['source_sample_rate'] != 48000
            or value['protocol_sha256'] != p['synthetic_protocol_sha256'] or type(value['sources']) is not list):
        raise ValueError('invalid label-free manifest')
    rows = value['sources']
    if len(rows) != p['input_identity']['sources']:
        raise ValueError('incomplete source cohort')
    for row in rows:
        if set(row) != SOURCE_KEYS or type(row['track']) is not str or not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}', row['track']):
            raise ValueError('invalid source fields')
        for key in ('beat_frames', 'downbeat_ordinals'):
            if type(row[key]) is not list or any(type(x) is not int for x in row[key]):
                raise ValueError('integer source arrays required')
        if type(row['total_frames']) is not int or type(row['windows']) is not list or type(row['known_gaps']) is not list:
            raise ValueError('wrong source geometry types')
        for key in ('events_sha256', 'capture_record_sha256', 'source_audio_sha256'):
            if type(row[key]) is not str or re.fullmatch('[0-9a-f]{64}', row[key]) is None:
                raise ValueError('invalid original digest')
    ids = [row['track'] for row in rows]
    expected = {row[k]: row[h] for row in rows for k, h in
                (('events_path', 'events_sha256'), ('capture_record_path', 'capture_record_sha256'))}
    if (len(set(ids)) != len(ids) or sum(len(r['windows']) for r in rows) != p['input_identity']['windows']
            or value['bindings'] != expected):
        raise ValueError('manifest cohort/order/bindings differ')
    return rows


def validate_source(row, cfg, bindings):
    events = read_bound(row['events_path'], row['events_sha256'], bindings)
    captured = read_bound(row['capture_record_path'], row['capture_record_sha256'], bindings)
    if (events['track'] != row['track'] or captured['track'] != row['track']
            or events['audio_sha256'] != row['source_audio_sha256'] or captured['audio_sha256'] != row['source_audio_sha256']
            or events['events'] != captured['events'] or row['beat_frames'] != events['events']['beat_frames']
            or [row['beat_frames'][i] for i in row['downbeat_ordinals']] != events['events']['downbeat_frames']
            or row['windows'] != [{k: w[k] for k in ('first_frame', 'end_frame')} for w in captured['geometry']['windows']]
            or row['total_frames'] != row['windows'][-1]['end_frame']):
        raise ValueError('original event/ordinal/geometry differs')


def audit_cohort(rows, cfg, bindings):
    for row in rows:
        validate_source(row, cfg, bindings)


def completion_path(attempt, role):
    return attempt / ('evaluate.v2.completion.json' if role == 'evaluate' else role + '.completion.json')


def empty_upstream():
    return dict.fromkeys(UPSTREAM_KEYS)


def upstream(attempt, role):
    value = empty_upstream()
    if role != 'construct':
        value.update(construction_completion_sha256=io.sha(safe_path(completion_path(attempt, 'construct'))),
                     construction_supervision_sha256=io.sha(safe_path(attempt / 'construct.v2.supervised.json')))
    if role == 'evaluate':
        value.update(verification_completion_sha256=io.sha(safe_path(completion_path(attempt, 'verify'))),
                     verification_supervision_sha256=io.sha(safe_path(attempt / 'verify.v2.supervised.json')))
    return value


def check_cohort_record(record, rows, cfg, role):
    ids = [r['track'] for r in rows]
    if (record['role'] != role or record['source_count'] != len(rows)
            or record['window_count'] != cfg['protocol']['input_identity']['windows']):
        raise ValueError('record cohort/role differs')
    items = record.get('rows', record.get('candidates'))
    if [r['index'] for r in items] != list(range(len(rows))) or [r['source_id'] for r in items] != ids:
        raise ValueError('record indices/order differ')
    if 'source_order' in record and record['source_order'] != ids:
        raise ValueError('context source order differs')


def read_phase_context(path, expected_sha, cfg, role, index, bindings):
    context = read_bound(path, expected_sha, bindings)
    validate_record(context, cfg, 'context')
    revision = io.git(Path(cfg['source_root']), 'rev-parse', 'HEAD')
    check_identity(context, cfg, revision)
    io.check_source(Path(cfg['source_root']), revision)
    if type(index) is not int or not 0 <= index < context['source_count'] or context['role'] != role:
        raise ValueError('context role/index differs')
    return context


def context_semantics(context, attempt, rows, cfg, role):
    check_cohort_record(context, rows, cfg, role)
    if context['upstream'] != upstream(attempt, role):
        raise ValueError('context upstream differs')
    for entry in context['rows']:
        if (entry['candidate_sha256'] is None) != (role == 'construct'):
            raise ValueError('context candidate nullability differs')


def header(attempt, role, cfg, revision, bindings, with_context=False):
    """Read v2 receipt metadata. Does not recheck its global binding map."""
    path = completion_path(attempt, role)
    report = read_bound(path, io.sha(safe_path(path)), bindings)
    envelope_path = attempt / (role + '.v2.supervised.json')
    envelope = read_bound(envelope_path, io.sha(safe_path(envelope_path)), bindings)
    resource_path = attempt / (role + '.phase.resources.json')
    resources = read_bound(resource_path, io.sha(safe_path(resource_path)), bindings)
    for value, kind in ((report, 'completion'), (envelope, 'supervision')):
        validate_record(value, cfg, kind)
        check_identity(value, cfg, revision)
        if value['role'] != role:
            raise ValueError('receipt role differs')
    validate_record({key: resources[key] for key in cfg['schema']['$defs']['phaseResources']['properties']},
                    cfg, 'phaseResources')
    limits = cfg['protocol']['limits']
    if (envelope['completion_sha256'] != io.sha(path) or envelope['resources_sha256'] != io.sha(resource_path)
            or envelope['context_sha256'] != report['context_sha256']
            or resources['status'] != 'PASS' or resources['failure'] is not None
            or resources['process_group_monitored'] is not True
            or resources['wall_seconds'] > limits['max_phase_seconds']
            or resources['max_observed_process_rss_kib'] > limits['max_process_rss_kib']):
        raise ValueError('unsupervised or invalid receipt')
    if report['reference_labels_loaded'] is not (role == 'evaluate'):
        raise ValueError('receipt label boundary differs')
    if (report['evaluation_report_sha256'] is None) != (role != 'evaluate'):
        raise ValueError('receipt evaluation nullability differs')
    ctxpath = attempt / (role + '.context.json')
    ctx = read_bound(ctxpath, report['context_sha256'], bindings)
    validate_record(ctx, cfg, 'context')
    check_identity(ctx, cfg, revision)
    if ctx['role'] != role or ctx['upstream'] != report['upstream'] or report['upstream'] != upstream(attempt, role):
        raise ValueError('receipt context/upstream differs')
    return (report, ctx) if with_context else report


def merge_expected(target, source):
    for name, digest in source.items():
        if name in target and target[name] != digest:
            raise ValueError('immutable expected binding changed')
        target[name] = digest


def full_receipt(attempt, role, cfg, revision, rows, bindings):
    report, context = header(attempt, role, cfg, revision, bindings, with_context=True)
    check_cohort_record(report, rows, cfg, role)
    context_semantics(context, attempt, rows, cfg, role)
    if role != 'construct' and [r['candidate_sha256'] for r in context['rows']] != [r['sha256'] for r in report['candidates']]:
        raise ValueError('receipt candidates differ from context')
    expected_reports = 0 if role == 'evaluate' else len(rows)
    reports = report['worker_reports']
    if (len(reports) != expected_reports or [r['index'] for r in reports] != list(range(expected_reports))
            or [r['source_id'] for r in reports] != [r['track'] for r in rows[:expected_reports]]):
        raise ValueError('incomplete worker report cohort')
    self_paths = {str(path.resolve()) for path in (completion_path(attempt, role),
                  attempt / (role + '.v2.supervised.json'), attempt / (role + '.phase.resources.json'))}
    expected = {key: digest for key, digest in bindings.items() if key not in self_paths}
    for entry in context['rows']:
        expected[str((attempt / 'jobs' / (str(entry['index']) + '.json')).resolve())] = entry['job_sha256']
    for entry in report['candidates']:
        expected[str((attempt / 'candidates' / (str(entry['index']) + '.json')).resolve())] = entry['sha256']
    for entry in reports:
        expected[str((attempt / role / (str(entry['index']) + '.json')).resolve())] = entry['sha256']
    if any(report['bindings'].get(key) != digest for key, digest in expected.items()):
        raise ValueError('receipt omitted or changed expected global bindings')
    io.recheck(report['bindings'])
    merge_expected(bindings, report['bindings'])
    for entry in report['candidates']:
        io.bind(attempt / 'candidates' / (str(entry['index']) + '.json'), entry['sha256'], bindings)
    for entry in reports:
        io.bind(attempt / role / (str(entry['index']) + '.json'), entry['sha256'], bindings)
    return report


def runtime_bindings():
    import resource
    # Keep the same stdlib module set in parent and pure workers.
    assert array('d').itemsize == 8 and resource.RLIMIT_AS is not None
    result = {}
    io.runtime_snapshot(result)
    return result


def allowed_bindings(cfg, context, attempt, row, index, role, runtime):
    allowed = set(cfg['bindings']) | set(runtime)
    for name in cfg['bindings']:
        if name.endswith('.py'):
            allowed.add(str(Path(importlib.util.cache_from_source(name)).resolve()))
    allowed |= {str(Path(path).resolve()) for path in (
        context['input_manifest_path'], attempt / (role + '.context.json'), row['events_path'],
        row['capture_record_path'], attempt / 'jobs' / (str(index) + '.json'),
        attempt / 'candidates' / (str(index) + '.json'))}
    return allowed


def check_worker_result(report, context, index, role, cfg, attempt, row, runtime):
    validate_record(report, cfg, 'worker')
    check_identity(report, cfg, context['source_revision'])
    selected = context['rows'][index]
    if (report['role'] != role or report['index'] != index or report['source_id'] != row['track']
            or report['context_sha256'] != record_digest(context)
            or report['job_sha256'] != selected['job_sha256']
            or role == 'verify' and report['candidate_sha256'] != selected['candidate_sha256']):
        raise ValueError('worker selected identity differs')
    if abs(sum(report['timings'].values()) - report['wall_seconds']) > 1e-6:
        raise ValueError('worker timing decomposition differs')
    expected = dict(cfg['bindings'])
    merge_expected(expected, runtime)
    expected.update({str(Path(path).resolve()): digest for path, digest in (
        (context['input_manifest_path'], context['input_manifest_sha256']),
        (attempt / (role + '.context.json'), report['context_sha256']),
        (row['events_path'], row['events_sha256']), (row['capture_record_path'], row['capture_record_sha256']),
        (attempt / 'jobs' / (str(index) + '.json'), selected['job_sha256']),
        (attempt / 'candidates' / (str(index) + '.json'), report['candidate_sha256']))})
    if not set(expected) <= set(report['bindings']):
        raise ValueError('worker omitted required bindings')
    if not set(report['bindings']) <= allowed_bindings(cfg, context, attempt, row, index, role, runtime):
        raise ValueError('worker binding scope escaped selected source')
    for key, digest in expected.items():
        if report['bindings'][key] != digest:
            raise ValueError('worker expected binding differs: ' + Path(key).name)
    io.recheck(report['bindings'])


def source_worker(role, attempt, index, context_sha256):
    started = time.monotonic()
    cfg = settings()
    import resource
    limits = cfg['protocol']['limits']
    resource.setrlimit(resource.RLIMIT_AS, (limits['max_process_rss_kib'] * 1024,) * 2)
    attempt = destination(attempt, cfg)
    if ((attempt / role / (str(index) + '.json')).exists() or completion_path(attempt, role).exists()
            or (attempt / (role + '.v2.supervised.json')).exists()):
        raise ValueError('worker or phase already completed; no replay')
    bindings = dict(cfg['bindings'])
    context = read_phase_context(attempt / (role + '.context.json'), context_sha256, cfg, role, index, bindings)
    t1 = time.monotonic()
    rows = load_input_manifest(context['input_manifest_path'], cfg, bindings)
    context_semantics(context, attempt, rows, cfg, role)
    row = rows[index]
    validate_source(row, cfg, bindings)
    job_path = attempt / 'jobs' / (str(index) + '.json')
    job = read_bound(job_path, context['rows'][index]['job_sha256'], bindings)
    if job != {key: row[key] for key in PLAIN} or set(job) != set(PLAIN):
        raise ValueError('worker job is not original plain observations')
    if role == 'verify':
        # Only upstream receipt metadata is inspected, not its cohort bindings or payloads.
        complete, original_context = header(attempt, 'construct', cfg, context['source_revision'], {}, with_context=True)
        check_cohort_record(complete, rows, cfg, 'construct')
        if (len(complete['worker_reports']) != len(rows)
                or complete['candidates'][index]['sha256'] != context['rows'][index]['candidate_sha256']
                or original_context['rows'][index]['job_sha256'] != context['rows'][index]['job_sha256']):
            raise ValueError('selected candidate is not in complete construction')
    t2 = time.monotonic()
    if role == 'construct':
        import apta_coupled_time as numerical
    else:
        import apta_coupled_time_verify as numerical
    loaded_runtime = {}
    io.runtime_snapshot(loaded_runtime)
    merge_expected(bindings, loaded_runtime)
    target = safe_path(attempt / 'candidates' / (str(index) + '.json'))
    payload = None
    if role == 'verify':
        expected = context['rows'][index]['candidate_sha256']
        io.bind(target, expected, bindings)
        payload = target.read_bytes()
    t3 = time.monotonic()
    if role == 'construct':
        candidate = numerical.construct(*(job[key] for key in PLAIN))
    else:
        numerical.verify_bytes(job['beat_frames'], job['downbeat_ordinals'], job['total_frames'],
                               job['windows'], payload, expected, job['known_gaps'])
    t4 = time.monotonic()
    if role == 'construct':
        payload = json.dumps(candidate, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()
        with target.open('xb') as stream:
            stream.write(payload)
    io.bind(target, hashlib.sha256(payload).hexdigest(), bindings)
    io.runtime_recheck(bindings)
    io.recheck(bindings)
    io.check_source(Path(cfg['source_root']), context['source_revision'])
    if any(name in sys.modules for name in ('apta_1_1_coupled_time', 'apta_1_1_dense_decoder', 'mir_eval')):
        raise ValueError('scoring/legacy runner imported in numerical worker')
    if ('apta_coupled_time' in sys.modules) != (role == 'construct'):
        raise ValueError('constructor/verifier import isolation differs')
    if ('apta_coupled_time_verify' in sys.modules) != (role == 'verify'):
        raise ValueError('independent verifier import isolation differs')
    counts = {key: 0 for key in cfg['schema']['$defs']['ioCounters']['properties']}
    counts.update(manifest_reads=1, context_reads=1 + int(role == 'verify'), selected_pair_reads=2, selected_job_reads=1,
                  selected_candidate_reads=int(role == 'verify'), selected_file_bind_calls=11 + int(role == 'verify'))
    result = {'format': cfg['operational']['records']['worker'], 'role': role,
              **common(cfg, context['source_revision']), 'status': 'PASS', 'index': index,
              'source_id': row['track'], 'context_sha256': context_sha256,
              'job_sha256': context['rows'][index]['job_sha256'], 'candidate_sha256': hashlib.sha256(payload).hexdigest(),
              'reference_labels_loaded': False, 'scoring_imported': False,
              'self_peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
              'io_counters': counts, 'bindings': bindings}
    t5 = time.monotonic()
    result['wall_seconds'] = t5 - started
    result['timings'] = dict(zip(('startup_seconds', 'input_validation_seconds', 'runtime_binding_seconds',
                                  'compute_seconds', 'finalization_seconds'),
                                 (t1 - started, t2 - t1, t3 - t2, t4 - t3, t5 - t4)))
    write_record(attempt, attempt / role / (str(index) + '.json'), result, cfg, 'worker')


def phase(role, attempt, inputs_path=None, inventory_path=None):
    cfg = settings()
    attempt = destination(attempt, cfg)
    if (attempt / (role + '.context.json')).exists() or completion_path(attempt, role).exists():
        raise ValueError('phase already initialized; no replay')
    bindings = dict(cfg['bindings'])
    snapshot = {}
    revision = io.source_snapshot(Path(cfg['source_root']), snapshot)
    merge_expected(bindings, snapshot)
    prior = None
    if role != 'construct':
        prior, construct_context = header(attempt, 'construct', cfg, revision, bindings, with_context=True)
        inputs_path = construct_context['input_manifest_path']
    rows = load_input_manifest(inputs_path, cfg, bindings)
    audit_cohort(rows, cfg, bindings)
    if role != 'construct':
        prior = full_receipt(attempt, 'construct', cfg, revision, rows, bindings)
    if role == 'evaluate':
        verification = full_receipt(attempt, 'verify', cfg, revision, rows, bindings)
        if prior['candidates'] != verification['candidates']:
            raise ValueError('construction/verification candidate cohort differs')
    jobs = []
    if role == 'construct':
        for folder in ('jobs', 'candidates', role):
            (attempt / folder).mkdir()
        for index, row in enumerate(rows):
            path = attempt / 'jobs' / (str(index) + '.json')
            value = {key: row[key] for key in PLAIN}
            if io.tree_bytes((attempt,)) + io.json_bytes(value) > cfg['protocol']['limits']['max_attempt_output_bytes']:
                raise ValueError('job output cap exceeded')
            io.write_new(path, value)
            io.bind(path, io.sha(path), bindings)
            jobs.append(io.sha(path))
    else:
        jobs = [entry['job_sha256'] for entry in construct_context['rows']]
        if role == 'verify':
            (attempt / role).mkdir()
    context = {'format': cfg['operational']['records']['context'], 'role': role, **common(cfg, revision),
               'input_manifest_path': str(Path(inputs_path).resolve()), 'source_count': len(rows),
               'window_count': cfg['protocol']['input_identity']['windows'], 'source_order': [r['track'] for r in rows],
               'rows': [{'index': i, 'source_id': row['track'], 'job_sha256': jobs[i],
                         'candidate_sha256': None if role == 'construct' else prior['candidates'][i]['sha256']}
                        for i, row in enumerate(rows)], 'upstream': upstream(attempt, role)}
    context_path = attempt / (role + '.context.json')
    write_record(attempt, context_path, context, cfg, 'context')
    context_sha256 = io.sha(context_path)
    io.bind(context_path, context_sha256, bindings)
    reports, candidates = [], []
    if role == 'evaluate':
        # The old module is imported only after complete v2 construction/verification.
        import apta_1_1_coupled_time as legacy
        evaluation_bindings = dict(bindings)
        result = legacy.evaluate(attempt, Path(inventory_path), cfg, prior, rows, revision, evaluation_bindings)
        merge_expected(bindings, evaluation_bindings)
        if result['source_revision'] != revision:
            raise ValueError('evaluation source changed')
        candidates = prior['candidates']
    else:
        runtime = runtime_bindings()
        for index, row in enumerate(rows):
            resources = io.supervise(command('_worker', role, index, '--attempt', attempt,
                                              '--context-sha256', context_sha256),
                                      cfg['protocol']['limits']['max_source_seconds'],
                                      cfg['protocol']['limits']['max_process_rss_kib'],
                                      cfg['protocol']['limits']['max_attempt_output_bytes'],
                                      (attempt,), attempt / role / (str(index) + '.worker'), group=False)
            if resources['status'] != 'PASS':
                raise ValueError('source supervision invalid')
            report_path = attempt / role / (str(index) + '.json')
            report = read_bound(report_path, io.sha(report_path), bindings)
            check_worker_result(report, context, index, role, cfg, attempt, row, runtime)
            candidate = attempt / 'candidates' / (str(index) + '.json')
            io.bind(candidate, report['candidate_sha256'], bindings)
            candidates.append({'index': index, 'source_id': row['track'], 'sha256': report['candidate_sha256']})
            reports.append({'index': index, 'source_id': row['track'], 'sha256': io.sha(report_path)})
        io.recheck(runtime)
        merge_expected(bindings, runtime)
    io.recheck(bindings)
    io.check_source(Path(cfg['source_root']), revision)
    result = {'format': cfg['operational']['records']['completion'], 'role': role, **common(cfg, revision),
              'status': 'PASS', 'context_sha256': context_sha256, 'source_count': len(rows),
              'window_count': cfg['protocol']['input_identity']['windows'], 'reference_labels_loaded': role == 'evaluate',
              'candidates': candidates, 'worker_reports': reports, 'upstream': context['upstream'],
              'evaluation_report_sha256': io.sha(attempt / 'evaluate.completion.json') if role == 'evaluate' else None,
              'bindings': bindings}
    write_record(attempt, completion_path(attempt, role), result, cfg, 'completion')


def dispatch(role, attempt, inputs_path=None, inventory_path=None):
    cfg = settings()
    attempt = destination(attempt, cfg)
    if role == 'construct':
        io.disjoint_output(attempt, (safe_path(inputs_path), Path(cfg['source_root'])))
        attempt.mkdir(parents=True)
    elif not attempt.is_dir() or (attempt / (role + '.phase.stdout')).exists():
        raise ValueError('phase absent attempt or already started')
    args = ['_phase', role, '--attempt', str(attempt)]
    if inputs_path is not None:
        args += ['--inputs', str(safe_path(inputs_path))]
    if inventory_path is not None:
        args += ['--inventory', str(safe_path(inventory_path))]
    limits = cfg['protocol']['limits']
    try:
        resource = io.supervise(command(*args), limits['max_phase_seconds'], limits['max_process_rss_kib'],
                                 limits['max_attempt_output_bytes'], (attempt,), attempt / (role + '.phase'))
        if resource['status'] != 'PASS':
            raise ValueError('phase supervision invalid')
        path = completion_path(attempt, role)
        report = read_bound(path, io.sha(path), {})
        validate_record(report, cfg, 'completion')
        io.check_source(Path(cfg['source_root']), report['source_revision'])
        result = {'format': cfg['operational']['records']['supervision'], 'role': role,
                  **common(cfg, report['source_revision']), 'status': 'PASS', 'context_sha256': report['context_sha256'],
                  'completion_sha256': io.sha(path), 'resources_sha256': io.sha(attempt / (role + '.phase.resources.json'))}
        write_record(attempt, attempt / (role + '.v2.supervised.json'), result, cfg, 'supervision')
    except BaseException as error:
        path = attempt / (role + '.INVALID.json')
        if not path.exists():
            io.write_new(path, {'format': cfg['operational']['records']['invalid'], 'role': role, 'status': 'INVALID',
                               'operational_registration_sha256': cfg['operational_registration_sha256'],
                               'attempt_path_sha256': cfg['attempt_path_sha256'],
                               'context_sha256': io.sha(attempt / (role + '.context.json')) if (attempt / (role + '.context.json')).is_file() else None,
                               'error': str(error), 'musical_verdict_valid': False})
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    for role in ('construct', 'verify', 'evaluate'):
        part = sub.add_parser(role)
        part.add_argument('--attempt', type=Path, required=True)
        if role == 'construct':
            part.add_argument('--inputs', type=Path, required=True)
        if role == 'evaluate':
            part.add_argument('--inventory', type=Path, required=True)
    phase_parser = sub.add_parser('_phase')
    phase_parser.add_argument('role', choices=('construct', 'verify', 'evaluate'))
    phase_parser.add_argument('--attempt', type=Path, required=True)
    phase_parser.add_argument('--inputs', type=Path)
    phase_parser.add_argument('--inventory', type=Path)
    worker_parser = sub.add_parser('_worker')
    worker_parser.add_argument('role', choices=('construct', 'verify'))
    worker_parser.add_argument('index', type=int)
    worker_parser.add_argument('--attempt', type=Path, required=True)
    worker_parser.add_argument('--context-sha256', required=True)
    args = parser.parse_args()
    if args.action == '_worker':
        source_worker(args.role, args.attempt, args.index, args.context_sha256)
    elif args.action == '_phase':
        phase(args.role, args.attempt, args.inputs, args.inventory)
    else:
        dispatch(args.action, args.attempt, getattr(args, 'inputs', None), getattr(args, 'inventory', None))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, TypeError, IndexError) as error:
        print(json.dumps({'status': 'INVALID', 'error': str(error)}), file=sys.stderr)
        sys.exit(2)
