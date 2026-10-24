#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect the fixed saved generated R4; no candidate or scoring dispatch."""
import argparse
import ast
from collections import Counter
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import time

import apta_1_1_coupled_time_v2 as records
import apta_1_1_neural_reference as reference
import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs/rust'
REGISTRATION = DOCS / 'COUPLED-TIME-R5-AUDIT-PROTOCOL-20261024.json'
REGISTRATION_SHA256 = '097938364d6b5d83525d9138c20016329a15fcdbf6cb86cd00a6c89209939c11'
SAVED = Path.home() / '.local/share/libapta-audio/rust-rewrite/coupled-time-v2-r4-frozen-20261024'


def read_saved(path, digest, bindings, base=SAVED):
    path = records.safe_path(path)
    if not path.is_relative_to(base) or path.name.startswith('labels.'):
        raise ValueError('only registered generated artifacts; labels forbidden')
    return records.read_bound(path, digest, bindings)


def support_summary(totals):
    total, complete = totals['windows'], totals['reference_complete_windows']
    if type(total) is not int or type(complete) is not int or not 0 <= complete <= total:
        raise ValueError('invalid support counts')
    emitted = 'reference_incomplete_windows' in totals
    if emitted and (type(totals['reference_incomplete_windows']) is not int
                    or totals['reference_incomplete_windows'] != total - complete):
        raise ValueError('inconsistent explicit support partition')
    return {'windows': total, 'reference_context_complete': complete,
            'reference_context_not_complete': total - complete,
            'incomplete_counter_emitted': emitted,
            'serialized_incomplete_counter': totals.get('reference_incomplete_windows'),
            'missing_counter_lookup_default': 0 if not emitted else None}


def emitted_keys(source):
    tree = ast.parse(source)
    function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'score_points')
    value = next(n.value for n in ast.walk(function) if isinstance(n, ast.Return))
    if not isinstance(value, ast.Dict) or not all(isinstance(key, ast.Constant) for key in value.keys):
        raise ValueError('support emitter shape changed')
    return {key.value for key in value.keys}


def topology(events):
    beats, downbeats = events['beat_frames'], events['downbeat_frames']
    if (not beats or any(type(value) is not int for value in beats + downbeats)
            or any(a >= b for a, b in zip(beats, beats[1:]))
            or any(a >= b for a, b in zip(downbeats, downbeats[1:]))):
        raise ValueError('invalid saved event order')
    index = {frame: i for i, frame in enumerate(beats)}
    if not set(downbeats) <= set(beats):
        raise ValueError('downbeat outside saved beat sequence')
    return [index[frame] for frame in downbeats]


def projection_trace(events, window, saved_projection):
    """Replay only the unchanged label-blind projector; trace its exact guard."""
    first, end = window['first_frame'], window['end_frame']
    projected = reference.project(events['beat_frames'], events['downbeat_frames'], first, end)
    if projected != saved_projection:
        raise ValueError('saved projection does not exactly replay')
    inside = [frame for frame in events['beat_frames'] if first <= frame < end]
    result = {'selected_kind': projected['selected']['kind'], 'inside_events': len(inside),
              'meter_present': bool(projected['meter']), 'canonical_projection_replayed': True}
    if len(inside) < 4:
        result['guard'] = 'INSUFFICIENT_EVENTS'
        return result
    n = len(inside)
    sx, sxx = n * (n - 1) // 2, n * (n - 1) * (2 * n - 1) // 6
    period = Fraction(n * sum(i * frame for i, frame in enumerate(inside)) - sx * sum(inside), n * sxx - sx * sx)
    anchor = reference.rounded(Fraction(sum(inside), n) - period * Fraction(sx, n))
    tempo = reference.rounded(Fraction(reference.RATE * 60000, 1) / period)
    tempo_ok, anchor_ok = 40000 <= tempo <= 300000, first <= anchor < end
    guard = 'ACCEPTED' if tempo_ok and anchor_ok else 'TEMPO_RANGE' if not tempo_ok else 'ANCHOR_BEFORE_WINDOW' if anchor < first else 'ANCHOR_AFTER_WINDOW'
    if (guard == 'ACCEPTED') != (result['selected_kind'] == 'accepted'):
        raise ValueError('projection guard trace disagrees with canonical output')
    result.update(guard=guard, tempo_in_registered_range=tempo_ok, anchor_in_window=anchor_ok,
                  # These coordinates remain exclusively in the private receipt.
                  rounded_anchor_frame=anchor, anchor_offset_from_window_start=anchor - first,
                  tempo_millibpm=tempo, period_numerator=period.numerator, period_denominator=period.denominator)
    return result


