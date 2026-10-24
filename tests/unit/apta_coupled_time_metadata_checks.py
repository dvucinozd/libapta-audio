#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fixed generated-R4 controls and exact metadata parity; no scoring dispatch."""
from collections import Counter
from copy import deepcopy
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import apta_1_1_coupled_time_v2_metadata as runner
import apta_dense_runner_support as io

SAVED = Path.home() / '.local/share/libapta-audio/rust-rewrite/coupled-time-v2-r4-frozen-20261024'
R4_RESULT_SHA256 = 'a46fad2d05ce9824dc444dc56d2b443f932c4aaa00259bbb293bf63ee91b0a1b'
R4_PREFLIGHT_SHA256 = '12f9fc50a3d262f7e9796d1e24a312e3aa5e8bb78a6f0ce2d90ef608bb6f8245'


def identical(left, right):
    """Compare JSON values including number/bool types, without key-order drift."""
    def canonical(value):
        return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)
    if canonical(left) != canonical(right):
        raise ValueError('legacy JSON field/value/type parity differs')


def strip_source(detail, cfg):
    """Remove only the six registered new leaves, with no recursive exclusions."""
    value = runner.strict_json(json.dumps(detail, allow_nan=False))
    scores = value['scores']
    size = len(scores['windows'])
    if not 1 <= size <= 8:
        raise ValueError('invalid parity window count')
    groups = [scores[name] for name in runner.DIAGNOSTIC_GROUPS]
    groups += [[window[name] for window in scores['windows']] for name in ('nominal', 'actual')]
    for group in groups:
        if len(group) != size:
            raise ValueError('parity diagnostic count differs')
        for score in group:
            runner.validate_record(score, cfg, 'metadataDiagnosticCounts')
            del score['reference_incomplete_windows']
    return value


def compare_source(old, new, cfg):
    identical(old, strip_source(new, cfg))


def compare_scientific(old, new, cfg):
    """Caller must validate new artifact identities before this value comparison."""
    runner.validate_record(new, cfg, 'evaluation')
    expected_keys = set(old) | {'operational_registration_sha256', 'metadata_amendment_sha256',
                                'context_sha256', 'reference_support_sha256'}
    if set(new) != expected_keys:
        raise ValueError('scientific parity key set differs')
    stripped = deepcopy(new)
    for key in expected_keys - set(old): del stripped[key]
    for group in runner.DIAGNOSTIC_GROUPS:
        del stripped['totals'][group]['reference_incomplete_windows']
    # These identities are separately checked against actual files by
    # full_receipt/evaluation_artifacts, not accepted as arbitrary exclusions.
    for key in ('format', 'source_revision', 'candidate_freeze_sha256', 'verification_sha256', 'bindings'):
        stripped[key] = old[key]
    if [row['track'] for row in new['details']] != [row['track'] for row in old['details']]:
        raise ValueError('scientific detail order differs')
    stripped['details'] = deepcopy(old['details'])
    identical(old, stripped)


def saved_read(relative, digest, bindings):
    path = runner.safe_path(SAVED / relative)
    if not path.is_relative_to(SAVED) or path.name.startswith('labels.'):
        raise ValueError('only fixed generated artifacts, never saved labels')
    return runner.read_bound(path, digest, bindings)


def baseline():
    """Read hash-bound saved outputs and public geometry, never labels/runtime."""
    cfg = runner.registered_settings()
    bindings = {}
    preflight = runner.read_bound(ROOT / 'docs/rust/COUPLED-TIME-RUNNER-V2-PREFLIGHT.json', R4_PREFLIGHT_SHA256, bindings)
    if preflight['evidence_sha256'] != R4_RESULT_SHA256:
        raise ValueError('registered R4 result differs')
    receipt = saved_read('r4.result.json', R4_RESULT_SHA256, bindings)
    if receipt['sources'] != 100 or receipt['windows'] != 600 or receipt['real_private_inputs_read'] is not False:
        raise ValueError('fixed generated100/600 cohort required')
    top = {name: saved_read('attempt/' + name, digest, bindings) for name, digest in receipt['artifact_sha256'].items()}
    context = top['construct.context.json']
    if Path(context['input_manifest_path']) != SAVED / 'generated/inputs.json':
        raise ValueError('fixed generated input path differs')
    inputs = saved_read('generated/inputs.json', context['input_manifest_sha256'], bindings)
    scientific = top['evaluate.completion.json']
    frozen = top['construct.completion.json']
    if (inputs['scope'] != 'GENERATED_R4_ONLY' or len(inputs['sources']) != 100
            or len(scientific['details']) != 100 or len(frozen['candidates']) != 100
            or scientific['status'] != 'REJECTED' or len(scientific['gates']) != 16
            or sum(scientific['gates'].values()) != 14):
        raise ValueError('frozen generated scientific scope differs')
    details = []
    candidates = []
    for index, row in enumerate(inputs['sources']):
        detail = scientific['details'][index]
        candidate = frozen['candidates'][index]
        if (detail['track'] != row['track'] or context['source_order'][index] != row['track']
                or candidate['source_id'] != row['track'] or candidate['index'] != index):
            raise ValueError('saved source index/order differs')
        details.append(saved_read('attempt/evaluate/%d.json' % index, detail['sha256'], bindings))
        saved_read('attempt/candidates/%d.json' % index, candidate['sha256'], bindings)
        candidates.append(candidate['sha256'])
    return cfg, inputs, scientific, details, candidates, bindings


