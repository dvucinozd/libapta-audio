#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""HS06 fixed host supervision and separate streaming numeric checker (Linux)."""
import argparse
import hashlib
import importlib.util
import io as binary_io
import json
import math
from pathlib import Path
import resource
import struct
import subprocess
import sys
import time
import apta_dense_runner_support as io
# Reuse frozen resource-refusal probes/strict JSON, without changing their source.
from apta_compact_neural_synthetic import enforcement, strict_json

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / 'docs/rust/PRODUCT-HOST-PROBABILITY-PROTOCOL.json'
EXPECTED_PROTOCOL = '50082ad1d7a9fccac4b939cc42f7f4709b78eff0735bdc8ef221b07dc12a5650'
REFERENCE_ROOT = ROOT / 'docs/rust/fixtures/product-host-probability-hs06'
spec = importlib.util.spec_from_file_location('hs06_scalar_reference', REFERENCE_ROOT / 'scalar_reference.py')
reference = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = reference
spec.loader.exec_module(reference)
CASES = ['fixtures', 'full-mixed', 'full-no-support', 'cancel']


class SyntheticFailure(ValueError):
    """Numeric/semantic failure, distinct from incomplete or corrupt transport."""


def require(condition, message):
    if not condition:
        raise SyntheticFailure(message)


def protocol():
    if io.sha(PROTOCOL) != EXPECTED_PROTOCOL:
        raise ValueError('HS06 registration changed')
    p = strict_json(PROTOCOL.read_text())
    for name, identity in p['bindings'].items():
        path = ROOT / name
        if path.stat().st_size != identity['bytes'] or io.sha(path) != identity['sha256']:
            raise ValueError('bound registration/control changed: ' + name)
    return p


class PipeChecker:
    def __init__(self, stream, maximum):
        self.stream, self.maximum = stream, maximum
        self.digest, self.count, self.ordinal = hashlib.sha256(), 0, 0
        self.reference_digest = hashlib.sha256()
        self.wait_seconds, self.check_seconds = 0., 0.
        self.receipt = {'status': 'INCOMPLETE', 'last_complete_record': None}

    def read(self, size):
        if not 0 <= size <= 65536 or self.count + size > self.maximum:
            raise ValueError('transport bound exceeded')
        start, raw = time.monotonic(), bytearray()
        while len(raw) < size:
            chunk = self.stream.read(size - len(raw))
            if not chunk:
                raise ValueError('truncated record')
            raw.extend(chunk)
            self.count += len(chunk)
            self.digest.update(chunk)
        self.wait_seconds += time.monotonic() - start
        return bytes(raw)

    def complete(self, kind, index, row=None):
        self.ordinal += 1
        self.receipt['last_complete_record'] = dict(ordinal=self.ordinal, kind=kind, index=index, row=row)

    def row(self, index, row, probabilities, frozen=None):
        if self.read(7) != struct.pack('<BHI', 1, index, row):
            raise ValueError('row tag/index/order')
        data = self.read(36)
        require(data[:12] == struct.pack('<3f', *probabilities), 'retained input bits changed')
        values = struct.unpack('<3d', data[12:])
        expected = reference.density(probabilities)
        self.reference_digest.update(struct.pack('<3d', *expected))
        start = time.monotonic()
        for column, (x, y) in enumerate(zip(values, expected)):
            targets = [y]
            if frozen is not None:
                targets.append(-math.inf if frozen[column] == '-Inf' else frozen[column])
            for target in targets:
                valid = x == target == -math.inf if target == -math.inf else math.isfinite(x) and -1024 <= x <= 0 and abs(x - target) <= 4e-13
                if not valid:
                    self.receipt['failure_witness'] = dict(index=index, row=row, column=column, got=repr(x), expected=repr(target))
                    raise SyntheticFailure('density scalar/zero mismatch')
        self.check_seconds += time.monotonic() - start
        self.complete('row', index, row)
        return data

    def events(self, index, expected):
        if self.read(5) != struct.pack('<BHH', 2, index, len(expected)):
            raise ValueError('event record identity/count/order')
        raw = self.read(len(expected) * 8)
        actual = list(struct.iter_unpack('<IBBBB', raw))
        require(actual == expected, 'global event/beat/class/reserved mismatch')
        self.complete('events', index)

    def trailer(self):
        if self.read(1) != b'\x03':
            raise ValueError('trailer tag/order')
        length = struct.unpack('<I', self.read(4))[0]
        if length > 65536:
            raise ValueError('trailer cap')
        data = self.read(length)
        self.receipt.update(trailer_hex=data.hex(), trailer_utf8=data.decode('utf-8'))
        extra = self.stream.read(1)
        if extra:
            self.count += 1
            self.digest.update(extra)
            self.receipt['extra_byte_hex'] = extra.hex()
            raise ValueError('extra byte after trailer')
        self.complete('trailer', 0)
        return strict_json(data)


