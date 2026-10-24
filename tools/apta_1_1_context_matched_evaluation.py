# SPDX-License-Identifier: Apache-2.0
"""P5 evaluation process only. Canonical scoring remains unchanged."""
from collections import Counter
import json

import apta_context_matched as matched
import apta_1_1_dense_decoder as old
import apta_1_1_coupled_time_evaluation_v2_metadata as metadata
import apta_1_1_event_representation as representation
import apta_1_1_neural_comparison as comparison
from apta_reference_support import reference_support


def exact(value):
    return json.dumps(value, sort_keys=True, allow_nan=False, separators=(',', ':'))


def warm():
    # These canonical helpers are imported lazily by the old scoring functions.
    # Load their code before freezing this phase's actual runtime closure.
    import apta_1_1_learned_piecewise
    import apta_1_1_coupled_time_v2_metadata
    public = {'beat_frames': [24000 * i for i in range(48)],
              'downbeat_frames': [96000 * i for i in range(12)]}
    comparison.event_metrics(public, public)


def _cell(events, window, projection, truth, kind):
    first, end = window['first_frame'], window['end_frame']
    if kind == 'context':
        points = projection['points']
        meter = projection['meter']
        meter = {k: meter[k] for k in ('numerator', 'denominator', 'downbeat_frame',
                 'downbeat_fraction_q32', 'downbeat_ordinal')} if meter else None
        nominal = [(p['position_q32'], projection['model']['period_q32']) for p in points]
    else:
        nominal = representation.points_for_control(projection)
        g = next((i for i, frame in enumerate(events['beat_frames']) if first <= frame < end), 0)
        points = [{'position_q32': q, 'ordinal': g + i} for i, (q, _) in enumerate(nominal)]
        meter = None
        if projection['meter']:
            raw = projection['meter']
            q = raw['downbeat_frame'] * matched.Q + raw['downbeat_fraction_q32']
            j, remainder = divmod(q - projection['selected']['anchor_frame'] * matched.Q, nominal[0][1])
            if remainder or j < 0:
                raise ValueError('legacy meter does not identify its lattice')
            meter = {k: raw[k] for k in ('numerator', 'denominator', 'downbeat_frame', 'downbeat_fraction_q32')}
            meter['downbeat_ordinal'] = g + j
    reference = {k: truth[k] for k in ('beat_frames', 'downbeat_frames', 'meter_numerator', 'meter_denominator')}
    evidence = matched.meter_evidence(meter, points, first, end, reference, kind)
    grid = representation.score_points(nominal, truth, first, end)
    return {'normalized': matched.make_cell(grid, evidence), 'grid_score': grid,
            'meter_score': evidence['score'], 'points': points, 'meter': meter}


def evaluate_source(events, candidate, windows, truth, saved, coarse, frozen_score, projections):
    # Reproduce the complete old diagnostic channel, including support metadata.
    support = reference_support(truth['beat_frames'], windows)
    previous = metadata.add_support(old.score_source(candidate, events, windows, truth, saved, coarse), support)
    if exact(previous) != exact(frozen_score['scores']):
        raise ValueError('frozen legacy diagnostic parity differs')
    cells = {a + '_' + r: [] for a in ('original', 'coupled') for r in ('legacy', 'context')}
    actual, successors = {}, {}
    for arm, raw in (('original', events), ('coupled', candidate['events'])):
        beats = raw['beat_frames']
        actual[arm], successors[arm] = [], []
        for i, window in enumerate(windows):
            first, end = window['first_frame'], window['end_frame']
            legacy = representation.ref.project(beats, raw['downbeat_frames'], first, end)
            context = projections[arm][i]
            before = _cell(raw, window, legacy, truth, 'legacy')
            after = _cell(raw, window, context, truth, 'context')
            for domain in ('G', 'M', 'D'):
                if before['normalized'][domain] > after['normalized'][domain]:
                    raise ValueError('within-arm projection support loss')
            if before['normalized']['G']:
                if before['points'] != after['points'] or any(
                        legacy['selected'][k] != context['model'][k]
                        for k in ('anchor_frame', 'tempo_millibpm', 'confidence')):
                    raise ValueError('legacy-owned grid/model tuple changed')
            if before['normalized']['M'] and before['meter'] != after['meter']:
                raise ValueError('legacy-owned meter tuple changed')
            cells[arm + '_legacy'].append(before)
            cells[arm + '_context'].append(after)
            raw_points = representation.points_for_events(beats, first, end)
            actual[arm].append(representation.score_points(raw_points, truth, first, end))
            successors[arm].append({'points': len(raw_points),
                'known_successor_points_all': sum(interval is not None for _, interval in raw_points),
                'unknown_successor_points_all': sum(interval is None for _, interval in raw_points)})
    return {'cells': cells, 'raw_actual': actual, 'raw_successor_availability': successors,
            'event_metrics': {'original': previous['old_event_metrics'], 'coupled': previous['candidate_event_metrics']},
            'reference_support': support, 'frozen_diagnostic_parity': True}


