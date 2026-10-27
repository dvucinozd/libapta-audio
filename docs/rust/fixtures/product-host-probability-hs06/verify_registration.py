# SPDX-License-Identifier: Apache-2.0
"""Independent HS06 registration checks. No oracle/generator/candidate import."""
from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def scalar(value):
    return -math.inf if value == '-Inf' else value


def checked_logs(row):
    b, d, n = row
    # F64 division precedes log; high precision verifies each finite log.
    arguments = [n / 15., b, d]
    result = []
    with localcontext() as ctx:
        ctx.prec = 100
        for argument in arguments:
            if argument == 0:
                result.append(-math.inf)
            else:
                reference = Decimal.from_float(argument).ln()
                actual = math.log(argument)
                check(abs(Decimal.from_float(actual) - reference) <= Decimal('4e-13'), 'libm vs Decimal')
                result.append(actual)
    return result


def read_rows(raw):
    check(len(raw) % 12 == 0, 'row alignment')
    return list(struct.iter_unpack('<3f', raw))


def main():
    book = json.loads((HERE / 'cases.json').read_text(encoding='utf-8'))
    protocol = json.loads((REPO / 'docs/rust/PRODUCT-HOST-PROBABILITY-PROTOCOL.json').read_text(encoding='utf-8'))
    for path, identity in protocol['bindings'].items():
        raw = (REPO / path).read_bytes()
        check(len(raw) == identity['bytes'], 'binding bytes: ' + path)
        check(hashlib.sha256(raw).hexdigest() == identity['sha256'], 'binding hash: ' + path)
    check((REPO / 'VERSION').read_text().strip() == '1.0.1', 'version')
    check(book['input_order'] == ['beat', 'downbeat', 'nonbeat'], 'input class order')
    check(book['density_order'] == ['nonbeat', 'beat', 'downbeat'], 'density class order')
    all_rows, invalid, valid_rows, scalar_count = 0, 0, 0, 0
    by_id = {}
    for case in book['cases']:
        raw = (HERE / case['input']).read_bytes()
        meta = book['assets'][case['input']]
        check(len(raw) == meta['bytes'] and hashlib.sha256(raw).hexdigest() == meta['sha256'], 'asset')
        rows = read_rows(raw)
        by_id[case['id']] = (case, rows)
        check(case['rows'] == len(rows), 'rows')
        check((50 * case['total_frames'] + case['sample_rate'] - 1) // case['sample_rate'] == len(rows), 'ceil geometry')
        all_rows += len(rows)
        supported, references, bad = [], [], None
        for index, row in enumerate(rows):
            # Exact decimal sum is also exact binary64 sum for three binary32
            # nonnegative probabilities of magnitude <=1.
            domain = all(math.isfinite(x) and 0 <= x <= 1 for x in row)
            if domain:
                with localcontext() as ctx:
                    ctx.prec = 200
                    total = sum((Decimal.from_float(x) for x in row), Decimal(0))
                    domain = abs(total - 1) <= Decimal(2) ** -21
            if not domain:
                bad = index
                break
            references.append(checked_logs(row))
            if row[0] >= .05 or row[1] >= .05:
                supported.append(index)
        ex = case['expected']
        if bad is not None:
            invalid += 1
            check(ex == dict(status='Failed', error='InvalidProbability', accepted_rows=bad,
                             committed_rows=bad, units=bad * 5 + 1, support=None,
                             result_present=False, densities=[]), 'invalid prefix semantics')
        else:
            valid_rows += len(rows)
            support = [supported[0], supported[-1] + 1] if supported else None
            check(ex['support'] == support and ex['result_present'], 'support/result')
            check(ex['status'] == ('Ready' if support else 'NoEvidence'), 'status')
            check(ex['accepted_rows'] == ex['committed_rows'] == len(rows), 'counters')
            check(ex['units'] == (len(rows) * 5 + 1 if rows else 0), 'units')
            check(len(ex['densities']) == len(rows), 'density count')
            for actual, reference in zip(ex['densities'], references):
                for x, y in zip(actual, reference):
                    x = scalar(x)
                    check((x == y == -math.inf) or (math.isfinite(x) and abs(x - y) <= 4e-13), 'density scalar')
                    scalar_count += 1
    counts = book['counts']
    check(counts['cases'] == 35 and counts['raw_bytes'] == 10536 and all_rows == 878 and valid_rows == 850, 'fixed cohort')
    check(counts['cases'] == len(book['cases']), 'case count')
    check(counts['short_rows'] == all_rows and counts['valid_short_rows'] == valid_rows, 'row denominator')
    check(counts['invalid_cases'] == invalid == 7, 'invalid denominator')
    check(counts['raw_assets'] == len(book['assets']) == len(book['cases']), 'asset denominator')
    check(counts['raw_bytes'] == sum(v['bytes'] for v in book['assets'].values()), 'asset bytes')
    check(counts['short_schedules'] == counts['cases'] * 20, 'schedule denominator')
    check(counts['composition_cases'] == sum('composition' in c for c in book['cases']) == 8, 'composition denominator')
    check(counts['selector_cases'] == len(book['selectors']) == 11, 'selector denominator')
    for selector in book['selectors']:
        scores = [float(v) for v in (selector['score3'], selector['score4'])]
        if any(math.isnan(s) or s == math.inf or s > 0 for s in scores):
            chosen = 'InvalidScore'
        elif scores == [-math.inf, -math.inf]:
            chosen = 'NoPath'
        else:
            chosen = 3 + int(scores[1] > scores[0])
        check(selector['expected'] == chosen, 'meter selection')
    # Explicit edge checks prevent a swapped order or relaxed threshold/sum.
    check(by_id['beat'][0]['expected']['densities'][0] == ['-Inf', 0., '-Inf'], 'beat slot')
    check(by_id['downbeat'][0]['expected']['densities'][0] == ['-Inf', '-Inf', 0.], 'down slot')
    check(struct.pack('<3f', *by_id['signed-zero'][1][0]) == struct.pack('<III', 0x80000000, 0x80000000, 0x3f800000), 'signed zero bits')
    for suffix in ['beat', 'down']:
        check(by_id['threshold-below-' + suffix][0]['expected']['support'] is None, 'threshold below')
        for label in ['at', 'above']:
            check(by_id['threshold-' + label + '-' + suffix][0]['expected']['support'] == [0, 1], 'threshold included')
    check(by_id['sum-exact'][0]['expected']['error'] is None, 'sum endpoint accepted')
    check(by_id['sum-between-old-bounds'][0]['expected']['error'] == 'InvalidProbability', 'stricter sum guard')
    check(by_id['support-exclusive-tail'][0]['expected']['support'] == [3, 4], 'exclusive source tail')
    for meter in [3, 4]:
        base, rows = by_id[f'composition-{meter}-0']
        shifted, shifted_rows = by_id[f'composition-{meter}-2']
        lo, hi = base['expected']['support']
        slo, shi = shifted['expected']['support']
        check(rows[lo:hi] == shifted_rows[slo:shi] and slo == lo + 2 and shi == hi + 2, 'identical support slice')
        check(shifted['composition']['event_rows'] == [x + 2 for x in base['composition']['event_rows']], 'global origin')
        for case in [base, shifted]:
            events = case['composition']['event_rows']
            check(len(events) == 2 * meter + 1 and all(b - a == 25 for a, b in zip(events, events[1:])), 'grid events')
            check(int(case['composition']['anchor_q32']) == events[0] * case['sample_rate'] // 50 * (1 << 32), 'anchor')
            check(int(case['composition']['period_q32']) == case['sample_rate'] // 2 * (1 << 32), 'period')
            comp = case['composition']
            check(comp['beat_numbers'] == [i % meter + 1 for i in range(len(events))], 'beat phase')
            check(comp['winning_classes'] == [int(i % meter == 0) for i in range(len(events))], 'winning probability class')
            check(comp['reserved_bytes'] == [0, 0] and comp['confidence'] == 0 and not comp['key_present'] and not comp['calibrated_quality_present'], 'event and unknown fields')
            check(comp['evidence_frames'] == [case['expected']['support'][0] * case['sample_rate'] // 50,
                                             case['expected']['support'][1] * case['sample_rate'] // 50], 'evidence origin/end')
            check(comp['beat_count'] == len(events) and comp['segment_count'] == 1 and comp['explicit_beat_count'] == 0 and comp['meter_denominator'] == 4, 'wire summary fields')
            check(comp['downbeat_whole_frame'] == events[0] * case['sample_rate'] // 50, 'downbeat whole frame')
    # Independent boolean reachability proves all-down 8-row NoPath without
    # implementing/using another floating-point HMM scorer or candidate.
    asset_root = REPO / 'docs/rust/fixtures/product-compact-cs02/assets'
    for meter in [3, 4]:
        ptr = [v[0] for v in struct.iter_unpack('<I', (asset_root / f'hmm{meter}-pointers.u32le').read_bytes())]
        pred = [v[0] for v in struct.iter_unpack('<H', (asset_root / f'hmm{meter}-previous.u16le').read_bytes())]
        state = list(struct.iter_unpack('BBB', (asset_root / f'hmm{meter}-state-map.u8').read_bytes()))
        transition = [v[0] for v in struct.iter_unpack('<d', (asset_root / f'hmm{meter}-logp.f64le').read_bytes())]
        check(all(t <= 0 for t in transition), 'nonpositive transition logs')
        check(any(b == 0 and 16 * q < interval and any(transition[j] == 0 for j in range(ptr[s], ptr[s + 1]))
                  for s, (b, interval, q) in enumerate(state)), 'one-row downbeat max transition zero')
        active = [True] * len(state)
        for _ in range(8):
            active = [b == 0 and 16 * q < interval and any(active[pred[j]] for j in range(ptr[s], ptr[s + 1]))
                      for s, (b, interval, q) in enumerate(state)]
        check(not any(active), 'all-down no path')
    prior3 = struct.unpack('<d', struct.pack('<Q', 0xc0221356bc07601a))[0]
    prior4 = struct.unpack('<d', struct.pack('<Q', 0xc022a6a1cc9106f7))[0]
    check(prior3 > prior4, 'one-row downbeat meter3 winner')
    # Independent bit-coded palette and explicit cycle, not oracle recipes.
    palette = [[0, 0, 0x3f800000], [0x3f800000, 0, 0], [0, 0x3f800000, 0],
               [0x3eaaaaab] * 3, [1, 0, 0x3f800000], [0, 1, 0x3f800000],
               [0x3f000000, 0x3f000000, 1], [0x3d4ccccc, 0, 0x3f733333],
               [0x3d4ccccd, 0, 0x3f733333], [0x3e800000, 0x3e800000, 0x3f000008]]
    cycle = [3, 0, 7, 4, 1, 8, 5, 2, 9, 6]
    check(set(book['full']) == {'full-mixed', 'full-no-support'} and counts['full_reference_rows'] == 60000, 'full denominator')
    for name, full in book['full'].items():
        raw_cycle = [struct.pack('<III', *palette[k]) for k in cycle] if name == 'full-mixed' else [struct.pack('<III', 0x3c23d70a, 0x3c23d70a, 0x3f7ae148)]
        check(name in ['full-mixed', 'full-no-support'], 'full recipe name')
        values = [struct.unpack('<3f', raw) for raw in raw_cycle]
        logs = [checked_logs(v) for v in values]
        source, output = hashlib.sha256(struct.pack('<Q', 30000)), hashlib.sha256()
        first, last = None, None
        for i in range(30000):
            k = i % len(values)
            source.update(raw_cycle[k])
            output.update(struct.pack('<3d', *logs[k]))
            if values[k][0] >= .05 or values[k][1] >= .05:
                if first is None:
                    first = i
                last = i + 1
        check(full['input_sha256'] == source.hexdigest(), 'full source digest')
        check(full['reference_density_sha256'] == output.hexdigest(), 'full reference digest')
        check(full['support'] == ([first, last] if first is not None else None), 'full support')
        check(full['rows'] == 30000 and full['units'] == 150001, 'full work')
        for anchor in full['anchors']:
            k = anchor['row'] % len(values)
            check(struct.pack('<III', *anchor['input_bits']) == raw_cycle[k], 'full anchor input')
            for actual, reference in zip(anchor['density'], logs[k]):
                a = scalar(actual)
                check(a == reference or abs(a - reference) <= 4e-13, 'full anchor density')
    check(protocol['case_counts'] == counts, 'protocol counts')
    check(protocol['storage']['probability_bytes_at_30000'] == 30000 * 3 * 4, 'probability bytes')
    check(protocol['storage']['density_bytes_at_30000'] == 30000 * 3 * 8, 'density bytes')
    check(protocol['fixed_case_order'] == ['fixtures', 'full-mixed', 'full-no-support', 'cancel'], 'run order')
    transport = protocol['transport']
    check(transport['maximum_transient_fixture_bytes'] == 8 + 43 * valid_rows + 5 * 8 + 8 * 32 + 5 + 65536, 'fixture transport cap')
    check(transport['maximum_transient_full_bytes'] == 8 + 43 * 30000 + 5 + 65536, 'full transport cap')
    cancel = protocol['cancellation']
    check(cancel['units_before_flag'] == 5 * cancel['committed_rows_before_flag'] + 1 == 686, 'cancel unit point')
    check(cancel['accepted_rows_before_flag'] == cancel['committed_rows_before_flag'] + 1 == 138, 'cancel staging point')
    check(protocol['composition']['block_rows'] == 256, 'frozen HMM block size')
    print(json.dumps(dict(registration='PASS_MATH_AND_IDENTITIES_ONLY', cases=len(book['cases']),
                          checked_short_density_scalars=scalar_count, full_reference_rows=60000,
                          old_and_new_bindings=len(protocol['bindings']), candidate='NOT_RUN'), sort_keys=True))


if __name__ == '__main__':
    main()
