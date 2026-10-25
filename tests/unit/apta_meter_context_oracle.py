# SPDX-License-Identifier: Apache-2.0
"""Independent MC-01 oracle; frozen rational P1 oracle plus stdlib only."""
from bisect import bisect_left

from apta_context_projection_oracle import independent_project_window

REGISTRATION_SHA256 = '542f5e44a46662f233a013faacdcc66e5254c579e1930cc5ec99c789c10da191'


def project_window_oracle(beat_frames, downbeat_ordinals, total_frames, first_frame,
                          end_frame, *, known_gaps):
    if type(known_gaps) is not list or len(known_gaps) != 0:
        raise ValueError('known gap-free source required')
    old = independent_project_window(beat_frames, downbeat_ordinals, total_frames,
                                      first_frame, end_frame)
    value = {**old, 'format': 'libapta-meter-context-projection-1',
             'registration_sha256': REGISTRATION_SHA256, 'baseline_meter': old['meter'],
             'baseline_meter_status': old['meter_status'], 'meter_origin': None, 'context': None}
    if old['meter'] is not None:
        value['meter_origin'] = 'P1'
    if old['meter'] is not None or old['status'] != 'ACCEPTED':
        return value

    times = [beat_frames[i] for i in downbeat_ordinals]
    lo, hi = bisect_left(times, first_frame), bisect_left(times, end_frame)
    selected = downbeat_ordinals[max(0, lo - 2):min(len(times), hi + 2)]
    quantum = 4294967296
    anchor = old['model']['anchor_frame'] * quantum
    period = old['model']['period_q32']
    g = old['first_observed_ordinal']
    coordinates = {}
    for ordinal in selected:
        frame = beat_frames[ordinal] * quantum
        floor = (frame - anchor) // period
        # Compare exact neighboring candidates, prefer the larger index on a tie.
        index = min((floor, floor + 1), key=lambda j: (abs(anchor + j * period - frame), -j))
        q = anchor + index * period
        good = (q >= 0 and q < total_frames * quantum and 0 <= g + index < 2**63
                and 10 * abs(q - frame) * old['fit']['period_denominator']
                <= old['fit']['period_numerator'] * quantum)
        coordinates[ordinal] = index, q, good

    options = []
    for offset in range(max(0, len(selected) - 2)):
        triple = selected[offset:offset + 3]
        n = triple[1] - triple[0]
        if n not in (3, 4) or triple[2] - triple[1] != n:
            continue
        data = [coordinates[i] for i in triple]
        if (not all(item[2] for item in data)
                or [data[1][0] - data[0][0], data[2][0] - data[1][0]] != [n, n]):
            continue
        owners = [i for i in triple if first_frame <= beat_frames[i] < end_frame and any(
            point['position_q32'] == coordinates[i][1] and point['ordinal'] == g + coordinates[i][0]
            for point in old['points'])]
        if owners:
            options.append((triple, n, owners[0], (g + data[0][0]) % n))
    alternatives = sorted({(item[1], item[3]) for item in options})
    value['context'] = {'downbeat_ordinals': list(selected), 'examined_triples': max(0, len(selected) - 2),
                        'eligible_triples': len(options),
                        'hypotheses': [{'numerator': n, 'phase_modulo': phase} for n, phase in alternatives],
                        'lookbehind_frames': max([0] + [first_frame - beat_frames[i] for i in selected]),
                        'lookahead_frames': max([0] + [beat_frames[i] - end_frame + 1 for i in selected])}
    if len(alternatives) == 1:
        triple, n, ordinal, _ = options[0]
        index, q, _ = coordinates[ordinal]
        value['meter'] = {'kind': 'estimated', 'numerator': n, 'denominator': 4,
                          'downbeat_frame': q // quantum, 'downbeat_fraction_q32': q % quantum,
                          'downbeat_ordinal': g + index, 'witness_ordinals': triple}
        value['meter_origin'], value['meter_status'] = 'CONTEXT', 'CONTEXT_OWNED'
    else:
        value['meter_status'] = 'AMBIGUOUS_CONTEXT' if alternatives else 'NO_ELIGIBLE_CONTEXT'
    return value
