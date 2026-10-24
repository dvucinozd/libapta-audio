# SPDX-License-Identifier: Apache-2.0
"""Test-only P4 oracle: exact distances and finite set partitions, no production imports.

Valid normalized inputs only; malformed-input rejection belongs to separate tests.
"""
from fractions import Fraction
from statistics import median

HASH = 'c8b17d5bda059ec96260180485f18ce59712db47c9d4037c9180e1917480c6e8'


def meter_evidence_oracle(meter, points, first_frame, end_frame, truth, representation):
    record = {'first_frame': first_frame, 'end_frame': end_frame, 'representation': representation,
              'reference_complete': int(first_frame >= truth['beat_frames'][0]
                                        and end_frame <= truth['beat_frames'][-1] + 1),
              'point_count': len(points), 'meter_supported': 0, 'downbeat_supported': 0,
              'legacy_native_unowned_meter_conflict': 0,
              'score': {'meter_emitted': 0, 'meter_correct': 0, 'downbeat_scored': 0, 'downbeat_correct': 0}}
    if meter is None:
        return record
    position = Fraction(meter['downbeat_frame']) + Fraction(meter['downbeat_fraction_q32'], 4294967296)
    if position < first_frame or position >= end_frame:
        assert representation == 'legacy'
        record['legacy_native_unowned_meter_conflict'] = 1
        return record
    assert (position * 4294967296, meter['downbeat_ordinal']) in {
        (p['position_q32'], p['ordinal']) for p in points}
    record['meter_supported'] = 1
    record['score']['meter_emitted'] = 1
    record['score']['meter_correct'] = int((meter['numerator'], meter['denominator']) ==
                                          (truth['meter_numerator'], truth['meter_denominator']))
    if truth['beat_frames'][0] <= position <= truth['beat_frames'][-1]:
        record['downbeat_supported'] = 1
        record['score']['downbeat_scored'] = 1
        nearest = sorted(enumerate(truth['downbeat_frames']), key=lambda pair:
                         (abs(Fraction(pair[1]) - position), pair[0]))[0][1]
        intervals = [(abs(a - nearest), i, b - a) for i, (a, b) in
                     enumerate(zip(truth['beat_frames'], truth['beat_frames'][1:]))]
        local = float(median([row[2] for row in sorted(intervals)[:5]]))
        # Keep canonical binary64 tolerance, but compare with exact Q32 distance.
        tolerance = Fraction.from_float(local * .10)
        record['score']['downbeat_correct'] = int(abs(position - nearest) <= tolerance)
    return record


def cell_oracle(grid, evidence):
    return {'first_frame': evidence['first_frame'], 'end_frame': evidence['end_frame'],
            'representation': evidence['representation'], 'reference_complete': evidence['reference_complete'],
            'G': int(grid['points'] != 0), 'M': evidence['meter_supported'], 'D': evidence['downbeat_supported'],
            'legacy_native_unowned_meter_conflict': evidence['legacy_native_unowned_meter_conflict'],
            'correct': {'grid_phase': grid['phase_windows'], 'grid_period': grid['period_windows'],
                        'grid_joint': grid['joint_windows'], 'grid_complete_sequence': grid['complete_sequence_windows'],
                        'meter': evidence['score']['meter_correct'], 'downbeat': evidence['score']['downbeat_correct']}}


def ledger_oracle(left, right, comparison, family, stratum='all', level='cohort'):
    indices = {i for i, c in enumerate(left) if stratum == 'all'
               or c['reference_complete'] == int(stratum == 'reference_complete')}
    domain = {'meter': 'M', 'downbeat': 'D'}.get(family, 'G')
    ls = {i for i in indices if left[i][domain]}
    rs = {i for i in indices if right[i][domain]}
    lc = {i for i in indices if left[i]['correct'][family]}
    rc = {i for i in indices if right[i]['correct'][family]}
    shared = ls & rs
    complete = {i for i in indices if left[i]['reference_complete']}
    conflicts = {i for i in indices if left[i]['legacy_native_unowned_meter_conflict']
                 or right[i]['legacy_native_unowned_meter_conflict']} if domain != 'G' else set()
    counts = {'windows': len(indices), 'reference_complete_windows': len(complete),
              'reference_incomplete_windows': len(indices - complete), 'both_supported': len(shared),
              'left_only_supported': len(ls - rs), 'right_only_supported': len(rs - ls),
              'neither_supported': len(indices - (ls | rs)), 'correct_both': len(shared & lc & rc),
              'fixes_on_both': len((shared & rc) - lc), 'breaks_on_both': len((shared & lc) - rc),
              'incorrect_both': len(shared - (lc | rc)), 'left_correct_on_both': len(shared & lc),
              'right_correct_on_both': len(shared & rc), 'left_only_correct': len((ls - rs) & lc),
              'right_only_correct': len((rs - ls) & rc), 'legacy_native_unowned_meter_conflicts': len(conflicts)}
    return {'format': 'libapta-context-matched-ledger-1', 'protocol_sha256': HASH,
            'scope': 'GENERATED_C3_DIAGNOSTIC', 'comparison': comparison, 'family': family, 'stratum': stratum,
            'level': level, 'counts': counts,
            'left_conditional_accuracy': {'numerator': len(shared & lc), 'denominator': len(shared)} if shared else None,
            'right_conditional_accuracy': {'numerator': len(shared & rc), 'denominator': len(shared)} if shared else None}
