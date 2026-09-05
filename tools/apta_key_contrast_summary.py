#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarize the frozen synthetic observer; no tuning or corpus scoring."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct

from apta_key_mode_diagnostic_summary import load_report

PROFILES = ((.748,.060,.488,.082,.674,.460,.096,.715,.104,.366,.057,.400),
            (.712,.084,.455,.270,.360,.320,.082,.600,.059,.291,.092,.260))
PROFILES = tuple(tuple(struct.unpack('f', struct.pack('f', v))[0] for v in row) for row in PROFILES)
BASELINE_HASHES = {False: "fc82ddbf13c04a1d406e40c2e44f48573c820dd4d99454f7123635695b1d586c",
                  True: "eedb1b97e4d975b37b925b649c80596871c7462412d3550de8d2139e862f06f1"}


def require(value, message):
    if not value:
        raise ValueError(message)


def vector(values, size):
    require(isinstance(values, list) and len(values) == size and
            all(type(v) in (float, int) and math.isfinite(v) and v >= 0 for v in values), "invalid trace vector")
    require(sum(values) > 0, "empty trace vector")
    return values


def contrast(values):
    total = sum(values)
    require(total > 0, "empty contrast")
    return dict(min_over_mean=len(values) * min(values) / total,
                normalized_entropy=-sum((v / total) * math.log(v / total) for v in values if v > 0) / math.log(len(values)))


def scores(values):
    norm = math.sqrt(sum(v * v for v in values))
    require(norm > 0, "empty score vector")
    return [sum(values[p] * PROFILES[mode][(p + 12 - tonic) % 12] for p in range(12)) /
            (norm * math.sqrt(sum(v * v for v in PROFILES[mode])))
            for mode in range(2) for tonic in range(12)]


def inspect_trace(row, variants):
    trace = row['contrast']
    require(trace['variants'] == variants and trace['observations'] == variants * 36 and
            trace['native_cumulative_bit_identical'] is True, "incomplete observer")
    raw, compressed = trace['raw_energy_by_variant'], trace['compressed_by_variant']
    require(len(raw) == len(compressed) == variants, "variant coverage")
    for r, c in zip(raw, compressed):
        vector(r, 36)
        vector(c, 36)
        require(all(math.isclose(math.log1p(a), b, rel_tol=3e-6, abs_tol=3e-7)
                    for a, b in zip(r, c)), "compression identity mismatch")
    for name, bins in (('raw_folded', raw), ('compressed_window', compressed)):
        folded = vector(trace[name], 12)
        expected = [sum(bins[v][p + octave * 12] / variants for v in range(variants) for octave in range(3))
                    for p in range(12)]
        require(all(math.isclose(a, b, rel_tol=3e-6, abs_tol=3e-7) for a, b in zip(expected, folded)), "fold identity mismatch")
    return raw, compressed


def load_checked(path, baseline_path, band):
    base_bytes = baseline_path.read_bytes()
    require(hashlib.sha256(base_bytes).hexdigest() == BASELINE_HASHES[band], "baseline hash mismatch")
    base_rows, _ = load_report(baseline_path, band)
    rows, digest = load_report(path, band, 'apta-key-contrast-diagnostic-1')
    require([{k: v for k, v in r.items() if k != 'contrast'} for r in rows] == base_rows,
            "original diagnostic changed")
    pcm = [r for r in rows if r['kind'] == 'pcm_cumulative']
    require(len(pcm) == 288 and all(('contrast' in r) == (r['kind'] == 'pcm_cumulative') for r in rows), "contrast row coverage")
    for row in pcm:
        inspect_trace(row, 3 if band else 1)
    metadata = json.loads(path.read_bytes())
    for field in ('observer_scratch_bytes', 'session_bytes'):
        require(type(metadata[field]) is int and metadata[field] > 0, 'invalid size metadata')
    return rows, digest, {field: metadata[field] for field in ('observer_scratch_bytes', 'session_bytes')}


def summarize_rows(rows):
    groups = defaultdict(list)
    for row in rows:
        if row['kind'] == 'pcm_cumulative':
            groups[(row['condition'], row['stimulus_mode'], row['window'])].append(row)
    groups_out = []
    for (condition, mode, window), group in sorted(groups.items()):
        require(len(group) == 12, 'incomplete transposition group')
        result = dict(condition=condition, stimulus_mode=mode, window=window, count=12, stages={})
        for name in ('raw_folded', 'compressed_window', 'native_cumulative'):
            measurements = []
            for row in group:
                v = row['chroma'] if name == 'native_cumulative' else row['contrast'][name]
                s = scores(v)
                # This is a descriptive argmax, not a replacement native verdict.
                selected = max(range(24), key=lambda i: s[i])
                measurements.append(dict(contrast(v), major_margin=max(s[:12]) - max(s[12:]),
                    stimulus_major_margin=s[row['stimulus_tonic']] - s[12 + row['stimulus_tonic']],
                    selected_major=int(selected < 12),
                    matches_progression_key=int(selected == mode * 12 + row['stimulus_tonic'])))
            result['stages'][name] = {
                **{'mean_' + field: sum(m[field] for m in measurements) / 12 for field in
                   ('min_over_mean', 'normalized_entropy', 'major_margin', 'stimulus_major_margin')},
                **{field: sum(m[field] for m in measurements) for field in ('selected_major', 'matches_progression_key')}}
        result['octave_resolved'] = {}
        for field in ('raw_energy_by_variant', 'compressed_by_variant'):
            measurements = [contrast([sum(v[b] for v in r['contrast'][field]) / len(r['contrast'][field])
                                      for b in range(36)]) for r in group]
            result['octave_resolved'][field] = {name: sum(m[name] for m in measurements) / 12
                                               for name in ('min_over_mean', 'normalized_entropy')}
        groups_out.append(result)
    return groups_out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('default', 'band', 'baseline-default', 'baseline-band', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'output exists')
    result = dict(format='apta-key-contrast-summary-1', evidence_level='synthetic-diagnostic',
                  acceptance_claim=False, candidate_promoted=False, corpus_access=False,
                  interpretation='Raw folded scoring is counterfactual. IV/V windows are not global-key correctness tests.',
                  builds={})
    for name, band in (('default', False), ('band', True)):
        rows, digest, metadata = load_checked(getattr(args, name), getattr(args, 'baseline_' + name), band)
        result['builds'][name] = dict(report_sha256=digest, baseline_report_sha256=BASELINE_HASHES[band],
            original_720_rows_unchanged=True, observed_windows=288, **metadata, groups=summarize_rows(rows))
    with args.output.open('x', encoding='utf-8', newline='\n') as output:
        output.write(json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + '\n')


if __name__ == '__main__':
    main()
