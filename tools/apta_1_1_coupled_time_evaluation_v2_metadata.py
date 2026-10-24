#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Versioned evaluation orchestration; canonical scoring and gates are unchanged."""
from collections import Counter
from copy import deepcopy
import json
from pathlib import Path

import apta_dense_runner_support as io

METRICS = ('beat_f1', 'downbeat_f1', 'beat_cmlt', 'downbeat_cmlt')


def _read(path, digest, bindings):
    from apta_1_1_coupled_time_v2_metadata import read_bound
    return read_bound(path, digest, bindings)


def write_detail(attempt, path, value, cfg):
    from apta_1_1_coupled_time_v2_metadata import safe_path
    if io.tree_bytes((attempt,)) + io.json_bytes(value) > cfg['protocol']['limits']['max_attempt_output_bytes']:
        raise ValueError('evaluation output cap exceeded')
    io.write_new(safe_path(path), value)


def add_support(scored, support):
    """Copy and extend only the six registered leaves, checking old context bits."""
    from apta_1_1_coupled_time_v2_metadata import DIAGNOSTIC_GROUPS, partition
    if type(support) is not list or not support or len(scored['windows']) != len(support):
        raise ValueError('support/scored window count differs')
    groups = [scored[name] for name in DIAGNOSTIC_GROUPS]
    groups += [[window[name] for window in scored['windows']] for name in ('nominal', 'actual')]
    if any(len(group) != len(support) for group in groups):
        raise ValueError('diagnostic window count differs')
    for counts in support:
        partition(counts)
        if set(counts) != {'windows', 'reference_complete_windows', 'reference_incomplete_windows'} or counts['windows'] != 1:
            raise ValueError('invalid per-window support')
    for group in groups:
        for score, counts in zip(group, support):
            if (type(score) is not dict or 'reference_incomplete_windows' in score
                    or type(score.get('windows')) is not int or score['windows'] != 1
                    or type(score.get('reference_complete_windows')) is not int
                    or score['reference_complete_windows'] != counts['reference_complete_windows']):
                raise ValueError('legacy reference context differs')
    result = deepcopy(scored)
    groups = [result[name] for name in DIAGNOSTIC_GROUPS]
    groups += [[window[name] for window in result['windows']] for name in ('nominal', 'actual')]
    for group in groups:
        for score, counts in zip(group, support):
            score['reference_incomplete_windows'] = counts['reference_incomplete_windows']
    return result


def evaluation_boundary(attempt, cfg, complete, sources, revision, bindings):
    """Recheck the original cohort and supervised upstream before any label read."""
    import apta_1_1_coupled_time_v2_metadata as runner
    attempt = runner.destination(attempt, cfg)
    # Validate earlier receipts from their own boundary state. The caller already
    # has a later evaluation context and verification artifacts in its map;
    # requiring those in the construction receipt would reject every valid run.
    upstream_bindings = dict(cfg['bindings'])
    snapshot = {}
    if io.source_snapshot(Path(cfg['source_root']), snapshot) != revision:
        raise ValueError('evaluation source revision differs')
    runner.merge_expected(upstream_bindings, snapshot)
    frozen, original_context = runner.header(attempt, 'construct', cfg, revision, upstream_bindings, with_context=True)
    original_sources = runner.load_input_manifest(original_context['input_manifest_path'], cfg, upstream_bindings)
    if sources != original_sources:
        raise ValueError('evaluation cohort differs from bound original manifest')
    runner.audit_cohort(original_sources, cfg, upstream_bindings)
    frozen = runner.full_receipt(attempt, 'construct', cfg, revision, original_sources, upstream_bindings)
    verified = runner.full_receipt(attempt, 'verify', cfg, revision, original_sources, upstream_bindings)
    if complete != frozen or frozen['candidates'] != verified['candidates']:
        raise ValueError('evaluation requires complete matching construction and verification')
    runner.merge_expected(bindings, upstream_bindings)
    context_path = attempt / 'evaluate.context.json'
    context = runner.read_bound(context_path, io.sha(context_path), bindings)
    runner.validate_record(context, cfg, 'context')
    runner.check_identity(context, cfg, revision)
    runner.context_semantics(context, attempt, original_sources, cfg, 'evaluate')
    if (context['input_manifest_path'] != original_context['input_manifest_path']
            or [row['candidate_sha256'] for row in context['rows']] != [row['sha256'] for row in frozen['candidates']]
            or [row['job_sha256'] for row in context['rows']] != [row['job_sha256'] for row in original_context['rows']]):
        raise ValueError('evaluation context candidate/job/manifest differs')
    io.recheck(bindings)
    io.check_source(Path(cfg['source_root']), revision)
    return context, original_sources


