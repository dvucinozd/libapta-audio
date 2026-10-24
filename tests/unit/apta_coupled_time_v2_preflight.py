#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generated-only R4 campaign. No event/audio/label/manifest input CLI exists."""
from collections import Counter
import hashlib
import importlib.metadata
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time_v2 as runner
import apta_dense_runner_support as io

BOOTSTRAP = ROOT / 'tests/unit/apta_coupled_time_v2_runner_bootstrap.py'
METRICS = ('beat_f1', 'downbeat_f1', 'beat_cmlt', 'downbeat_cmlt')
TOTAL = 1440000


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def source_inventory(root, revision):
    raw = subprocess.check_output(['git', '-C', str(root), 'ls-tree', '-rlz', revision])
    result = []
    for record in raw.split(b'\0'):
        if not record:
            continue
        metadata, name = record.split(b'\t', 1)
        mode, kind, digest, size = metadata.decode().split()
        if mode not in ('100644', '100755') or kind != 'blob':
            raise ValueError('only regular tracked source files supported')
        result.append({'path': name.decode(), 'blob': digest, 'bytes': int(size)})
    return result


def extract_regular_archive(archive, destination):
    destination = runner.safe_path(destination)
    destination.mkdir(parents=True, exist_ok=False)
    seen = set()
    with tarfile.open(archive, 'r:') as stream:
        for member in stream:
            parts = PurePosixPath(member.name).parts
            if (not parts or member.name.startswith('/') or any(p in ('.', '..', '.git') for p in parts)
                    or member.name in seen or not (member.isfile() or member.isdir())):
                raise ValueError('unsafe or duplicate archive member')
            seen.add(member.name)
            path = destination.joinpath(*parts)
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                with stream.extractfile(member) as source, path.open('xb') as target:
                    shutil.copyfileobj(source, target)


def archive_source(root, destination):
    revision = git(root, 'rev-parse', 'HEAD')
    if git(root, 'status', '--porcelain'):
        raise ValueError('repair source is not clean')
    inventory = source_inventory(root, revision)
    if len(inventory) < 1024:
        raise ValueError('full tracked source fixture requires at least1024 files')
    destination = runner.safe_path(destination)
    if destination.exists():
        raise ValueError('source fixture destination exists')
    archive = destination.parent / (destination.name + '.tar')
    destination.parent.mkdir(parents=True, exist_ok=True)
    if archive.exists():
        raise ValueError('source archive already exists')
    subprocess.run(['git', '-C', str(root), 'archive', '--format=tar', '--output', str(archive), revision], check=True)
    extract_regular_archive(archive, destination)
    for row in inventory:
        value = (destination / row['path']).read_bytes()
        digest = hashlib.sha1(b'blob ' + str(len(value)).encode() + b'\0' + value).hexdigest()
        if len(value) != row['bytes'] or digest != row['blob']:
            raise ValueError('archive differs from repair commit')
    actual = {str(path.relative_to(destination)) for path in destination.rglob('*') if path.is_file()}
    if actual != {row['path'] for row in inventory}:
        raise ValueError('archive file set differs')
    for args in (('init',), ('config', 'core.autocrlf', 'false'), ('config', 'core.filemode', 'false'),
                 ('add', '--force', '--all'), ('-c', 'user.name=Generated R4', '-c', 'user.email=generated@example.invalid',
                                  'commit', '-m', 'generated full repair archive')):
        subprocess.run(['git', '-C', str(destination), *args], capture_output=True, check=True)
    if git(destination, 'status', '--porcelain'):
        raise ValueError('generated source checkout is dirty')
    tracked = subprocess.check_output(['git', '-C', str(destination), 'ls-files', '-z']).decode().split('\0')
    if set(filter(None, tracked)) != {row['path'] for row in inventory}:
        raise ValueError('fixture tracked file set differs from archive')
    for row in inventory:
        value = (destination / row['path']).read_bytes()
        if hashlib.sha1(b'blob ' + str(len(value)).encode() + b'\0' + value).hexdigest() != row['blob']:
            raise ValueError('archive bytes changed during generated Git initialization')
    io.write_new(archive.with_suffix('.inventory.json'), inventory)
    return {'repair_revision': revision, 'fixture_revision': git(destination, 'rev-parse', 'HEAD'),
            'tracked_count': len(inventory), 'tracked_bytes': sum(row['bytes'] for row in inventory),
            'archive_sha256': io.sha(archive), 'inventory_sha256': io.sha(archive.with_suffix('.inventory.json'))}


