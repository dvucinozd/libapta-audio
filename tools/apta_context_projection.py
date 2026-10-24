#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""P0 generated-only context-anchor projection; no CLI, I/O or scorer imports."""
from fractions import Fraction

REGISTRATION_SHA256 = '56e5b498d0baa7d83b0913fe7dfb7ff14f542d479c94a2e59818753bfe424489'
FORMAT = 'libapta-context-anchor-projection-1'
Q = 1 << 32
MAXIMUM_EVENTS = 3072
MAXIMUM_SOURCE_FRAMES = 1440000
RATE = 48000


def _validate(beats, downbeats, total, first, end):
    if (type(total) is not int or not 1 <= total <= MAXIMUM_SOURCE_FRAMES
            or type(first) is not int or type(end) is not int or not 0 <= first < end <= total
            or type(beats) is not list or len(beats) > MAXIMUM_EVENTS
            or type(downbeats) is not list or len(downbeats) > MAXIMUM_EVENTS):
        raise ValueError('invalid_input')
    previous = -1
    for beat in beats:
        if type(beat) is not int or not previous < beat < total:
            raise ValueError('invalid_input')
        previous = beat
    previous = -1
    for ordinal in downbeats:
        if type(ordinal) is not int or not previous < ordinal < len(beats):
            raise ValueError('invalid_input')
        previous = ordinal


def _round(value):
    return (2 * value.numerator + value.denominator) // (2 * value.denominator)


def _fit(values):
    n = len(values)
    sx = n * (n - 1) // 2
    sxx = n * (n - 1) * (2 * n - 1) // 6
    period = Fraction(n * sum(i * value for i, value in enumerate(values)) - sx * sum(values),
                      n * sxx - sx * sx)
    if period <= 0:
        raise ValueError('nonpositive_fit')
    intercept = (sum(values) - period * sx) / n
    anchor = _round(intercept)
    tempo = _round(Fraction(RATE * 60000, 1) / period)
    return period, {'intercept_numerator': intercept.numerator,
                    'intercept_denominator': intercept.denominator,
                    'period_numerator': period.numerator, 'period_denominator': period.denominator,
                    'rounded_anchor': anchor, 'tempo_millibpm': tempo}


def _owned_points(anchor_q32, period_q32, first, end, anchor_ordinal, total):
    """Exact Q32 ownership. Internal inputs are already validated model values."""
    if period_q32 <= 0:
        raise ValueError('invalid_period')
    first_index = max(0, -(-(first * Q - anchor_q32) // period_q32))
    last_index = (end * Q - 1 - anchor_q32) // period_q32
    count = max(0, last_index - first_index + 1)
    if count > MAXIMUM_EVENTS:
        raise ValueError('capacity_exceeded')
    points = []
    for j in range(first_index, last_index + 1):
        position, ordinal = anchor_q32 + j * period_q32, anchor_ordinal + j
        if not (0 <= position < total * Q and first * Q <= position < end * Q
                and position < (1 << 64) and 0 <= ordinal < (1 << 63)):
            raise ValueError('coordinate_or_ordinal_overflow')
        points.append({'position_q32': position, 'ordinal': ordinal})
    return (first_index if points else None), points


def _meter(beats, downbeat_ordinals, first, end, g, anchor, period_q32, fitted_period):
    owned = [i for i in downbeat_ordinals if first <= beats[i] < end]
    for left, middle, right in zip(owned, owned[1:], owned[2:]):
        numerator = middle - left
        if numerator not in (3, 4) or right - middle != numerator:
            continue

        def point(index):
            j = _round(Fraction((beats[index] - anchor) * Q, period_q32))
            position = anchor * Q + j * period_q32
            eligible = j >= 0 and abs(Fraction(position, Q) - beats[index]) <= fitted_period / 10
            return j, position, eligible

        if not point(left)[2]:
            continue
        # This is the first legacy-eligible triple. No later triple may rescue
        # an unowned witness; only this same triple's left/middle/right may bind.
        for raw_index in (left, middle, right):
            j, position, eligible = point(raw_index)
            if eligible and first * Q <= position < end * Q:
                return {'kind': 'estimated', 'numerator': numerator, 'denominator': 4,
                        'downbeat_frame': position >> 32, 'downbeat_fraction_q32': position & (Q - 1),
                        'downbeat_ordinal': g + j, 'witness_ordinals': [left, middle, right]}, 'OWNED'
        return None, 'NO_OWNED_WITNESS'
    return None, 'NO_ELIGIBLE_BARS'


def project_window(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame):
    """Fit once; keep the source-valid origin and emit only owned model points.

    Model ordinals do not reassign observed event ordinals or establish source
    continuity. All validation precedes abstention, and inputs are never mutated.
    """
    _validate(beat_frames, downbeat_ordinals, total_frames, first_frame, end_frame)
    observed = [i for i, beat in enumerate(beat_frames) if first_frame <= beat < end_frame]
    g = observed[0] if observed else None
    result = {'format': FORMAT, 'registration_sha256': REGISTRATION_SHA256,
              'total_frames': total_frames, 'first_frame': first_frame, 'end_frame': end_frame,
              'source_beat_count': len(beat_frames), 'first_observed_ordinal': g,
              'observed_count': len(observed), 'status': 'INSUFFICIENT_EVENTS', 'fit': None,
              'model': None, 'first_index': None, 'point_count': 0, 'points': [],
              'meter': None, 'meter_status': 'NOT_EVALUATED'}
    if len(observed) < 4:
        return result
    fitted_period, fit = _fit([beat_frames[i] for i in observed])
    result['fit'] = fit
    anchor, tempo = fit['rounded_anchor'], fit['tempo_millibpm']
    if not 40000 <= tempo <= 300000:
        result['status'] = 'UNSUPPORTED_TEMPO'
        return result
    if not 0 <= anchor < total_frames:
        result['status'] = 'SOURCE_ANCHOR_OUT_OF_DOMAIN'
        return result
    period_q32 = (RATE * 60000 * Q) // tempo
    result['model'] = {'anchor_frame': anchor, 'anchor_ordinal': g,
                       'tempo_millibpm': tempo, 'period_q32': period_q32, 'confidence': 0}
    first_index, points = _owned_points(anchor * Q, period_q32, first_frame, end_frame, g, total_frames)
    if not points:
        result['status'] = 'NO_OWNED_POINTS'
        return result
    result.update(status='ACCEPTED', first_index=first_index, point_count=len(points), points=points)
    result['meter'], result['meter_status'] = _meter(beat_frames, downbeat_ordinals, first_frame,
                                                   end_frame, g, anchor, period_q32, fitted_period)
    return result