def inspect_case(before, after, window, old_projection, new_projection):
    old_ordinals, new_ordinals = topology(before), topology(after)
    same_topology = len(before['beat_frames']) == len(after['beat_frames']) and old_ordinals == new_ordinals
    first, end = window['first_frame'], window['end_frame']
    membership = lambda events: [i for i, frame in enumerate(events['beat_frames']) if first <= frame < end]
    return {'same_source_topology': same_topology,
            'same_window_event_membership': membership(before) == membership(after),
            'old': projection_trace(before, window, old_projection),
            'candidate': projection_trace(after, window, new_projection)}


def run(output):
    started = time.monotonic()
    output = records.safe_path(output)
    io.external(output, ROOT)
    if output.exists() or SAVED.is_relative_to(output) or output.is_relative_to(SAVED) or ROOT.is_relative_to(output):
        raise ValueError('new disjoint external audit output required')
    bindings = {}
    protocol = records.read_bound(REGISTRATION, REGISTRATION_SHA256, bindings)
    if (protocol['audit_execution_enabled'] is not True or protocol['scope'] != 'SAVED_GENERATED_R4_ONLY'
            or protocol['execution_enabled'] is not False or protocol['real_execution_enabled'] is not False):
        raise ValueError('registered read-only audit scope required')
    for name, digest in protocol['binding'].items():
        io.bind(DOCS / name, digest, bindings)
    operational = json.loads((DOCS / 'COUPLED-TIME-RUNNER-V2-PROTOCOL.json').read_text())
    for name, digest in operational['binding'].items():
        io.bind(DOCS / name, digest, bindings)
    revision = io.git(ROOT, 'rev-parse', 'HEAD')
    io.check_source(ROOT, revision)
    limits = protocol['limits']
    resource.setrlimit(resource.RLIMIT_AS, (limits['max_process_rss_kib'] * 1024,) * 2)
    frozen = json.loads((DOCS / 'COUPLED-TIME-RUNNER-V2-RUNTIME-PREP-20261024.json').read_text())
    for name, digest in frozen['component_sha256'].items():
        io.bind(ROOT / name, digest, bindings)
    io.bind(Path(__file__), io.sha(__file__), bindings)
    io.runtime_snapshot(bindings)
    preflight = json.loads((DOCS / 'COUPLED-TIME-RUNNER-V2-PREFLIGHT.json').read_text())
    receipt = read_saved(SAVED / 'r4.result.json', preflight['evidence_sha256'], bindings)
    if receipt['sources'] != 100 or receipt['windows'] != 600 or receipt['real_private_inputs_read'] is not False:
        raise ValueError('registered generated cohort only')
    top = {name: read_saved(SAVED / 'attempt' / name, digest, bindings) for name, digest in receipt['artifact_sha256'].items()}
    context = top['construct.context.json']
    inputs_path = SAVED / 'generated/inputs.json'
    if Path(context['input_manifest_path']) != inputs_path:
        raise ValueError('unexpected generated manifest location')
    inputs = read_saved(inputs_path, context['input_manifest_sha256'], bindings)
    if inputs['scope'] != 'GENERATED_R4_ONLY' or len(inputs['sources']) != 100:
        raise ValueError('generated-only scope differs')
    completion = top['evaluate.completion.json']
    constructed = top['construct.completion.json']
    if len(completion['details']) != 100 or len(constructed['candidates']) != 100:
        raise ValueError('saved complete cohort required')
    counts, paired, cases = Counter(), {key: Counter() for key in ('ols', 'coarse', 'actual')}, []
    support_totals = Counter()
    for index, row in enumerate(inputs['sources']):
        if time.monotonic() - started > limits['max_wall_seconds']:
            raise ValueError('audit wall cap exceeded')
        detail = completion['details'][index]
        if detail['track'] != row['track'] or context['source_order'][index] != row['track']:
            raise ValueError('saved source order differs')
        scored = read_saved(SAVED / 'attempt/evaluate' / (str(index) + '.json'), detail['sha256'], bindings)['scores']
        candidate = read_saved(SAVED / 'attempt/candidates' / (str(index) + '.json'), constructed['candidates'][index]['sha256'], bindings)
        events = read_saved(row['events_path'], row['events_sha256'], bindings)['events']
        if len(events['beat_frames']) != len(candidate['events']['beat_frames']) or topology(events) != topology(candidate['events']):
            raise ValueError('saved topology differs')
        counts.update(sources=1, topology_preserved_sources=1)
        if len(row['windows']) != 6 or len(scored['windows']) != 6:
            raise ValueError('six saved windows required')
        for role in paired:
            window_sum = Counter()
            for window in scored['windows']:
                window_sum.update(window['paired'][role])
            if dict(window_sum) != scored['paired'][role]:
                raise ValueError('saved window/source paired counters disagree')
            paired[role].update(scored['paired'][role])
        for score in scored['candidate_nominal']:
            support_totals.update(score)
        for window_index, window in enumerate(scored['windows']):
            counts['windows'] += 1
            changes = window['paired']['ols']
            if changes['meter_breaks'] or changes['downbeat_breaks']:
                baseline = SAVED / 'generated/baseline' / (row['track'] + '.json')
                saved = read_saved(baseline, completion['bindings'][str(baseline)], bindings)
                old_projection = saved['models']['beat_this']['windows'][window_index]['candidate']
                case = inspect_case(events, candidate['events'], row['windows'][window_index], old_projection, window['projected'])
                case.update(source_index=index, window_index=window_index, source_id=row['track'], paired=window['paired'])
                cases.append(case)
    if any(dict(paired[role]) != receipt['paired'][role] for role in paired) or counts['sources'] != 100 or counts['windows'] != 600:
        raise ValueError('saved full-cohort paired counters differ')
    if dict(support_totals) != receipt['totals']['candidate_nominal']:
        raise ValueError('saved support counters differ')
    keys = emitted_keys((ROOT / 'tools/apta_1_1_event_representation.py').read_text())
    actual_protocol = json.loads((DOCS / 'COUPLED-TIME-DEVELOPMENT-PROTOCOL.json').read_text())
    support = support_summary(receipt['totals']['candidate_nominal'])
    if support['incomplete_counter_emitted'] or 'reference_incomplete_windows' in keys:
        raise ValueError('registered missing-counter diagnosis changed')
    real_incomplete = actual_protocol['input_identity']['reference_incomplete_windows']
    reasons = Counter(case['candidate']['guard'] for case in cases)
    public = {'format': 'libapta-coupled-time-r5-saved-audit-1', 'status': 'DIAGNOSIS_COMPLETE_NO_REPAIR',
              'source_revision': revision, 'audit_registration_sha256': REGISTRATION_SHA256,
              'r4_result_sha256': preflight['evidence_sha256'], 'audit_tool_sha256': io.sha(__file__),
              'generated_only': True, 'real_private_inputs_read': False, 'labels_loaded': False,
              'new_candidates': 0, 'new_scoring_calls': 0, 'canonical_projection_replay_calls': len(cases) * 2,
              'counts': dict(counts), 'affected_windows': len(cases), 'affected_sources': len({c['source_index'] for c in cases}),
              'projection_guard_reasons': dict(reasons),
              'case_topology_preserved': all(case['same_source_topology'] for case in cases),
              'case_window_event_membership_preserved': all(case['same_window_event_membership'] for case in cases),
              'old_case_meter_present': all(case['old']['meter_present'] for case in cases),
              'candidate_case_meter_missing': all(not case['candidate']['meter_present'] for case in cases),
              'paired': {key: dict(value) for key, value in paired.items()}, 'reference_support': support,
              'real_registered_incomplete_count': real_incomplete,
              'real_support_counter_guard_would_fail': real_incomplete != support['missing_counter_lookup_default'],
              'real_historical_runtime_freeze_audited': False,
              'real_authorization_created': False, 'scientific_verdict_unchanged': receipt['scientific_verdict'],
              'wall_seconds': time.monotonic() - started, 'self_peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}
    io.runtime_recheck(bindings)
    io.recheck(bindings)
    io.check_source(ROOT, revision)
    public['wall_seconds'] = time.monotonic() - started
    public['self_peak_rss_kib'] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    private = {'cases': cases, 'bindings': bindings, 'public': public}
    if (public['wall_seconds'] > limits['max_wall_seconds'] or public['self_peak_rss_kib'] > limits['max_process_rss_kib']
            or io.json_bytes(private) + io.json_bytes({**public, 'private_receipt_sha256': '0' * 64}) > limits['max_output_bytes']):
        raise ValueError('audit resource cap exceeded')
    output.mkdir(parents=True)
    io.write_new(output / 'private-case-audit.json', private)
    public['private_receipt_sha256'] = io.sha(output / 'private-case-audit.json')
    io.write_new(output / 'audit.aggregate.json', public)
    return public


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run(args.output)
    print(json.dumps({'status': result['status'], 'sources': result['counts']['sources'],
                      'windows': result['counts']['windows'], 'affected_windows': result['affected_windows']}, sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({'status': 'INVALID_AUDIT', 'error': str(error)}))
        raise SystemExit(2)