def source_row(case, index):
    if 'input' in case:
        # Only the public short fixture; largest input is2724B.
        with (REFERENCE_ROOT / case['input']).open('rb') as stream:
            stream.seek(index * 12)
            return struct.unpack('<3f', stream.read(12))
    return reference.full_row(case['id'], index)


def source_hash(case, count):
    h = hashlib.sha256(struct.pack('<Q', case['rows']))
    for i in range(count):
        h.update(struct.pack('<3f', *source_row(case, i)))
    return h.hexdigest()


def probability_hash(case, count):
    h = hashlib.sha256()
    for i in range(count):
        h.update(struct.pack('<3f', *source_row(case, i)))
    return h.hexdigest()


def metadata(data, name, book, cases, observed):
    require(data['case'] == name and data['status'] == 'COMPLETE', 'case identity')
    require(data['protocol_sha256'] == EXPECTED_PROTOCOL and data['fixture_sha256'] == io.sha(REFERENCE_ROOT / 'cases.json'), 'protocol/case identity')
    require(36 <= data['session_value_bytes'] <= 512 and 768 <= data['driver_value_bytes'] <= 4096, 'whole-value bounds')
    require(data['pending_payload_bytes'] == 36 and data['input_buffer_bytes'] == 768, 'declared fixed payload')
    require(data['selectors'] == ([s['expected'] for s in book['selectors']] if name == 'fixtures' else []), 'selector cases')
    require(len(data['observations']) == len(cases), 'observation denominator')
    shifted = {}
    for index, (case, item, digest) in enumerate(zip(cases, data['observations'], observed)):
        ex = case['expected']
        require(item['id'] == case['id'] and item['rows'] == case['rows'], 'source identity')
        require(item['sample_rate'] == case['sample_rate'] and item['total_frames'] == case['total_frames'], 'source geometry')
        for key in ['status', 'error', 'accepted_rows', 'committed_rows', 'units', 'result_present', 'support']:
            require(item[key] == ex[key], 'exact ' + key + ': ' + case['id'])
        require(item['input_sha256'] == source_hash(case, item['accepted_rows']), 'accepted input hash')
        if name in ['full-mixed', 'full-no-support']:
            require(item['input_sha256'] == book['full'][name]['input_sha256'], 'registered full source digest')
        require(item['probability_sha256'] == probability_hash(case, item['committed_rows']), 'committed input hash')
        if item['result_present']:
            require(item['density_sha256'] == digest, 'emitted density hash')
        else:
            # No density view was emitted. Its diagnostic hash is not a numeric
            # prefix certificate; exact inputs/counters plus Rust tests are separate.
            require(len(item['density_sha256']) == 64, 'diagnostic hash shape')
        require(item['probability_capacity'] == case['rows'] and item['density_capacity'] == case['rows'], 'actual buffer capacities')
        for key in ['construction_seconds', 'step_seconds', 'max_step_seconds', 'emit_and_composition_seconds', 'composition_seconds', 'pipe_seconds']:
            require(math.isfinite(item[key]) and item[key] >= 0, 'time scope')
        if name == 'cancel':
            require(item['cancellation_phase'] == 'NonBeatLog' and 0 <= item['cancel_seconds'] <= .05, 'pending cancel scope/latency')
        else:
            require(item['cancellation_phase'] is None and item['cancel_seconds'] is None, 'unexpected cancellation')
        comp = case.get('composition')
        if comp is None:
            require(item['composition'] is None, 'unexpected decoder invocation')
            continue
        c = item['composition']
        require(c['status'] == comp['status'], 'composition outcome')
        scores = [-math.inf if v == '-Inf' else v for v in c['scores']]
        require(len(scores) == 2 and all(isinstance(v, (int, float)) and (math.isfinite(v) and v <= 0 or v == -math.inf) for v in scores), 'score domain')
        if comp['status'] == 'GridReady':
            require(c['meter'] == comp['meter'] and c['meter'] == (4 if scores[1] > scores[0] else 3), 'finite winner')
            require(all(math.isfinite(v) for v in scores), 'finite composition scores')
            require(c['reason'] is None and c['native_wire_checked'] and len(c['wire_sha256']) == 64 and len(c['path_sha256']) == 64, 'native wire/trace evidence')
            summary = c['summary']
            for key in ['tempo_millibpm', 'anchor_q32', 'period_q32', 'downbeat_ordinal', 'beat_count', 'meter_denominator', 'segment_count', 'explicit_beat_count', 'confidence', 'key_present', 'calibrated_quality_present', 'downbeat_whole_frame', 'evidence_frames', 'applicability_frames']:
                require(summary[key] == comp[key], 'grid summary ' + key)
            pair = (c['scores'], c['path_sha256'])
            if comp['shifted_peer'] is None:
                shifted[case['id']] = pair
            else:
                require(pair == shifted[comp['shifted_peer']], 'identical trimmed evidence score/path')
        else:
            require(not c['native_wire_checked'] and c['summary'] is None and c['wire_sha256'] is None, 'unsupported/no evidence has no grid')
            if comp['status'] == 'NoPath':
                require(c['meter'] is None and scores == [-math.inf, -math.inf] and c['path_sha256'] is None, 'no path handling')
            elif comp['status'] == 'UnsupportedGrid':
                require(c['meter'] == 3 and c['reason'] == 'InsufficientEvents' and c['path_sha256'] is not None, 'unsupported reason')
            else:
                require(c['meter'] is None and c['maximum_workspace_payload'] == 0 and c['graph_numeric_payload'] == 0 and c['path_sha256'] is None and c['reason'] is None and scores == [-math.inf, -math.inf], 'no decoder on absent support')
        if c['meter'] is not None:
            support = ex['support']
            rows = support[1] - support[0]
            states = 8415 if c['meter'] == 3 else 11220
            workspace = max(11220 * 16, (24 + 2 * rows) * states + 2 * rows)
            require(c['maximum_workspace_payload'] == workspace and c['graph_numeric_payload'] == 235304 and c['event_scratch_payload'] == 24576, 'exact sequential trace/graph/event payload')
        elif c['status'] == 'NoPath':
            require(c['maximum_workspace_payload'] == 11220 * 16 and c['graph_numeric_payload'] == 235304 and c['event_scratch_payload'] == 0, 'score-only no path payload')
        else:
            require(c['event_scratch_payload'] == 0, 'no absent-support allocation')


