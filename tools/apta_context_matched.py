# SPDX-License-Identifier: Apache-2.0
"""P4 pure evaluation helpers; no I/O, imports, projector or campaign dispatch.

Inputs are normalized verified evaluation facts, never constructor inputs.
Source identity and projection oracle verification remain the future runner's job.
"""
PROTOCOL_SHA256 = 'c8b17d5bda059ec96260180485f18ce59712db47c9d4037c9180e1917480c6e8'
Q = 1 << 32
MAX_FRAME = 1440000
FAMILIES = ('grid_phase', 'grid_period', 'grid_joint', 'grid_complete_sequence', 'meter', 'downbeat')
SCORE_KEYS = ('windows', 'reference_complete_windows', 'points', 'scored_points',
              'phase_correct_points', 'period_correct_points', 'unknown_successor_points',
              'phase_windows', 'period_windows', 'joint_windows', 'one_to_one_phase_windows',
              'complete_sequence_windows')
METER_KEYS = ('meter_emitted', 'meter_correct', 'downbeat_scored', 'downbeat_correct')
COUNT_KEYS = ('windows', 'reference_complete_windows', 'reference_incomplete_windows',
              'both_supported', 'left_only_supported', 'right_only_supported', 'neither_supported',
              'correct_both', 'fixes_on_both', 'breaks_on_both', 'incorrect_both',
              'left_correct_on_both', 'right_correct_on_both', 'left_only_correct',
              'right_only_correct', 'legacy_native_unowned_meter_conflicts')
COMPARISONS = (('candidate_vs_original_legacy', 'legacy', 'legacy'),
               ('candidate_vs_original_context', 'context', 'context'),
               ('context_vs_legacy_original', 'legacy', 'context'),
               ('context_vs_legacy_coupled', 'legacy', 'context'))


def _integer(value, lower, upper):
    if type(value) is not int or not lower <= value <= upper:
        raise ValueError('invalid exact integer')


def _shape(value, keys):
    if type(value) is not dict or set(value) != set(keys):
        raise ValueError('invalid closed record')


def _geometry(first, end):
    _integer(first, 0, MAX_FRAME - 1)
    _integer(end, first + 1, MAX_FRAME)


def _events(values, minimum):
    if type(values) is not list or not minimum <= len(values) <= 3072:
        raise ValueError('invalid reference event capacity')
    previous = -1
    for value in values:
        _integer(value, previous + 1, MAX_FRAME)
        previous = value


def _truth(truth):
    _shape(truth, ('beat_frames', 'downbeat_frames', 'meter_numerator', 'meter_denominator'))
    _events(truth['beat_frames'], 2)
    _events(truth['downbeat_frames'], 1)
    _integer(truth['meter_numerator'], 1, 32)
    if type(truth['meter_denominator']) is not int or truth['meter_denominator'] not in (1, 2, 4, 8, 16, 32):
        raise ValueError('unknown reference meter')
    if not set(truth['downbeat_frames']) <= set(truth['beat_frames']):
        raise ValueError('reference downbeats outside beat events')


def _points(points, first, end):
    if type(points) is not list or len(points) > 3072:
        raise ValueError('invalid owned point capacity')
    previous_q, previous_ordinal = -1, -1
    for point in points:
        _shape(point, ('position_q32', 'ordinal'))
        _integer(point['position_q32'], max(first * Q, previous_q + 1), end * Q - 1)
        _integer(point['ordinal'], previous_ordinal + 1, (1 << 63) - 1)
        previous_q, previous_ordinal = point['position_q32'], point['ordinal']


