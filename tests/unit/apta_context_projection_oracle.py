# SPDX-License-Identifier: Apache-2.0
"""Independent P1 oracle: rational elimination, enumeration and residual products.

No production or registration-test helpers are imported. This is test-only code.
"""
from fractions import Fraction

REGISTRATION_SHA256 = '56e5b498d0baa7d83b0913fe7dfb7ff14f542d479c94a2e59818753bfe424489'


def _nearest(value):
    quotient, remainder = divmod(value.numerator, value.denominator)
    return quotient + int(2 * remainder >= value.denominator)


def normal_fit(values):
    rows = [[Fraction(0), Fraction(0), Fraction(0)] for _ in range(2)]
    for index, value in enumerate(values):
        design = (Fraction(1), Fraction(index))
        for a in range(2):
            for b in range(2):
                rows[a][b] += design[a] * design[b]
            rows[a][2] += design[a] * value
    # Gaussian elimination instead of the production covariance determinant.
    factor = rows[1][0] / rows[0][0]
    for column in range(3):
        rows[1][column] -= factor * rows[0][column]
    slope = rows[1][2] / rows[1][1]
    intercept = (rows[0][2] - rows[0][1] * slope) / rows[0][0]
    return intercept, slope


def independent_project_window(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame):
    if (type(beat_frames) is not list or type(downbeat_ordinals) is not list
            or type(total_frames) is not int or type(first_frame) is not int or type(end_frame) is not int
            or not 1 <= total_frames <= 1440000 or not 0 <= first_frame < end_frame <= total_frames
            or len(beat_frames) > 3072 or len(downbeat_ordinals) > 3072):
        raise ValueError('invalid input')
    if (any(type(x) is not int or not 0 <= x < total_frames for x in beat_frames)
            or any(x >= y for x, y in zip(beat_frames, beat_frames[1:]))
            or any(type(x) is not int or not 0 <= x < len(beat_frames) for x in downbeat_ordinals)
            or any(x >= y for x, y in zip(downbeat_ordinals, downbeat_ordinals[1:]))):
        raise ValueError('invalid input')
    owned_observations = [(i, frame) for i, frame in enumerate(beat_frames) if first_frame <= frame < end_frame]
    count = len(owned_observations)
    origin_ordinal = owned_observations[0][0] if count else None
    result = {'format': 'libapta-context-anchor-projection-1', 'registration_sha256': REGISTRATION_SHA256,
              'total_frames': total_frames, 'first_frame': first_frame, 'end_frame': end_frame,
              'source_beat_count': len(beat_frames), 'first_observed_ordinal': origin_ordinal,
              'observed_count': count, 'status': 'INSUFFICIENT_EVENTS', 'fit': None,
              'model': None, 'first_index': None, 'point_count': 0, 'points': [],
              'meter': None, 'meter_status': 'NOT_EVALUATED'}
    if count < 4:
        return result
    intercept, slope = normal_fit([p for _, p in owned_observations])
    if slope <= 0:
        raise ValueError('nonpositive slope')
    A, T = _nearest(intercept), _nearest(Fraction(2880000000, 1) / slope)
    result['fit'] = {'intercept_numerator': intercept.numerator, 'intercept_denominator': intercept.denominator,
                     'period_numerator': slope.numerator, 'period_denominator': slope.denominator,
                     'rounded_anchor': A, 'tempo_millibpm': T}
    if T < 40000 or T > 300000:
        result['status'] = 'UNSUPPORTED_TEMPO'
        return result
    if A < 0 or A >= total_frames:
        result['status'] = 'SOURCE_ANCHOR_OUT_OF_DOMAIN'
        return result
    quantum = 4294967296
    step = (2880000000 * quantum) // T
    result['model'] = {'anchor_frame': A, 'anchor_ordinal': origin_ordinal, 'tempo_millibpm': T,
                       'period_q32': step, 'confidence': 0}
    # Do not use a ceiling/last-index formula: independently enumerate and filter.
    indices = [i for i in range(3073) if first_frame * quantum <= A * quantum + i * step < end_frame * quantum]
    if len(indices) > 3072:
        raise ValueError('point capacity')
    if not indices:
        result['status'] = 'NO_OWNED_POINTS'
        return result
    result.update(status='ACCEPTED', first_index=indices[0], point_count=len(indices),
                  points=[{'position_q32': A * quantum + i * step, 'ordinal': origin_ordinal + i} for i in indices])
    db = [(i, beat_frames[i]) for i in downbeat_ordinals if first_frame <= beat_frames[i] < end_frame]
    result['meter_status'] = 'NO_ELIGIBLE_BARS'
    for a, b, c in zip(db, db[1:], db[2:]):
        meter = b[0] - a[0]
        if meter not in (3, 4) or c[0] - b[0] != meter:
            continue
        def eligible(event):
            index = _nearest(Fraction((event[1] - A) * quantum, step))
            q = A * quantum + index * step
            # Cross-products instead of the production Fraction distance test.
            good = index >= 0 and 10 * abs(q - event[1] * quantum) * slope.denominator <= slope.numerator * quantum
            return index, q, good
        if not eligible(a)[2]:
            continue
        result['meter_status'] = 'NO_OWNED_WITNESS'
        for event in (a, b, c):
            index, q, good = eligible(event)
            if good and first_frame * quantum <= q < end_frame * quantum:
                result['meter'] = {'kind': 'estimated', 'numerator': meter, 'denominator': 4,
                                   'downbeat_frame': q // quantum, 'downbeat_fraction_q32': q % quantum,
                                   'downbeat_ordinal': origin_ordinal + index,
                                   'witness_ordinals': [a[0], b[0], c[0]]}
                result['meter_status'] = 'OWNED'
                break
        break
    return result