def filesystem_class(path, mountinfo=None):
    path = runner.safe_path(path).resolve()
    mountinfo = Path('/proc/self/mountinfo').read_text() if mountinfo is None else mountinfo
    candidates = []
    for index, line in enumerate(mountinfo.splitlines()):
        left, right = line.split(' - ', 1)
        point = Path(left.split()[4].replace('\\040', ' '))
        if path.is_relative_to(point):
            candidates.append((len(point.parts), index, point, right.split()[0]))
    if not candidates:
        raise ValueError('unresolved filesystem class')
    _, _, point, kind = max(candidates)
    if kind in ('9p', 'drvfs') and str(point).startswith('/mnt/'):
        return 'windows-mounted'
    if kind in ('ext4', 'xfs', 'btrfs', 'tmpfs'):
        return 'linux'
    raise ValueError('unregistered filesystem type')


def generated_source(index):
    """Fixed public arithmetic, no private-data-derived fixture values."""
    n = 148 if index == 1 else 20 + (17 * index) % 129
    family = ('constant', 'jitter', 'ramp', 'boundary')[index % 4]
    period = (TOTAL - 19200) // (n - 1)
    truth = [9600 + period * i for i in range(n)]
    if family == 'ramp':
        truth = [9600]
        for i in range(n - 1):
            truth.append(truth[-1] + period - 10 * (n - 2) + 20 * i)
    observed = [value + (-160 if i % 2 == 0 else 160) for i, value in enumerate(truth)] if family == 'jitter' else list(truth)
    ends = [240000 * i for i in range(1, 7)]
    if family == 'boundary':
        truth = [1000 + 24000 * i for i in range(60)]
        observed = [value + (320 if i == 8 else 0) for i, value in enumerate(truth)]
        ends[0] = 193319  # Published CT02 crossing fixture, extended to six windows.
    windows = [{'first_frame': start, 'end_frame': end} for start, end in zip([0, *ends[:-1]], ends)]
    ordinals = list(range(0, len(observed), 4))
    events = {'beat_frames': observed, 'downbeat_frames': [observed[i] for i in ordinals]}
    reference = {'track': 'generated-r4-%03d' % index, 'requested_frames': TOTAL,
                 'beat_frames': truth, 'downbeat_frames': [truth[i] for i in ordinals],
                 'meter_numerator': 4, 'meter_denominator': 4, 'genre': family}
    return events, reference, windows, ordinals