def saved_controls():
    """Exercise all600 saved diagnostics without constructing or scoring a candidate."""
    from apta_reference_support import reference_support
    from apta_1_1_coupled_time_evaluation_v2_metadata import add_support
    from apta_coupled_time_v2_preflight import generated_source
    cfg, inputs, scientific, details, candidates, bindings = baseline()
    total = Counter()
    for index, (row, old) in enumerate(zip(inputs['sources'], details)):
        events, truth, windows, ordinals = generated_source(index)
        identical(row['beat_frames'], events['beat_frames'])
        identical(row['downbeat_ordinals'], ordinals)
        identical(row['windows'], windows)
        if row['track'] != truth['track'] or len(windows) != 6:
            raise ValueError('public arithmetic differs from frozen generated source')
        counts = reference_support(truth['beat_frames'], windows)
        extended = {'track': old['track'], 'scores': add_support(old['scores'], counts)}
        # JSON serialization exercises distinct per-window aliases, like files.
        compare_source(old, runner.strict_json(json.dumps(extended)), cfg)
        for window in counts: total.update(window)
    identical(dict(total), cfg['operational']['generated_coverage'])
    io.recheck(bindings)
    return {'status': 'SAVED_GENERATED_EXTENSION_CONTROLS_PASS', 'sources': 100, 'windows': 600,
            'baseline_candidate_hashes_verified': len(candidates), 'reference_support': dict(total),
            'legacy_scientific_verdict': scientific['status'], 'legacy_gate_pass_count': 14,
            'r4_result_sha256': R4_RESULT_SHA256, 'r4_preflight_sha256': R4_PREFLIGHT_SHA256,
            'bound_saved_files': len(bindings), 'saved_labels_read': False,
            'new_candidates': 0, 'new_scoring_calls': 0, 'fresh_candidate_parity_measured': False}


def compare_attempt(attempt, cfg):
    """C3 only: actual full new output identities,100 candidate bytes and600 records."""
    _authority, inputs, old_scientific, old_details, old_candidates, old_bindings = baseline()
    bindings = dict(cfg['bindings'])
    revision = io.git(Path(cfg['source_root']), 'rev-parse', 'HEAD')
    _constructed, construct_context = runner.header(attempt, 'construct', cfg, revision, bindings, with_context=True)
    rows = runner.load_input_manifest(construct_context['input_manifest_path'], cfg, bindings)
    # Use fresh maps for earlier boundaries, avoiding future envelope requirements.
    for role in ('construct', 'verify', 'evaluate'):
        current = dict(cfg['bindings'])
        for prior in ('construct', 'verify', 'evaluate'):
            runner.full_receipt(attempt, prior, cfg, revision, rows, current)
            if prior == role: break
    completed, context = runner.header(attempt, 'evaluate', cfg, revision, bindings, with_context=True)
    proof, scientific = runner.evaluation_artifacts(attempt, cfg, completed, context, rows, bindings)
    if len(rows) != 100:
        raise ValueError('full new100-source cohort required for parity')
    for index, (row, old_row, old, digest) in enumerate(zip(rows, inputs['sources'], old_details, old_candidates)):
        if row['track'] != old_row['track']:
            raise ValueError('new/old generated source order differs')
        for key in runner.PLAIN: identical(row[key], old_row[key])
        candidate_path = attempt / 'candidates' / (str(index) + '.json')
        if io.sha(candidate_path) != digest or completed['candidates'][index]['sha256'] != digest:
            raise ValueError('actual candidate byte parity differs')
        new = runner.read_bound(attempt / 'evaluate' / (str(index) + '.json'), scientific['details'][index]['sha256'], bindings)
        compare_source(old, new, cfg)
    compare_scientific(old_scientific, scientific, cfg)
    identical(proof['counts'], cfg['operational']['generated_coverage'])
    io.recheck(old_bindings)
    io.recheck(bindings)
    return {'status': 'EXACT_FULL_GENERATED_METADATA_PARITY_PASS', 'candidate_byte_parity_sources': 100,
            'legacy_diagnostic_parity_windows': 600, 'reference_support': proof['counts'],
            'reference_support_sha256': completed['reference_support_sha256'],
            'scientific_verdict': scientific['status'], 'scientific_gate_pass_count': sum(scientific['gates'].values()),
            'r4_result_sha256': R4_RESULT_SHA256, 'r4_preflight_sha256': R4_PREFLIGHT_SHA256}