def specs(name, book):
    if name == 'fixtures':
        return book['cases']
    key = 'full-mixed' if name == 'cancel' else name
    full = book['full'][key]
    ex = dict(status=full['status'], error=None, accepted_rows=30000, committed_rows=30000,
              units=150001, support=full['support'], result_present=True)
    if name == 'cancel':
        ex.update(status='Cancelled', accepted_rows=138, committed_rows=137, units=686, support=None, result_present=False)
    return [dict(id=key, rows=30000, sample_rate=48000, total_frames=28800000, expected=ex)]


def worker(executable, name, output):
    p = protocol()
    book = strict_json((REFERENCE_ROOT / 'cases.json').read_text())
    resource.setrlimit(resource.RLIMIT_AS, (128 * 1048576,) * 2)
    resource.setrlimit(resource.RLIMIT_CPU, (30,) * 2)
    start = time.monotonic()
    cases = specs(name, book)
    cap = p['transport']['maximum_transient_' + ('fixture' if name == 'fixtures' else 'cancel' if name == 'cancel' else 'full') + '_bytes']
    err_path = output.with_suffix('.child.stderr')
    observed = []
    with err_path.open('xb') as err:
        child = subprocess.Popen([str(executable), '--case', name], stdout=subprocess.PIPE, stderr=err)
        checker = PipeChecker(child.stdout, cap)
        try:
            header = checker.read(8)
            checker.receipt['header_hex'] = header.hex()
            if header != b'HS06BIN1':
                raise ValueError('header identity')
            for index, case in enumerate(cases):
                h = hashlib.sha256()
                checker.reference_digest = hashlib.sha256()
                if case['expected']['result_present']:
                    for r in range(case['rows']):
                        frozen = case['expected']['densities'][r] if name == 'fixtures' else None
                        raw = checker.row(index, r, source_row(case, r), frozen)
                        h.update(raw[12:])
                if name in ['full-mixed', 'full-no-support']:
                    require(checker.reference_digest.hexdigest() == book['full'][name]['reference_density_sha256'], 'full reference identity')
                observed.append(h.hexdigest())
                if 'composition' in case:
                    c = case['composition']
                    expected = list(zip(c['event_rows'], c['beat_numbers'], c['winning_classes'], [0] * c['beat_count'], [0] * c['beat_count'])) if c['status'] == 'GridReady' else []
                    checker.events(index, expected)
            data = checker.trailer()
            child.stdout.close()
            if child.wait() != 0:
                raise ValueError('Rust child failed')
            metadata(data, name, book, cases, observed)
            own, children = resource.getrusage(resource.RUSAGE_SELF), resource.getrusage(resource.RUSAGE_CHILDREN)
            cpu = own.ru_utime + own.ru_stime + children.ru_utime + children.ru_stime
            if cpu > 30 or max(own.ru_maxrss, children.ru_maxrss) > 65536:
                raise ValueError('combined CPU or HWM RSS cap exceeded')
            runtime = {}
            io.runtime_snapshot(runtime)
            io.write_new(output.with_suffix('.runtime.json'), runtime)
            data.update(worker_wall_seconds=time.monotonic()-start, combined_cpu_seconds=cpu,
                        worker_peak_rss_kib=own.ru_maxrss, child_peak_rss_kib=children.ru_maxrss,
                        child_cpu_seconds=children.ru_utime+children.ru_stime,
                        pipe_wait_seconds=checker.wait_seconds, checker_seconds=checker.check_seconds,
                        applied_rlimits={'AS': resource.getrlimit(resource.RLIMIT_AS), 'CPU': resource.getrlimit(resource.RLIMIT_CPU)},
                        prefix_density_hash_unverified_metadata_only=name == 'cancel',
                        child_stderr_sha256=io.sha(err_path))
            checker.receipt['status'] = 'PASS'
            io.write_new(output, data)
        except BaseException as exc:
            checker.receipt.update(failure=str(exc), failure_kind='SYNTHETIC_FAIL' if isinstance(exc, SyntheticFailure) else 'INVALID_OR_INCOMPLETE')
            if child.poll() is None:
                child.kill()
            child.wait()
            child.stdout.close()
            raise
        finally:
            checker.receipt.update(transport_bytes=checker.count, transport_sha256=checker.digest.hexdigest(),
                                   complete_records=checker.ordinal, candidate_density_sha256=observed, child_returncode=child.returncode)
            io.write_new(output.with_suffix('.pipe-receipt.json'), checker.receipt)


