#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""TEST ONLY generated identities/instrumentation. Production never imports this."""
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time_v2 as runner
import apta_dense_runner_support as io

settings_path = str(Path(sys.argv[1]).resolve())
cfg = json.loads(Path(settings_path).read_text())
runner.settings = lambda: cfg
runner.command = lambda *args: [sys.executable, str(Path(__file__).resolve()), settings_path, *map(str, args)]
original_phase = runner.phase
original_supervise = io.supervise


def generated_phase(role, *args, **kwargs):
    if role == 'evaluate' and cfg['test_metric'] == 'stub':
        import apta_1_1_neural_comparison as comparison
        comparison.event_metrics = lambda truth, events: {
            key: float(events['beat_frames'] == truth['beat_frames'][:-1])
            for key in ('beat_f1', 'downbeat_f1', 'beat_cmlt', 'downbeat_cmlt')}
    return original_phase(role, *args, **kwargs)


def generated_supervise(command, seconds, rss_kib, byte_limit, *args, **kwargs):
    if '_phase' in command:
        limits = cfg.get('test_phase_limits', {})
        seconds = limits.get('max_phase_seconds', seconds)
        rss_kib = limits.get('max_process_rss_kib', rss_kib)
        byte_limit = limits.get('max_attempt_output_bytes', byte_limit)
    return original_supervise(command, seconds, rss_kib, byte_limit, *args, **kwargs)


runner.phase = generated_phase
io.supervise = generated_supervise
sys.argv = [sys.argv[0], *sys.argv[2:]]
worker = len(sys.argv) > 1 and sys.argv[1] == '_worker'
phase = len(sys.argv) > 1 and sys.argv[1] == '_phase'
trace = []
if phase and cfg.get('test_boundary_trace'):
    snapshot, audit_cohort, recheck, write_record = io.source_snapshot, runner.audit_cohort, io.recheck, runner.write_record

    def traced_snapshot(*args):
        trace.append('snapshot')
        return snapshot(*args)

    def traced_audit(*args):
        trace.append('cohort_audit')
        return audit_cohort(*args)

    def traced_recheck(bindings):
        if str(Path(cfg['source_root']) / 'fixture.txt') in bindings:
            trace.append('global_recheck')
        return recheck(bindings)

    def traced_write(*args):
        result = write_record(*args)
        trace.append(args[-1] + '_written')
        return result

    io.source_snapshot, runner.audit_cohort, io.recheck, runner.write_record = traced_snapshot, traced_audit, traced_recheck, traced_write
opened, bound = [], []
access_counts = dict.fromkeys(('open_calls', 'bind_calls', 'selected_observation_opens',
                               'unselected_source_file_opens', 'unselected_source_file_binds',
                               'tracked_fixture_file_binds'), 0)
summary_access = cfg.get('test_access_summary', False)
if worker:
    old_open, old_bind = Path.open, io.bind
    index = int(sys.argv[3])
    attempt = Path(sys.argv[sys.argv.index('--attempt') + 1])
    policy = cfg.get('test_access_policy', {})
    selected = set(policy.get('sources', [{}])[index].values()) if summary_access else set()
    selected |= {str(attempt / 'jobs' / (str(index) + '.json')),
                 str(attempt / 'candidates' / (str(index) + '.json'))}

    def count_access(path, kind):
        resolved = path.resolve()
        text = str(resolved)
        access_counts[kind + '_calls'] += 1
        roots = [Path(value) for value in policy.get('source_roots', [])]
        roots += [attempt / 'jobs', attempt / 'candidates']
        if any(resolved.is_relative_to(root) for root in roots) and text not in selected:
            access_counts['unselected_source_file_' + ('opens' if kind == 'open' else 'binds')] += 1
        if kind == 'open' and text in set(policy.get('sources', [{}])[index].values()):
            access_counts['selected_observation_opens'] += 1
        if kind == 'bind' and resolved.is_relative_to(Path(cfg['source_root'])):
            access_counts['tracked_fixture_file_binds'] += 1

    def opened_file(path, *args, **kwargs):
        if summary_access:
            count_access(path, 'open')
        else:
            opened.append(str(path.resolve()))
        return old_open(path, *args, **kwargs)

    def bound_file(path, *args, **kwargs):
        if summary_access:
            count_access(Path(path), 'bind')
        else:
            bound.append(str(Path(path).resolve()))
        return old_bind(path, *args, **kwargs)

    Path.open, io.bind = opened_file, bound_file
    if cfg.get('test_worker_memory_mb'):
        allocation = bytearray(cfg['test_worker_memory_mb'] * 1024 * 1024)
        time.sleep(.5)
    if cfg.get('test_worker_output_bytes'):
        attempt = Path(sys.argv[sys.argv.index('--attempt') + 1])
        (attempt / 'generated-overflow.bin').write_bytes(b'x' * cfg['test_worker_output_bytes'])
    if cfg.get('test_worker_delay'):
        time.sleep(cfg['test_worker_delay'])
try:
    result = runner.main()
    if phase and cfg.get('test_boundary_trace'):
        attempt = Path(sys.argv[sys.argv.index('--attempt') + 1])
        io.write_new(attempt / (sys.argv[2] + '.boundary-trace.json'), trace)
    if worker:
        role, index = sys.argv[2], int(sys.argv[3])
        attempt = Path(sys.argv[sys.argv.index('--attempt') + 1])
        Path.open, io.bind = old_open, old_bind
        io.write_new(attempt / role / (str(index) + '.access.json'),
                     {'format': 'generated-access-counts-1', **access_counts} if summary_access
                     else {'opened': opened, 'bound': bound})
        mutation = cfg.get('test_mutation', {})
        if mutation.get('role', 'construct') == role and mutation.get('after_index') == index:
            path = Path(mutation['path'])
            if mutation['kind'] == 'append':
                with path.open('ab') as stream:
                    stream.write(b'\n ')
            elif mutation['kind'] == 'poison_report':
                report_path = attempt / role / (str(index) + '.json')
                value = json.loads(report_path.read_text())
                value['bindings'][str(path)] = io.sha(path)
                report_path.write_text(json.dumps(value, sort_keys=True, indent=2) + '\n')
        if cfg.get('test_live_descendant'):
            import subprocess
            subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(5)'])
            time.sleep(.1)
    sys.exit(result)
except (ValueError, OSError, KeyError, TypeError, IndexError) as error:
    print(json.dumps({'status': 'INVALID', 'error': str(error)}), file=sys.stderr)
    sys.exit(2)