def _local_period(beats, reference):
    # Same stable source-order ties and median as canonical _local_period.
    indices = sorted(range(len(beats) - 1), key=lambda i: abs(beats[i] - reference))[:5]
    gaps = sorted(beats[i + 1] - beats[i] for i in indices)
    middle = len(gaps) // 2
    return (gaps[(len(gaps) - 1) // 2] + gaps[middle]) / 2


def meter_evidence(meter, points, first_frame, end_frame, truth, representation):
    """Bind a normalized meter to an owned point, then score only meter fields.

    Meter is None or exactly numerator/denominator/downbeat_frame/
    downbeat_fraction_q32/downbeat_ordinal. Points use P1's position_q32/ordinal.
    Legacy-unowned meter is an explicit conflict, never matched correctness.
    """
    _geometry(first_frame, end_frame)
    _truth(truth)
    _points(points, first_frame, end_frame)
    if type(representation) is not str or representation not in ('legacy', 'context'):
        raise ValueError('invalid representation')
    complete = int(truth['beat_frames'][0] <= first_frame and end_frame - 1 <= truth['beat_frames'][-1])
    result = {'first_frame': first_frame, 'end_frame': end_frame, 'representation': representation,
              'reference_complete': complete, 'point_count': len(points), 'meter_supported': 0,
              'downbeat_supported': 0, 'legacy_native_unowned_meter_conflict': 0,
              'score': {key: 0 for key in METER_KEYS}}
    if meter is None:
        return result
    if not points:
        raise ValueError('published meter requires a grid')
    _shape(meter, ('numerator', 'denominator', 'downbeat_frame', 'downbeat_fraction_q32', 'downbeat_ordinal'))
    if type(meter['numerator']) is not int or meter['numerator'] not in (3, 4):
        raise ValueError('invalid published meter')
    _integer(meter['denominator'], 4, 4)
    _integer(meter['downbeat_frame'], 0, (1 << 64) - 1)
    _integer(meter['downbeat_fraction_q32'], 0, Q - 1)
    _integer(meter['downbeat_ordinal'], 0, (1 << 63) - 1)
    q = meter['downbeat_frame'] * Q + meter['downbeat_fraction_q32']
    if not first_frame * Q <= q < end_frame * Q:
        if representation != 'legacy':
            raise ValueError('context meter is unowned')
        result['legacy_native_unowned_meter_conflict'] = 1
        return result
    if not any(p['position_q32'] == q and p['ordinal'] == meter['downbeat_ordinal'] for p in points):
        raise ValueError('meter does not identify an owned grid point')
    result['meter_supported'] = 1
    result['score']['meter_emitted'] = 1
    result['score']['meter_correct'] = int(meter['numerator'] == truth['meter_numerator']
                                         and meter['denominator'] == truth['meter_denominator'])
    frame = meter['downbeat_frame'] + meter['downbeat_fraction_q32'] / Q
    if truth['beat_frames'][0] <= frame <= truth['beat_frames'][-1]:
        result['downbeat_supported'] = 1
        result['score']['downbeat_scored'] = 1
        nearest = min(truth['downbeat_frames'], key=lambda value: abs(value - frame))
        local = _local_period(truth['beat_frames'], nearest)
        result['score']['downbeat_correct'] = int(abs(frame - nearest) <= local * .10)
    return result


def _meter_evidence(value):
    _shape(value, ('first_frame', 'end_frame', 'representation', 'reference_complete', 'point_count',
                   'meter_supported', 'downbeat_supported', 'legacy_native_unowned_meter_conflict', 'score'))
    _geometry(value['first_frame'], value['end_frame'])
    if value['representation'] not in ('legacy', 'context') or type(value['representation']) is not str:
        raise ValueError('invalid representation')
    for key in ('reference_complete', 'meter_supported', 'downbeat_supported', 'legacy_native_unowned_meter_conflict'):
        _integer(value[key], 0, 1)
    _integer(value['point_count'], 0, 3072)
    _shape(value['score'], METER_KEYS)
    for v in value['score'].values():
        _integer(v, 0, 1)
    s = value['score']
    if (s['meter_emitted'] != value['meter_supported'] or s['downbeat_scored'] != value['downbeat_supported']
            or s['meter_correct'] > s['meter_emitted'] or s['downbeat_correct'] > s['downbeat_scored']
            or value['downbeat_supported'] > value['meter_supported']
            or value['meter_supported'] and not value['point_count']
            or value['legacy_native_unowned_meter_conflict'] and
               (value['representation'] != 'legacy' or value['meter_supported'] or not value['point_count'])):
        raise ValueError('inconsistent meter evidence')


def make_cell(grid_score, evidence):
    """Normalize canonical nominal score_points counters and checked meter evidence.

    This checks structural consistency, not canonical grid-score recomputation.
    The caller must use the registered canonical scorer and verified projections.
    Nominal points have known periods; raw actual successor masks stay separate.
    """
    _meter_evidence(evidence)
    _shape(grid_score, SCORE_KEYS)
    for v in grid_score.values():
        _integer(v, 0, 3072)
    s, n, scored = grid_score, grid_score['points'], grid_score['scored_points']
    for key in ('phase_windows', 'period_windows', 'joint_windows', 'one_to_one_phase_windows', 'complete_sequence_windows'):
        _integer(s[key], 0, 1)
    full = bool(evidence['reference_complete'] and n and scored == n)
    if (s['windows'] != 1 or s['reference_complete_windows'] != evidence['reference_complete']
            or n != evidence['point_count'] or not s['unknown_successor_points'] <= scored <= n
            or s['unknown_successor_points'] != 0
            or s['phase_correct_points'] > scored
            or s['period_correct_points'] > scored - s['unknown_successor_points']
            or s['phase_windows'] != int(full and s['phase_correct_points'] == n)
            or s['period_windows'] != int(full and s['period_correct_points'] == n)
            or s['joint_windows'] != s['phase_windows'] * s['period_windows']
            or s['one_to_one_phase_windows'] > s['phase_windows']
            or s['complete_sequence_windows'] != s['one_to_one_phase_windows'] * s['period_windows']):
        raise ValueError('canonical grid counters inconsistent with evidence')
    return {'first_frame': evidence['first_frame'], 'end_frame': evidence['end_frame'],
            'representation': evidence['representation'], 'reference_complete': evidence['reference_complete'],
            'G': int(n > 0), 'M': evidence['meter_supported'], 'D': evidence['downbeat_supported'],
            'legacy_native_unowned_meter_conflict': evidence['legacy_native_unowned_meter_conflict'],
            'correct': {'grid_phase': s['phase_windows'], 'grid_period': s['period_windows'],
                        'grid_joint': s['joint_windows'], 'grid_complete_sequence': s['complete_sequence_windows'],
                        'meter': evidence['score']['meter_correct'], 'downbeat': evidence['score']['downbeat_correct']}}


def _cell(value):
    _shape(value, ('first_frame', 'end_frame', 'representation', 'reference_complete', 'G', 'M', 'D',
                   'legacy_native_unowned_meter_conflict', 'correct'))
    _geometry(value['first_frame'], value['end_frame'])
    if type(value['representation']) is not str or value['representation'] not in ('legacy', 'context'):
        raise ValueError('invalid representation')
    for key in ('reference_complete', 'G', 'M', 'D', 'legacy_native_unowned_meter_conflict'):
        _integer(value[key], 0, 1)
    _shape(value['correct'], FAMILIES)
    for v in value['correct'].values():
        _integer(v, 0, 1)
    c = value['correct']
    if (not value['D'] <= value['M'] <= value['G']
            or any(c[k] > value['G'] * value['reference_complete'] for k in FAMILIES[:4])
            or c['grid_joint'] != c['grid_phase'] * c['grid_period']
            or c['grid_complete_sequence'] > c['grid_joint'] or c['meter'] > value['M']
            or c['downbeat'] > value['D']
            or value['legacy_native_unowned_meter_conflict'] and
               (value['representation'] != 'legacy' or value['M'] or not value['G'])):
        raise ValueError('inconsistent cell support/correctness')


def make_ledger(left_cells, right_cells, comparison, family, stratum='all', level='cohort'):
    """Paired counts; validate every aligned cell before selecting a stratum.

    Geometry/context alignment is checked here. Source identity/order must also
    be bound by the caller. Re-run over combined cells to aggregate; never average rates.
    """
    pair = next((p for p in COMPARISONS if p[0] == comparison), None)
    if (type(comparison) is not str or pair is None or type(family) is not str or family not in FAMILIES
            or type(stratum) is not str or stratum not in ('all', 'reference_complete', 'reference_incomplete')
            or type(level) is not str or level not in ('source', 'group', 'cohort')
            or type(left_cells) is not list or type(right_cells) is not list
            or len(left_cells) != len(right_cells) or len(left_cells) > (8 if level == 'source' else 600)):
        raise ValueError('invalid comparison/record capacity')
    counts = {key: 0 for key in COUNT_KEYS}
    domain = 'G' if family.startswith('grid_') else 'M' if family == 'meter' else 'D'
    for left, right in zip(left_cells, right_cells):
        _cell(left)
        _cell(right)
        if (left['representation'] != pair[1] or right['representation'] != pair[2]
                or any(left[k] != right[k] for k in ('first_frame', 'end_frame', 'reference_complete'))):
            raise ValueError('paired representation or geometry differs')
        complete = left['reference_complete']
        if stratum != 'all' and complete != int(stratum == 'reference_complete'):
            continue
        ls, rs = left[domain], right[domain]
        lc, rc = left['correct'][family], right['correct'][family]
        counts['windows'] += 1
        counts['reference_complete_windows'] += complete
        counts['reference_incomplete_windows'] += 1 - complete
        if domain != 'G':
            counts['legacy_native_unowned_meter_conflicts'] += int(
                bool(left['legacy_native_unowned_meter_conflict'] or right['legacy_native_unowned_meter_conflict']))
        if ls and rs:
            counts['both_supported'] += 1
            counts['left_correct_on_both'] += lc
            counts['right_correct_on_both'] += rc
            counts['correct_both'] += lc * rc
            counts['fixes_on_both'] += (1 - lc) * rc
            counts['breaks_on_both'] += lc * (1 - rc)
            counts['incorrect_both'] += (1 - lc) * (1 - rc)
        elif ls:
            counts['left_only_supported'] += 1
            counts['left_only_correct'] += lc
        elif rs:
            counts['right_only_supported'] += 1
            counts['right_only_correct'] += rc
        else:
            counts['neither_supported'] += 1
    both = counts['both_supported']
    return {'format': 'libapta-context-matched-ledger-1', 'protocol_sha256': PROTOCOL_SHA256,
            'scope': 'GENERATED_C3_DIAGNOSTIC', 'comparison': comparison, 'family': family,
            'stratum': stratum, 'level': level, 'counts': counts,
            'left_conditional_accuracy': {'numerator': counts['left_correct_on_both'], 'denominator': both} if both else None,
            'right_conditional_accuracy': {'numerator': counts['right_correct_on_both'], 'denominator': both} if both else None}
