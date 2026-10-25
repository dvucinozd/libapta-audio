# SPDX-License-Identifier: Apache-2.0
"""Independent public MC-05 oracle for valid inputs; no production imports."""
from fractions import Fraction
from statistics import median

Q = 4294967296


def extract(windows):
    result = []
    for w in windows:
        meter = w['meter']
        selected = []
        if meter is not None:
            # Enumerate whole-bar ordinal solutions instead of filtering modulo.
            ordinals = {p['ordinal']: p for p in w['points']}
            d, m = meter['downbeat_ordinal'], meter['numerator']
            for k in range(-3072, 3073):
                if d + k * m in ordinals:
                    selected.append(dict(ordinals[d + k * m]))
        result.append({'first_frame': w['first_frame'], 'end_frame': w['end_frame'],
                       'model': dict(w['model']) if w['model'] is not None else None,
                       'grid_points': [dict(p) for p in w['points']],
                       'phase_points': selected})
    return result


def evidence(meter, w, truth):
    beats, downbeats = truth['beat_frames'], truth['downbeat_frames']
    score = dict(meter_emitted=0, meter_correct=0, downbeat_scored=0, downbeat_correct=0)
    r = {'first_frame': w['first_frame'], 'end_frame': w['end_frame'],
         'representation': 'context',
         'reference_complete': int(beats[0] <= w['first_frame'] and w['end_frame'] <= beats[-1] + 1),
         'point_count': len(w['points']), 'meter_supported': 0, 'downbeat_supported': 0,
         'legacy_native_unowned_meter_conflict': 0, 'score': score}
    if meter is None:
        return r
    score['meter_emitted'] = r['meter_supported'] = 1
    score['meter_correct'] = int((meter['numerator'], meter['denominator']) ==
                                 (truth['meter_numerator'], truth['meter_denominator']))
    position = Fraction(meter['downbeat_frame']) + Fraction(meter['downbeat_fraction_q32'], Q)
    if beats[0] <= position <= beats[-1]:
        score['downbeat_scored'] = r['downbeat_supported'] = 1
        nearest = sorted(enumerate(downbeats), key=lambda pair: (abs(pair[1] - position), pair[0]))[0][1]
        spans = sorted((abs(a - nearest), index, b - a)
                       for index, (a, b) in enumerate(zip(beats, beats[1:])))[:5]
        threshold = Fraction.from_float(float(median([s[2] for s in spans])) * 0.10)
        score['downbeat_correct'] = int(abs(position - nearest) <= threshold)
    return r


def evaluate(windows, truth):
    result = []
    for w, phase in zip(windows, extract(windows)):
        rows = []
        for p in phase['phase_points']:
            whole, fraction = divmod(p['position_q32'], Q)
            meter = dict(w['meter'], downbeat_frame=whole,
                         downbeat_fraction_q32=fraction, downbeat_ordinal=p['ordinal'])
            rows.append({'point': dict(p), 'evidence': evidence(meter, w, truth)})
        eligible = [r for r in rows if r['evidence']['downbeat_supported']]
        good = [r for r in eligible if r['evidence']['score']['downbeat_correct']]
        result.append({'phase': phase, 'published': evidence(w['meter'], w, truth),
                       'phase_point_count': len(rows), 'supported_point_count': len(eligible),
                       'correct_point_count': len(good), 'E_supported': int(bool(eligible)),
                       'all_correct': int(len(good) == len(eligible)) if eligible else None,
                       'conditional_accuracy': {'numerator': len(good), 'denominator': len(eligible)}
                       if eligible else None, 'point_scores': rows})
    return result