def canonical_totals(rows):
    totals = {}
    for key in rows[0]['cells']:
        totals[key] = {}
        for stratum in ('all', 'reference_complete', 'reference_incomplete'):
            counter = Counter(dict.fromkeys((*matched.SCORE_KEYS, *matched.METER_KEYS, 'reference_incomplete_windows'), 0))
            for row in rows:
                for c in row['cells'][key]:
                    complete = c['normalized']['reference_complete']
                    if stratum != 'all' and complete != int(stratum == 'reference_complete'):
                        continue
                    counter.update(c['grid_score'])
                    counter.update(c['meter_score'])
                    counter['reference_incomplete_windows'] += 1 - complete
            totals[key][stratum] = dict(counter)
    return totals


def summarize(rows, groups):
    if len(rows) != len(groups) or not rows:
        raise ValueError('source/group denominator differs')
    metrics = ('beat_f1', 'downbeat_f1', 'beat_cmlt', 'downbeat_cmlt')
    cells = {key: [c['normalized'] for row in rows for c in row['cells'][key]] for key in rows[0]['cells']}
    comparisons = {'candidate_vs_original_legacy': ('original_legacy', 'coupled_legacy'),
                   'candidate_vs_original_context': ('original_context', 'coupled_context'),
                   'context_vs_legacy_original': ('original_legacy', 'original_context'),
                   'context_vs_legacy_coupled': ('coupled_legacy', 'coupled_context')}
    ledgers, canonical_by_level = [], []

    def emit(selected, level, name):
        canonical_by_level.append({'level': level, 'group': name, 'cells': canonical_totals([rows[i] for i in selected])})
        selected_cells = {key: [c['normalized'] for i in selected for c in rows[i]['cells'][key]] for key in cells}
        for comparison_name, (left, right) in comparisons.items():
            for family in matched.FAMILIES:
                for stratum in ('all', 'reference_complete', 'reference_incomplete'):
                    ledgers.append({'group': name, 'ledger': matched.make_ledger(selected_cells[left],
                        selected_cells[right], comparison_name, family, stratum, level)})
    for i in range(len(rows)):
        emit([i], 'source', str(i))
    for group in sorted(set(groups)):
        emit([i for i, name in enumerate(groups) if name == group], 'group', group)
    emit(list(range(len(rows))), 'cohort', 'all')
    totals = canonical_totals(rows)
    means = {arm: {k: sum(row['event_metrics'][arm][k] for row in rows) / len(rows) for k in metrics}
             for arm in ('original', 'coupled')}
    breaks = {k: 0 for k in ('phase', 'period', 'joint', 'complete_sequence')}
    for row in rows:
        for a, b in zip(row['raw_actual']['original'], row['raw_actual']['coupled']):
            for key in breaks:
                breaks[key] += int(bool(a[key + '_windows']) and not b[key + '_windows'])
    faults = []
    for entry in ledgers:
        p = entry['ledger']
        if p['level'] != 'cohort' or p['comparison'] != 'candidate_vs_original_context':
            continue
        if p['stratum'] == 'all' and p['family'] in ('grid_joint', 'meter', 'downbeat') and p['counts']['left_only_supported']:
            faults.append(p['family'] + '_support_loss')
        target = 'reference_complete' if p['family'].startswith('grid_') else 'all'
        if p['stratum'] == target and p['counts']['breaks_on_both']:
            faults.append(p['family'] + '_shared_correctness_break')
    faults += ['raw_actual_' + k + '_break' for k, value in breaks.items() if value]
    faults += ['event_' + k + '_decrease' for k in metrics if means['coupled'][k] + 1e-12 < means['original'][k]]
    windows = len(cells['original_context'])
    complete = sum(c['reference_complete'] for c in cells['original_context'])
    return {'sources': len(rows), 'windows': windows, 'reference_complete_windows': complete,
            'reference_incomplete_windows': windows - complete, 'evaluated_cells': windows * 4,
            'cell_totals': totals, 'cell_totals_by_level': canonical_by_level,
            'ledgers': ledgers, 'event_means': means, 'raw_actual_breaks': breaks,
            'pair_screen': 'PAIR_REJECTED_ON_GENERATED_DIAGNOSTICS' if faults else 'PAIR_NOT_REJECTED_ON_GENERATED_DIAGNOSTICS',
            'pair_screen_faults': faults, 'legacy_verdict_preserved': 'REJECTED_14_OF_16',
            'formal_acceptance': False, 'production_activation': False}
