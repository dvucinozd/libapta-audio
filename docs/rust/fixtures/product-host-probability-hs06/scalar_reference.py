# SPDX-License-Identifier: Apache-2.0
"""HS06 public arithmetic recipes; stdlib only, never a candidate implementation.

--write is registration generation only. After registration the checked-in bytes
are authoritative; later implementations/checkers must not regenerate them.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent


def q(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def from_bits(x):
    return struct.unpack('<f', struct.pack('<I', x))[0]


def tagged(x):
    return '-Inf' if x == -math.inf else x


def density(row):
    b, d, n = row
    if not all(math.isfinite(x) and 0 <= x <= 1 for x in row):
        raise ValueError('InvalidProbability')
    if abs(((float(b) + float(d)) + float(n)) - 1) > 2 ** -21:
        raise ValueError('InvalidProbability')
    return [math.log(n / 15) if n else -math.inf,
            math.log(b) if b else -math.inf,
            math.log(d) if d else -math.inf]


def expected(rows):
    converted, supported = [], []
    for i, row in enumerate(rows):
        try:
            converted.append([tagged(x) for x in density(row)])
        except ValueError:
            return dict(status='Failed', error='InvalidProbability',
                        accepted_rows=i, committed_rows=i, units=5 * i + 1,
                        support=None, result_present=False, densities=[])
        if float(row[0]) >= .05 or float(row[1]) >= .05:
            supported.append(i)
    support = [supported[0], supported[-1] + 1] if supported else None
    return dict(status='Ready' if support else 'NoEvidence', error=None,
                accepted_rows=len(rows), committed_rows=len(rows),
                units=5 * len(rows) + 1 if rows else 0, support=support,
                result_present=True, densities=converted)


def full_row(name, index):
    """Every row is evaluated; palette repetition is only an input recipe."""
    if name == 'full-no-support':
        return [q(.01), q(.01), q(.98)]
    if name != 'full-mixed':
        raise ValueError(name)
    # Distinct all-three impulses, subnormals, sum and threshold boundaries.
    palette = [[0, 0, 1], [1, 0, 0], [0, 1, 0],
               [q(1 / 3)] * 3, [from_bits(1), 0, 1],
               [0, from_bits(1), 1], [.5, .5, from_bits(1)],
               [from_bits(bits(q(.05)) - 1), 0, q(.95)],
               [q(.05), 0, q(.95)], [.25, .25, q(.5 + 2 ** -21)]]
    return list(palette[(index * 7 + 3) % 10])


def stream_identity(name, count=30000):
    source = hashlib.sha256(struct.pack('<Q', count))
    converted = hashlib.sha256()
    support = []
    anchors = []
    for i in range(count):
        row = full_row(name, i)
        values = density(row)
        source.update(struct.pack('<3f', *row))
        converted.update(struct.pack('<3d', *values))
        if row[0] >= .05 or row[1] >= .05:
            if not support:
                support = [i, i + 1]
            support[1] = i + 1
        if i in [0, 1, 9, 137, 14999, 29999]:
            anchors.append(dict(row=i, input_bits=[bits(x) for x in row],
                                density=[tagged(x) for x in values]))
    return dict(rows=count, input_sha256=source.hexdigest(),
                reference_density_sha256=converted.hexdigest(),
                support=support or None, anchors=anchors,
                status='Ready' if support else 'NoEvidence', units=5 * count + 1)


def make():
    cases, assets = [], {}

    def add(name, rows, fs=44100, source_frames=None, composition=None):
        rows = [[q(x) for x in r] for r in rows]
        raw = b''.join(struct.pack('<3f', *r) for r in rows)
        path = 'assets/' + name + '.f32le'
        assets[path] = raw
        c = dict(id=name, rows=len(rows), sample_rate=fs,
                 total_frames=source_frames if source_frames is not None else len(rows) * fs // 50,
                 input=path, expected=expected(rows))
        if composition is not None:
            c['composition'] = composition
        cases.append(c)

    add('empty', [])
    for name, row in [('nonbeat', [0, 0, 1]), ('beat', [1, 0, 0]),
                      ('downbeat', [0, 1, 0]), ('uniform', [q(1 / 3)] * 3),
                      ('signed-zero', [-0., -0., 1]),
                      ('subnormal-beat', [from_bits(1), 0, 1]),
                      ('subnormal-down', [0, from_bits(1), 1]),
                      ('subnormal-non', [.5, .5, from_bits(1)])]:
        add(name, [row])
    for col, suffix in [(0, 'beat'), (1, 'down')]:
        for delta, label in [(-1, 'below'), (0, 'at'), (1, 'above')]:
            row = [0., 0., q(.95)]
            row[col] = from_bits(bits(q(.05)) + delta)
            add('threshold-' + label + '-' + suffix, [row])
    add('support-row-zero', [[1, 0, 0], [0, 0, 1], [0, 0, 1]])
    add('support-exclusive-tail', [[0, 0, 1]] * 3 + [[0, 1, 0]],
        source_frames=3 * 882 + 1)
    add('support-gap', [[0, 0, 1], [1, 0, 0], [0, 0, 1],
                        [0, 1, 0], [0, 0, 1]])
    for label, delta in [('inside', 2 ** -22), ('exact', 2 ** -21)]:
        add('sum-' + label, [[.25, .25, q(.5 + delta)]])
    invalid = [('sum-between-old-bounds', [.25, .25, q(.5 + 2 ** -20)]),
               ('all-zero', [0, 0, 0]),
               ('nan-beat', [from_bits(0x7fc00001), 0, 1]),
               ('inf-down', [0, math.inf, 1]),
               ('negative-inf-non', [0, 0, -math.inf]),
               ('negative-beat', [-from_bits(1), 0, 1]),
               ('above-one-down', [0, from_bits(bits(1.) + 1), 0])]
    for name, row in invalid:
        add(name, [[0, 0, 1], [1, 0, 0], row, [0, 1, 0]])
    for meter, base_rows, fs in [(3, 175, 44100), (4, 225, 48000)]:
        for shift in [0, 2]:
            count = base_rows + shift
            rows = []
            for i in range(count):
                t = i - shift
                rows.append([q(.02), q(.96), q(.02)] if t >= 0 and t % (25 * meter) == 0
                            else [q(.96), q(.02), q(.02)] if t >= 0 and t % 25 == 0
                            else [q(.01), q(.01), q(.98)])
            events = list(range(shift, ((base_rows - 1) // 25) * 25 + shift + 1, 25))
            comp = dict(status='GridReady', meter=meter,
                        basis='frozen HS02 two_small_hmm_compositions plus identical-slice shift',
                        event_rows=events, beat_numbers=[i % meter + 1 for i in range(len(events))],
                        tempo_millibpm=120000, anchor_q32=str((shift * fs // 50) << 32),
                        period_q32=str((fs // 2) << 32), downbeat_ordinal=0,
                        applicability_frames=[0, count * fs // 50],
                        native_wire_validation='required',
                        shifted_peer=None if shift == 0 else 'composition-' + str(meter) + '-0')
            comp.update(beat_count=len(events), meter_denominator=4, segment_count=1,
                        explicit_beat_count=0, confidence=0, key_present=False,
                        calibrated_quality_present=False, reserved_bytes=[0, 0],
                        winning_classes=[1 if i % meter == 0 else 0 for i in range(len(events))],
                        evidence_frames=[shift * fs // 50, (events[-1] + 1) * fs // 50],
                        downbeat_whole_frame=shift * fs // 50)
            add('composition-' + str(meter) + '-' + str(shift), rows, fs, composition=comp)
    add('composition-no-path', [[0, 1, 0]] * 8, composition=dict(status='NoPath', meters=[3, 4]))
    add('composition-insufficient', [[0, 1, 0]],
        composition=dict(status='UnsupportedGrid', reason='InsufficientEvents', meter=3,
                         result_present=False))
    add('composition-no-support', [[q(.01), q(.01), q(.98)]] * 9,
        composition=dict(status='NoEvidence', decoder_invoked=False))
    add('composition-empty', [], composition=dict(status='NoEvidence', decoder_invoked=False))
    selectors = []
    for s3, s4, value in [(-10, -10, 3), (-10, -9, 4), (-9, -10, 3),
                          ('-Inf', -9, 4), (-9, '-Inf', 3),
                          ('-Inf', '-Inf', 'NoPath'), (-0., 0., 3),
                          ('NaN', -9, 'InvalidScore'), (-9, '+Inf', 'InvalidScore'),
                          (1, -9, 'InvalidScore'), (-9, 'NaN', 'InvalidScore')]:
        selectors.append(dict(score3=s3, score4=s4, expected=value))
    full = {name: stream_identity(name) for name in ['full-mixed', 'full-no-support']}
    counts = dict(cases=len(cases), short_rows=sum(c['rows'] for c in cases),
                  valid_short_rows=sum(c['rows'] for c in cases if c['expected']['error'] is None),
                  invalid_cases=len(invalid), composition_cases=sum('composition' in c for c in cases),
                  selector_cases=len(selectors), raw_assets=len(assets), raw_bytes=sum(map(len, assets.values())),
                  schedules_per_case=20, short_schedules=20 * len(cases), full_reference_rows=60000)
    book = dict(format='libapta-hs06-cases-1', input_order=['beat', 'downbeat', 'nonbeat'],
                density_order=['nonbeat', 'beat', 'downbeat'], counts=counts,
                cases=cases, selectors=selectors, full=full,
                assets={p: dict(bytes=len(b), sha256=hashlib.sha256(b).hexdigest()) for p, b in assets.items()})
    return book, assets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    book, assets = make()
    if args.write:
        (ROOT / 'assets').mkdir(exist_ok=True)
        for path, raw in assets.items():
            (ROOT / path).write_bytes(raw)
        (ROOT / 'cases.json').write_bytes((json.dumps(book, indent=2, sort_keys=True, allow_nan=False) + '\n').encode('utf-8'))
    print(json.dumps(book['counts'], sort_keys=True))


if __name__ == '__main__':
    main()