def checker_preflight():
    p = [0., 1., 0.]
    d = reference.density(p)
    raw = struct.pack('<BHI3f3d', 1, 0, 0, *p, *d)
    PipeChecker(binary_io.BytesIO(raw), 43).row(0, 0, p)
    bad = [b'\x02' + raw[1:], raw[:1] + struct.pack('<H', 1) + raw[3:],
           raw[:3] + struct.pack('<I', 1) + raw[7:], raw[:-1],
           raw[:7] + struct.pack('<I', 0x80000000) + raw[11:]]
    for x in [math.nan, math.inf, 0.]:
        bad.append(raw[:19] + struct.pack('<d', x) + raw[27:])
    bad.append(raw[:35] + struct.pack('<d', -math.inf))
    for value in bad:
        try:
            PipeChecker(binary_io.BytesIO(value), 43).row(0, 0, p)
        except ValueError:
            pass
        else:
            raise ValueError('malformed row accepted')
    trailer = b'\x03' + struct.pack('<I', 2) + b'{}'
    assert PipeChecker(binary_io.BytesIO(trailer), 65549).trailer() == {}
    for value in [trailer + b'x', trailer[:-1], b'\x03' + struct.pack('<I', 65537),
                  b'\x03' + struct.pack('<I', 13) + b'{"a":1,"a":2}',
                  b'\x03' + struct.pack('<I', 9) + b'{"a":NaN}']:
        try:
            PipeChecker(binary_io.BytesIO(value), 65550).trailer()
        except ValueError:
            pass
        else:
            raise ValueError('malformed trailer accepted')
    events = struct.pack('<BHHIBBBB', 2, 0, 1, 0, 1, 1, 0, 0)
    PipeChecker(binary_io.BytesIO(events), len(events)).events(0, [(0, 1, 1, 0, 0)])
    for value in [events[:-1], events[:-1] + b'\x01', events[:3] + struct.pack('<H', 0) + events[5:]]:
        try:
            PipeChecker(binary_io.BytesIO(value), len(events)).events(0, [(0, 1, 1, 0, 0)])
        except ValueError:
            pass
        else:
            raise ValueError('malformed event accepted')
    return dict(accepted=3, refused=len(bad)+5+3)