def evaluate(attempt, inventory_path, cfg, complete, sources, revision, bindings):
    """Only this path imports scoring modules or parses reference labels."""
    import apta_1_1_coupled_time_v2_metadata as runner
    context, sources = evaluation_boundary(attempt, cfg, complete, sources, revision, bindings)
    from apta_reference_support import reference_support
    p = cfg["protocol"]
    proof = {"format": cfg['operational']['records']['support'],
             "registration_sha256": cfg['registration_sha256'],
             "operational_registration_sha256": cfg['operational_registration_sha256'],
             "metadata_amendment_sha256": cfg['metadata_amendment_sha256'],
             "source_revision": revision, "context_sha256": io.sha(attempt / 'evaluate.context.json'),
             "input_manifest_sha256": context['input_manifest_sha256'],
             "candidate_freeze_sha256": context['upstream']['construction_completion_sha256'],
             "verification_sha256": context['upstream']['verification_completion_sha256'],
             "reference_labels_loaded": True, "source_count": len(sources),
             "window_count": context['window_count'], "counts": dict.fromkeys(runner.SUPPORT_KEYS, 0), "sources": []}
    inventory = _read(inventory_path, p["input_identity"]["ct03_private_inventory_sha256"], bindings)
    if [row["track"] for row in inventory["sources"]] != [row["track"] for row in sources]:
        raise ValueError("evaluation inventory cohort differs")
    # Bind the full original inventory, including model/audio/old-score identities.
    for name, digest in inventory["bindings"].items():
        io.bind(name, digest, bindings)
    original_freeze_path = next((Path(name) for name, digest in inventory["bindings"].items()
                                 if digest == p["input_identity"]["original_freeze_sha256"]), None)
    if original_freeze_path is None:
        raise ValueError("original model/runtime freeze is not bound")
    original_freeze = _read(original_freeze_path, p["input_identity"]["original_freeze_sha256"], bindings)
    manifest = _read(inventory["labels_manifest_path"], p["input_identity"]["prepared_manifest_sha256"], bindings)
    if (manifest.get("holdout_performances_opened") is not False
            or [row["track"] for row in manifest["tracks"]] != [row["track"] for row in sources]):
        raise ValueError("reference manifest scope/order differs")
    import apta_1_1_dense_decoder as scorer
    import apta_1_1_neural_comparison as comparison
    import apta_1_1_event_representation
    import apta_1_1_learned_piecewise
    # Warm real metric dependencies before runtime freeze; no constructor import.
    comparison.event_metrics({"beat_frames": [], "downbeat_frames": []}, {"beat_frames": [], "downbeat_frames": []})
    io.runtime_snapshot(bindings, original_freeze["bindings"])
    old_report_path = next((Path(name) for name, digest in inventory["bindings"].items()
                            if digest == p["input_identity"]["legacy_scoring_report_sha256"]), None)
    if old_report_path is None:
        raise ValueError("baseline scoring report not bound")
    old_report = _read(old_report_path, p["input_identity"]["legacy_scoring_report_sha256"], bindings)
    target = attempt / "evaluate"
    target.mkdir()
    names = ("old_coarse", "old_ols", "ols_nominal", "ols_actual", "candidate_projected", "candidate_nominal", "candidate_actual")
    totals = {name: Counter() for name in names}
    paired = {name: Counter() for name in ("ols", "coarse", "actual")}
    old_macros, new_macros, genres, details, regressions = Counter(), Counter(), {}, [], []
    for index, (row, truth, full) in enumerate(zip(sources, manifest["tracks"], inventory["sources"])):
        if any(row[key] != full[key] for key in row):
            raise ValueError("evaluation inputs differ from label-free freeze")
        if truth["requested_frames"] != row["total_frames"]:
            raise ValueError("reference source length differs")
        saved = _read(full["baseline_score_path"], full["baseline_score_sha256"], bindings)
        coarse = _read(full["coarse_path"], full["coarse_sha256"], bindings)
        if coarse["requested_frames"] != row["total_frames"] or coarse["sample_rate"] != 48000:
            raise ValueError("coarse source geometry differs")
        events = _read(row["events_path"], row["events_sha256"], bindings)["events"]
        candidate = _read(attempt / "candidates" / (str(index) + ".json"), complete["candidates"][index]["sha256"], bindings)
        scored = scorer.score_source(candidate, events, row["windows"], truth, saved, coarse)
        support = reference_support(truth['beat_frames'], row['windows'])
        scored = add_support(scored, support)
        source_counts = {key: sum(counts[key] for counts in support) for key in runner.SUPPORT_KEYS}
        proof['sources'].append({'index': index, 'counts': source_counts,
                                 'windows': [{'index': number, 'counts': counts} for number, counts in enumerate(support)]})
        for key in runner.SUPPORT_KEYS:
            proof['counts'][key] += source_counts[key]
        for name in names:
            for score in scored[name]:
                totals[name].update(score)
        for name in paired:
            paired[name].update(scored["paired"][name])
        old_macros.update(scored["old_event_metrics"])
        new_macros.update(scored["candidate_event_metrics"])
        genre = genres.setdefault(truth["genre"], {"sources": 0, "legacy": Counter(), "candidate": Counter()})
        genre["sources"] += 1
        genre["legacy"].update(scored["old_event_metrics"])
        genre["candidate"].update(scored["candidate_event_metrics"])
        regressions.extend({"track": row["track"], "window": i} for i in scored["original_regressions"])
        path = target / (str(index) + ".json")
        write_detail(attempt, path, {"track": row["track"], "scores": scored}, cfg)
        io.bind(path, io.sha(path), bindings)
        details.append({"track": row["track"], "sha256": io.sha(path)})
    observed = {"ols_nominal_joint_windows": totals["ols_nominal"]["joint_windows"],
                "ols_nominal_complete_sequence_windows": totals["ols_nominal"]["complete_sequence_windows"],
                "ols_actual_joint_windows": totals["ols_actual"]["joint_windows"],
                "ols_actual_complete_sequence_windows": totals["ols_actual"]["complete_sequence_windows"],
                "coarse_joint_windows": totals["old_coarse"]["confirmed_joint_windows"],
                "original_coarse_to_ols_joint_breaks": len(regressions)}
    count = len(sources)
    old_means = {key: old_macros[key] / count for key in METRICS}
    new_means = {key: new_macros[key] / count for key in METRICS}
    expected_context = p["input_identity"]
    if (observed != p["baseline"] or old_means != p["baseline_event_means"]
            or old_means != old_report["event_means"]["beat_this"]
            or dict(totals["old_coarse"]) != old_report["totals"]["coarse"]
            or dict(totals["old_ols"]) != old_report["totals"]["beat_this"]
            or totals["candidate_nominal"]["windows"] != expected_context["windows"]
            or totals["candidate_nominal"]["reference_complete_windows"] != expected_context["reference_complete_windows"]
            or totals["candidate_nominal"]["reference_incomplete_windows"] != expected_context["reference_incomplete_windows"]):
        raise ValueError("baseline metrics or full reference denominator differs")
    for genre, data in genres.items():
        old = old_report["by_genre"][genre]["beat_this"]
        if data["sources"] != old["tracks"] or {k: data["legacy"][k] / data["sources"] for k in METRICS} != old["event_means"]:
            raise ValueError("baseline genre macros differ")
    for name in runner.DIAGNOSTIC_GROUPS:
        if any(totals[name][key] != proof['counts'][key] for key in runner.SUPPORT_KEYS):
            raise ValueError('diagnostic/support cohort totals differ')
    for key in runner.SUPPORT_KEYS:
        if proof['counts'][key] != expected_context[key]:
            raise ValueError('registered support denominator differs')
    runner.validate_record(proof, cfg, 'support')
    runner.write_record(attempt, attempt / 'evaluate.reference-support.json', proof, cfg, 'support')
    io.bind(attempt / 'evaluate.reference-support.json', io.sha(attempt / 'evaluate.reference-support.json'), bindings)
    verdicts = scorer.gates(p, totals, paired, old_means, new_means)
    io.runtime_recheck(bindings, original_freeze["bindings"])
    io.recheck(bindings)
    io.check_source(Path(cfg['source_root']), revision)
    result = {"format": cfg["operational"]["records"]["evaluation"], "status": "EXPOSED_DIAGNOSTIC_PASS_ONLY" if all(verdicts.values()) else "REJECTED",
              "source_revision": revision, "registration_sha256": cfg["registration_sha256"],
              "operational_registration_sha256": cfg['operational_registration_sha256'],
              "metadata_amendment_sha256": cfg['metadata_amendment_sha256'],
              "context_sha256": proof['context_sha256'],
              "reference_support_sha256": io.sha(attempt / 'evaluate.reference-support.json'),
              "candidate_freeze_sha256": io.sha(attempt / "construct.completion.json"),
              "verification_sha256": io.sha(attempt / "verify.completion.json"),
              "gates": verdicts, "totals": totals, "paired": paired, "old_event_means": old_means,
              "candidate_event_means": new_means, "by_genre": genres, "original_regressions": regressions,
              "details": details, "bindings": bindings, "sources": count, "reference_labels_loaded": True,
              "confidence_calibrated": False, "independent_transfer": False, "native_graph_constructed": False,
              "physical_p4": False, "formal_acceptance": False, "production_promotion": False}
    # Canonical accumulators are Counters. Materialize their JSON object form
    # before exact-type schema validation, preserving every numeric value.
    result = runner.strict_json(json.dumps(result, allow_nan=False))
    runner.write_record(attempt, attempt / 'evaluate.support.completion.json', result, cfg, 'evaluation')
    return result