def component_bindings():
    op = json.loads((ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-PROTOCOL.json').read_text())
    names = set(op['immutable_component_sha256']) | set(op['future_preflight_components'])
    names |= {'tests/unit/apta_1_1_coupled_time_runner.py', 'tests/unit/apta_1_1_dense_decoder_runner.py',
              'tests/unit/apta_1_1_coupled_time.py', 'tests/unit/apta_1_1_coupled_time_verify.py',
              'tests/unit/apta_1_1_coupled_time_v2_preflight.py'}
    values = {name: io.sha(ROOT / name) for name in sorted(names)}
    if any(values[name] != digest for name, digest in op['immutable_component_sha256'].items()):
        raise ValueError('immutable scientific component changed')
    return op, values


def generate_fixture(base, source_root, count=100):
    """Create generated observations/labels and canonical baseline; no candidate."""
    if type(count) is not int or not 1 <= count <= 100:
        raise ValueError('invalid generated source count')
    import apta_1_1_neural_reference as ref
    import apta_1_1_neural_comparison as comparison
    import apta_1_1_learned_piecewise as previous
    base = runner.safe_path(base)
    io.external(base, ROOT)
    base.mkdir(parents=True, exist_ok=False)
    for folder in ('events', 'capture', 'baseline'):
        (base / folder).mkdir()
    op, components = component_bindings()
    p = json.loads((ROOT / 'docs/rust/COUPLED-TIME-DEVELOPMENT-PROTOCOL.json').read_text())
    rows, truths, details = [], [], []
    input_bindings, bindings = {}, {}
    totals = {'coarse': Counter(), 'beat_this': Counter()}
    diagnostic = {'nominal': Counter(), 'actual': Counter()}
    macros, genres = Counter(), {}
    for index in range(count):
        events, truth, windows, ordinals = generated_source(index)
        track = truth['track']
        audio_sha = hashlib.sha256(('generated-no-audio:' + track).encode()).hexdigest()
        event_path, capture_path = base / 'events' / (track + '.json'), base / 'capture' / (track + '.json')
        io.write_new(event_path, {'track': track, 'audio_sha256': audio_sha, 'events': events})
        io.write_new(capture_path, {'track': track, 'audio_sha256': audio_sha, 'events': events, 'geometry': {'windows': windows}})
        controls = [ref.project(events['beat_frames'], events['downbeat_frames'], w['first_frame'], w['end_frame']) for w in windows]
        scores = [ref.annotated.score_window(w, truth, 48000) for w in controls]
        metrics = comparison.event_metrics(truth, events)
        coarse_path, score_path = base / 'baseline' / (track + '-coarse.json'), base / 'baseline' / (track + '.json')
        io.write_new(coarse_path, {'windows': controls, 'requested_frames': TOTAL, 'sample_rate': 48000})
        io.write_new(score_path, {'track': track, 'coarse': scores,
                                'models': {'beat_this': {'event_metrics': metrics,
                                  'windows': [{'candidate': w, 'score': s} for w, s in zip(controls, scores)]}}})
        row = {'track': track, 'events_path': str(event_path), 'events_sha256': io.sha(event_path),
               'capture_record_path': str(capture_path), 'capture_record_sha256': io.sha(capture_path),
               'source_audio_sha256': audio_sha, 'total_frames': TOTAL, 'windows': windows,
               'beat_frames': events['beat_frames'], 'downbeat_ordinals': ordinals, 'known_gaps': []}
        for path in (event_path, capture_path):
            input_bindings[str(path)] = io.sha(path)
        for score in scores:
            totals['coarse'].update(score)
            totals['beat_this'].update(score)
        points = previous.control_points(controls, events['beat_frames'])
        for mode in diagnostic:
            for score in previous.score_point_windows(points, truth, windows, mode == 'actual'):
                diagnostic[mode].update(score)
        macros.update(metrics)
        genre = genres.setdefault(truth['genre'], {'tracks': 0, 'means_sum': Counter()})
        genre['tracks'] += 1
        genre['means_sum'].update(metrics)
        row.update(coarse_path=str(coarse_path), coarse_sha256=io.sha(coarse_path),
                   baseline_score_path=str(score_path), baseline_score_sha256=io.sha(score_path))
        bindings[str(coarse_path)], bindings[str(score_path)] = io.sha(coarse_path), io.sha(score_path)
        rows.append(row)
        truths.append(truth)
        details.append({'track': track, 'sha256': io.sha(score_path)})
    means = {key: macros[key] / count for key in METRICS}
    oldreport = base / 'baseline-report.json'
    io.write_new(oldreport, {'details': details, 'totals': totals, 'event_means': {'beat_this': means},
                            'by_genre': {name: {'beat_this': {'tracks': g['tracks'],
                              'event_means': {key: g['means_sum'][key] / g['tracks'] for key in METRICS}}}
                              for name, g in genres.items()}})
    labels = base / 'labels.json'
    io.write_new(labels, {'holdout_performances_opened': False, 'tracks': truths})
    freeze = base / 'generated-original-freeze.json'
    generated_runtime = {str(ROOT / name): digest for name, digest in components.items()}
    io.runtime_snapshot(generated_runtime)
    io.write_new(freeze, {'generated_only': True, 'bindings': generated_runtime})
    bindings.update(input_bindings)
    for path in (oldreport, freeze):
        bindings[str(path)] = io.sha(path)
    inventory = base / 'inventory.json'
    io.write_new(inventory, {'sources': rows, 'bindings': bindings, 'labels_manifest_path': str(labels)})
    inputs = base / 'inputs.json'
    io.write_new(inputs, {'format': 'libapta-coupled-time-label-free-inputs-1', 'scope': 'GENERATED_R4_ONLY',
                         'registration_parent_revision': 'generated-only', 'source_sample_rate': 48000,
                         'sources': [{key: row[key] for key in runner.SOURCE_KEYS} for row in rows],
                         'bindings': input_bindings, 'input_order': 'fixed arithmetic index',
                         'protocol_sha256': p['synthetic_protocol_sha256'], 'candidate_constructed': False, 'execution_enabled': False})
    p['input_identity'].update(sources=count, windows=count * 6,
        reference_complete_windows=diagnostic['nominal']['reference_complete_windows'],
        reference_incomplete_windows=diagnostic['nominal']['reference_incomplete_windows'],
        ct03_label_free_inputs_sha256=io.sha(inputs), ct03_private_inventory_sha256=io.sha(inventory),
        original_freeze_sha256=io.sha(freeze), prepared_manifest_sha256=io.sha(labels), legacy_scoring_report_sha256=io.sha(oldreport))
    p['baseline'].update(ols_nominal_joint_windows=diagnostic['nominal']['joint_windows'],
        ols_nominal_complete_sequence_windows=diagnostic['nominal']['complete_sequence_windows'],
        ols_actual_joint_windows=diagnostic['actual']['joint_windows'],
        ols_actual_complete_sequence_windows=diagnostic['actual']['complete_sequence_windows'],
        coarse_joint_windows=totals['coarse']['confirmed_joint_windows'], original_coarse_to_ols_joint_breaks=0)
    p['baseline_event_means'] = means
    initial = {str(ROOT / name): digest for name, digest in components.items()}
    for name, digest in op['binding'].items():
        io.bind(ROOT / 'docs/rust' / name, digest, initial)
    for name in ('COUPLED-TIME-RUNNER-V2-PROTOCOL.json', 'COUPLED-TIME-RUNNER-V2-R3-20261024.json'):
        io.bind(ROOT / 'docs/rust' / name, io.sha(ROOT / 'docs/rust' / name), initial)
    cfg = {'protocol': p, 'operational': op, 'schema': json.loads((ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-SCHEMA.json').read_text()),
           'source_root': str(source_root), 'registration_sha256': runner.REGISTRATION_SHA256,
           'operational_registration_sha256': runner.OPERATIONAL_SHA256,
           'authorization_sha256': hashlib.sha256(b'GENERATED_R4_ONLY').hexdigest(),
           'bindings': initial, 'test_metric': 'canonical', 'test_access_summary': True,
           'test_access_policy': {'source_roots': [str(base / 'events'), str(base / 'capture')],
                                 'sources': [{key: row[key] for key in ('events_path', 'capture_record_path')} for row in rows]}}
    return cfg, inputs, inventory, labels


def run(output, source_fixture):
    output, source_fixture = runner.safe_path(output), runner.safe_path(source_fixture)
    io.external(output, ROOT)
    io.external(source_fixture, ROOT)
    if ROOT.is_relative_to(output) or ROOT.is_relative_to(source_fixture):
        raise ValueError('fixture/output encloses real source')
    if output.exists() or source_fixture.exists():
        raise ValueError('new exclusive destinations required')
    storage = {'source': filesystem_class(source_fixture), 'observations': filesystem_class(output), 'output': filesystem_class(output)}
    if storage != {'source': 'windows-mounted', 'observations': 'linux', 'output': 'linux'}:
        raise ValueError('registered storage topology required')
    revision = git(ROOT, 'rev-parse', 'HEAD')
    if git(ROOT, 'status', '--porcelain') or git(ROOT, 'ls-remote', 'origin', 'refs/heads/rust-rewrite').split()[0] != revision:
        raise ValueError('clean pushed repair revision required')
    prep_path = ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-R4-PREP-20261024.json'
    preparation = json.loads(prep_path.read_text())
    op, components = component_bindings()
    if preparation['status'] != 'GENERATED_DRIVER_CHECKS_PASS' or preparation['component_sha256'] != components:
        raise ValueError('stale/missing generated driver preparation')
    expected_runtime = json.loads((ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-R3-20261024.json').read_text())['runtime']
    packages = {name: importlib.metadata.version(name) for name in ('numpy', 'scipy', 'mir_eval')}
    if (sys.version.split()[0] != expected_runtime['python'] or packages != expected_runtime['packages']
            or io.sha(Path(sys.executable).resolve()) != expected_runtime['canonical_interpreter_sha256']):
        raise ValueError('retained canonical experiment runtime required')
    output.mkdir(parents=True, exist_ok=False)
    phases = {}
    started = time.monotonic()
    try:
        archive = archive_source(ROOT, source_fixture)
        cfg, inputs, inventory, labels = generate_fixture(output / 'generated', source_fixture)
        if len(json.loads(inputs.read_text())['sources']) != 100:
            raise ValueError('full100 generated cohort required')
        attempt = output / 'attempt'
        cfg['attempt_path_sha256'] = hashlib.sha256(str(attempt.resolve()).encode()).hexdigest()
        cfg_path = output / 'settings.generated.json'
        io.write_new(cfg_path, cfg)
        hidden = labels.with_suffix('.withheld')
        labels.rename(hidden)
        preparation_seconds = time.monotonic() - started
        for role, extra in (('construct', ['--inputs', str(inputs)]), ('verify', []), ('evaluate', ['--inventory', str(inventory)])):
            if role == 'evaluate':
                hidden.rename(labels)
            if role != 'evaluate' and labels.exists():
                raise ValueError('generated labels not withheld')
            if git(ROOT, 'rev-parse', 'HEAD') != revision or git(ROOT, 'status', '--porcelain'):
                raise ValueError('actual repair source changed')
            print(json.dumps({'phase': role, 'state': 'STARTED'}), flush=True)
            phase_start = time.monotonic()
            with (output / (role + '.cli.stdout')).open('xb') as stdout, (output / (role + '.cli.stderr')).open('xb') as stderr:
                result = subprocess.run([sys.executable, str(BOOTSTRAP), str(cfg_path), role,
                                         '--attempt', str(attempt), *extra], cwd=ROOT, stdout=stdout, stderr=stderr)
            phases[role] = {'returncode': result.returncode, 'cli_wall_seconds': time.monotonic() - phase_start}
            if result.returncode:
                raise ValueError(role + ' failed; no automatic retry')
            resources = json.loads((attempt / (role + '.phase.resources.json')).read_text())
            phases[role]['resources'] = {key: resources[key] for key in ('status', 'wall_seconds', 'max_observed_process_rss_kib', 'process_group_monitored')}
            print(json.dumps({'phase': role, 'state': 'COMPLETED', **phases[role]['resources']}), flush=True)
        for name, digest in components.items():
            if io.sha(ROOT / name) != digest:
                raise ValueError('component changed during R4')
        io.check_source(ROOT, revision)
        if git(ROOT, 'ls-remote', 'origin', 'refs/heads/rust-rewrite').split()[0] != revision:
            raise ValueError('remote revision changed')
        scientific = json.loads((attempt / 'evaluate.completion.json').read_text())
        access = Counter()
        stages = {role: Counter() for role in ('construct', 'verify')}
        for role in stages:
            for index in range(100):
                report = json.loads((attempt / role / (str(index) + '.json')).read_text())
                stages[role].update(report['timings'])
                value = json.loads((attempt / role / (str(index) + '.access.json')).read_text())
                if value.pop('format') != 'generated-access-counts-1':
                    raise ValueError('bounded access instrumentation required')
                access.update(value)
        if any(access[key] for key in ('unselected_source_file_opens', 'unselected_source_file_binds', 'tracked_fixture_file_binds')):
            raise ValueError('selected-source access scope violated')
        if access['selected_observation_opens'] != 1600:
            raise ValueError('selected observation audit count differs')
        result = {'format': 'libapta-coupled-time-r4-generated-attempt-1', 'status': 'GENERATED_OPERATIONAL_PREFLIGHT_PASS',
                  'source_revision': revision, 'archive': archive, 'storage': storage,
                  'sources': 100, 'windows': 600, 'preparation_seconds': preparation_seconds,
                  'phases': phases, 'stage_seconds_sums': stages, 'access_counts': access,
                  'scientific_verdict': scientific['status'], 'scientific_gates': scientific['gates'],
                  'totals': scientific['totals'], 'paired': scientific['paired'],
                  'old_event_means': scientific['old_event_means'], 'candidate_event_means': scientific['candidate_event_means'],
                  'by_genre': {name: {'sources': row['sources'], **{kind: {key: value / row['sources'] for key, value in row[kind].items()}
                               for kind in ('legacy', 'candidate')}} for name, row in scientific['by_genre'].items()},
                  'component_sha256': components, 'real_private_inputs_read': False, 'automatic_retry': False,
                  'runtime': {'python': sys.version.split()[0], 'packages': packages,
                              'interpreter_sha256': io.sha(Path(sys.executable).resolve())},
                  'preparation_receipt_sha256': io.sha(prep_path), 'attempt_bytes': io.tree_bytes((attempt,))}
        result['artifact_sha256'] = {path.name: io.sha(path) for path in attempt.glob('*.json')}
        evidence_files = [path for path in output.iterdir() if path.is_file()]
        result['driver_evidence_bytes_before_result'] = sum(path.stat().st_size for path in evidence_files)
        if result['attempt_bytes'] + result['driver_evidence_bytes_before_result'] + io.json_bytes(result) > op['limits']['max_attempt_output_bytes']:
            raise ValueError('whole attempt plus driver evidence cap exceeded')
        io.write_new(output / 'r4.result.json', result)
        print(json.dumps({'status': result['status'], 'attempt_bytes': result['attempt_bytes']}), flush=True)
        return result
    except BaseException as error:
        io.write_new(output / 'R4.INVALID.json', {'status': 'INVALID', 'error': str(error), 'phases': phases,
                                                'real_private_inputs_read': False, 'automatic_retry': False,
                                                'musical_verdict_valid': False})
        raise


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--source-fixture', type=Path, required=True)
    args = parser.parse_args()
    run(args.output, args.source_fixture)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({'status': 'INVALID', 'error': str(error)}), file=sys.stderr)
        sys.exit(2)
