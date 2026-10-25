# SPDX-License-Identifier: Apache-2.0
"""MC-01 generated-only meter context. Pure inputs; unchanged P1 grid."""
from fractions import Fraction

import apta_context_projection as p1

REGISTRATION_SHA256 = '542f5e44a46662f233a013faacdcc66e5254c579e1930cc5ec99c789c10da191'
FORMAT = 'libapta-meter-context-projection-1'
Q = 1 << 32


def project_window(beat_frames, downbeat_ordinals, total_frames, first_frame,
                   end_frame, *, known_gaps):
    """Keep P1-owned meter; otherwise require unambiguous neighboring-bar evidence."""
    if type(known_gaps) is not list or known_gaps:
        raise ValueError('explicit gap-free source required')
    baseline = p1.project_window(beat_frames, downbeat_ordinals, total_frames,
                                 first_frame, end_frame)
    result = dict(baseline, format=FORMAT, registration_sha256=REGISTRATION_SHA256,
                  baseline_meter=baseline['meter'], baseline_meter_status=baseline['meter_status'],
                  meter_origin='P1' if baseline['meter'] is not None else None, context=None)
    if baseline['status'] != 'ACCEPTED' or baseline['meter'] is not None:
        return result

    before = [i for i in downbeat_ordinals if beat_frames[i] < first_frame][-2:]
    inside = [i for i in downbeat_ordinals if first_frame <= beat_frames[i] < end_frame]
    after = [i for i in downbeat_ordinals if beat_frames[i] >= end_frame][:2]
    context = before + inside + after
    model = baseline['model']
    anchor, step, origin = model['anchor_frame'], model['period_q32'], model['anchor_ordinal']
    tolerance = Fraction(baseline['fit']['period_numerator'], baseline['fit']['period_denominator'] * 10)
    owned = {(point['position_q32'], point['ordinal']) for point in baseline['points']}
    mapped = {}
    for i in context:
        value = Fraction((beat_frames[i] - anchor) * Q, step)
        index = (2 * value.numerator + value.denominator) // (2 * value.denominator)
        position = anchor * Q + index * step
        valid = (0 <= position < total_frames * Q and 0 <= origin + index < (1 << 63)
                 and abs(Fraction(position, Q) - beat_frames[i]) <= tolerance)
        mapped[i] = index, position, valid

    candidates, hypotheses = [], set()
    for triple in zip(context, context[1:], context[2:]):
        left, middle, right = triple
        meter = middle - left
        if (meter not in (3, 4) or right - middle != meter
                or not all(mapped[i][2] for i in triple)
                or mapped[middle][0] - mapped[left][0] != meter
                or mapped[right][0] - mapped[middle][0] != meter):
            continue
        witnesses = [i for i in triple if first_frame <= beat_frames[i] < end_frame
                     and (mapped[i][1], origin + mapped[i][0]) in owned]
        if witnesses:
            candidates.append((triple, meter, witnesses[0]))
            hypotheses.add((meter, (origin + mapped[left][0]) % meter))
    result['context'] = {
        'downbeat_ordinals': context, 'examined_triples': max(0, len(context) - 2),
        'eligible_triples': len(candidates),
        'hypotheses': [{'numerator': m, 'phase_modulo': phase} for m, phase in sorted(hypotheses)],
        'lookbehind_frames': max(0, first_frame - beat_frames[context[0]]) if context else 0,
        'lookahead_frames': max(0, beat_frames[context[-1]] - (end_frame - 1)) if context else 0,
    }
    if not candidates:
        result['meter_status'] = 'NO_ELIGIBLE_CONTEXT'
    elif len(hypotheses) != 1:
        result['meter_status'] = 'AMBIGUOUS_CONTEXT'
    else:
        triple, meter, witness = candidates[0]
        index, position, _ = mapped[witness]
        result['meter'] = {'kind': 'estimated', 'numerator': meter, 'denominator': 4,
                          'downbeat_frame': position >> 32, 'downbeat_fraction_q32': position & (Q - 1),
                          'downbeat_ordinal': origin + index, 'witness_ordinals': list(triple)}
        result.update(meter_status='CONTEXT_OWNED', meter_origin='CONTEXT')
    return result