def bind_runtime(executable, p):
    bindings = {}
    for name, meta in p['bindings'].items():
        io.bind(ROOT / name, meta['sha256'], bindings)
    for name in p['planned_source_paths']:
        io.bind(ROOT / name, io.sha(ROOT / name), bindings)
    io.bind(PROTOCOL, EXPECTED_PROTOCOL, bindings)
    for path in [executable, Path(sys.executable).resolve()]:
        io.bind(path, io.sha(path), bindings)
    rustup = (Path.home() / '.cargo/bin/rustup').resolve()
    io.bind(rustup, io.sha(rustup), bindings)
    toolchain = dict(feature='experimental-compact-probabilities', tools={})
    for name in ['rustc', 'cargo']:
        path = Path(subprocess.check_output([str(rustup), 'which', name], text=True).strip()).resolve()
        io.bind(path, io.sha(path), bindings)
        toolchain['tools'][name] = dict(path=str(path), sha256=io.sha(path), version_verbose=subprocess.check_output([str(path), '-vV'], text=True))
    linked = subprocess.run(['ldd', str(executable.resolve())], capture_output=True, text=True, check=True)
    for token in linked.stdout.split():
        if token.startswith('/'):
            path = Path(token).resolve()
            io.bind(path, io.sha(path), bindings)
    subprocess.run([sys.executable, '-c', 'pass'], check=True, capture_output=True, text=True, timeout=5)
    count = io.runtime_snapshot(bindings)
    return bindings, toolchain, count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--worker', choices=CASES)
    parser.add_argument('--preflight', action='store_true', help='only fixtures, malformed checker probes and resource refusals; no long candidate case')
    args = parser.parse_args()
    if args.worker:
        worker(args.executable, args.worker, args.output)
        return
    p = protocol()
    if p['fixed_case_order'] != CASES:
        raise ValueError('fixed four-case order changed')
    revision = io.git(ROOT, 'rev-parse', 'HEAD')
    if not args.preflight:
        io.check_source(ROOT, revision)
    io.external(args.output, ROOT)
    io.external(args.executable, ROOT)
    io.disjoint_output(args.output, [args.executable])
    bindings, toolchain, runtime_count = bind_runtime(args.executable, p)
    args.output.mkdir(parents=True, exist_ok=False)
    final = dict(format='libapta-host-probability-result-1', protocol_sha256=EXPECTED_PROTOCOL,
                 source_revision=revision, executable_sha256=io.sha(args.executable),
                 component_sha256={n: io.sha(ROOT / n) for n in p['planned_source_paths']},
                 python=sys.version, toolchain=toolchain, parent_runtime_files=runtime_count,
                 model_corpus_or_device_executed=False, preflight_only=args.preflight)
    receipts = []
    try:
        final['checker_preflight'] = checker_preflight()
        print('HS06 enforcement preflight', flush=True)
        final['enforcement'] = enforcement(args.output)
        io.write_new(args.output / 'bindings.json', bindings)
        for name in ['fixtures'] if args.preflight else CASES:
            print('HS06 start ' + name, flush=True)
            path = args.output / (name + '.json')
            command = [sys.executable, str(Path(__file__).resolve()), '--worker', name,
                       '--executable', str(args.executable.resolve()), '--output', str(path)]
            usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
            resources = io.supervise(command, 45, 65536, 1048576, [args.output], args.output / name)
            usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
            complete_waited_cpu = (usage_after.ru_utime + usage_after.ru_stime
                                   - usage_before.ru_utime - usage_before.ru_stime)
            if complete_waited_cpu > 30:
                raise ValueError('complete waited worker and descendants CPU cap exceeded')
            if resources['status'] != 'PASS' or resources['failure'] is not None:
                raise ValueError('supervisor final refusal')
            data = strict_json(path.read_text())
            data['complete_waited_worker_and_descendants_cpu_seconds'] = complete_waited_cpu
            receipts.append(dict(case=name, result=data, supervision=resources))
            io.recheck(bindings)
            io.runtime_recheck(bindings)
            runtime = strict_json(path.with_suffix('.runtime.json').read_text())
            if any(bindings.get(n) != h for n, h in runtime.items()):
                raise ValueError('worker runtime not prebound')
            io.recheck(runtime)
            print('HS06 PASS ' + name, flush=True)
        if not args.preflight:
            io.check_source(ROOT, revision)
        final.update(status='HS06_PREFLIGHT_ONLY' if args.preflight else 'HS06_SYNTHETIC_HOST_PASS_ONLY', cases=receipts)
        if io.tree_bytes([args.output]) + io.json_bytes(final) > 1048576:
            raise ValueError('aggregate artifact cap exceeded')
    except BaseException as exc:
        failures = [strict_json(path.read_text()) for path in args.output.glob('*.pipe-receipt.json')]
        verdict = 'HS06_SYNTHETIC_FAIL' if any(r.get('failure_kind') == 'SYNTHETIC_FAIL' for r in failures) else 'INVALID_OR_INCOMPLETE'
        final.update(status=verdict, failure=str(exc), cases=receipts)
        io.write_new(args.output / 'result.json', final)
        raise
    io.write_new(args.output / 'result.json', final)
    print(json.dumps(dict(status=final['status'], cases=len(receipts))), flush=True)


if __name__ == '__main__':
    main()
