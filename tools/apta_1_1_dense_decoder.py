#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Frozen construct/evaluate phases. Real cohort CLI stays gated by DD-04."""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import sys
import time

import apta_dense_decoder as model
import apta_neural_dense as codec
import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "docs/rust/DENSE-DECODER-PROTOCOL.json"
PREFLIGHT = ROOT / "docs/rust/DENSE-DECODER-PREFLIGHT.json"
METRICS = ("beat_f1", "downbeat_f1", "beat_cmlt", "downbeat_cmlt")
Q = 1 << 32


def protocol():
    if io.sha(PROTOCOL) != model.PROTOCOL_SHA256:
        raise ValueError("registered decoder protocol changed")
    value = json.loads(PROTOCOL.read_text())
    for key, constant in (("minimum_interval_rows", model.MINIMUM_INTERVAL),
                          ("maximum_interval_rows", model.MAXIMUM_INTERVAL),
                          ("maximum_interval_change_rows", model.MAXIMUM_INTERVAL_CHANGE),
                          ("logit_clip_abs", model.LOGIT_CLIP), ("logit_scale", model.LOGIT_SCALE),
                          ("transition_penalty_per_squared_row", model.TRANSITION_PENALTY),
                          ("minimum_winning_path_events", model.MINIMUM_EVENTS)):
        if value["decoder"][key] != constant:
            raise ValueError("decoder constant differs from registration")
    for name, key in (("DENSE-DECODER-PLAN-20261023.json", "json_sha256"),
                      ("DENSE-DECODER-IMPLEMENTATION-PLAN-20261023.md", "markdown_sha256"),
                      ("DENSE-DECODER-DD00-20261023.json", "dd00_summary_sha256")):
        io.bind(ROOT / "docs/rust" / name, value["plan"][key], {})
    return value


def require_preflight():
    if not PREFLIGHT.is_file():
        raise ValueError("DD-04 preflight is not complete; real cohort CLI is disabled")
    value = json.loads(PREFLIGHT.read_text())
    flags = ("generated_preflight_passed", "independent_verifier_passed",
             "runner_isolation_passed", "resource_enforcement_passed")
    if value.get("protocol_sha256") != model.PROTOCOL_SHA256 or any(value.get(k) is not True for k in flags):
        raise ValueError("DD-04 preflight gates incomplete")
    for name in ("tools/apta_dense_decoder.py", "tools/apta_1_1_dense_decoder.py",
                 "tools/apta_dense_runner_support.py", "tools/apta_dense_decoder_verify.py"):
        io.bind(ROOT / name, value.get("component_sha256", {}).get(name), {})
    return io.sha(PREFLIGHT)


def ids_in_order(rows, count):
    ids = [row["track"] for row in rows]
    if (len(ids) != count or len(set(ids)) != count
            or any(not isinstance(t, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", t) for t in ids)):
        raise ValueError("cohort count/unique safe identity differs")
    return ids


def source_geometry(record):
    g, aggregation = record["geometry"], record["aggregation"]
    windows = [{k: w[k] for k in ("first_frame", "end_frame")} for w in g["windows"]]
    total = windows[-1]["end_frame"]
    if codec.mapping(total, g["mono_samples"], g["rows"], windows) != g:
        raise ValueError("capture window mapping differs")
    proof = codec.coverage(g["rows"], aggregation["starts"], aggregation["chunk_lengths"])
    if any(aggregation[key] != v for key, v in proof.items()):
        raise ValueError("capture coverage differs")
    return total, windows


def bind_inputs(dense, legacy, config, bindings):
    inputs = config["inputs"]
    report = io.read_bound(dense / "report.private.json", inputs["dense_report_sha256"], bindings)
    worker = io.read_bound(legacy / "beat_this/report.json", inputs["legacy_worker_report_sha256"], bindings)
    if (report["source_revision"] != inputs["dense_source_revision"]
            or report["protocol_sha256"] != inputs["dense_protocol_sha256"]
            or report["complete"] is not True or report["read_labels"] is not False
            or report["musical_scoring"] is not False or report["new_decoder"] is not False):
        raise ValueError("wrong completed capture")
    for key in ("all_100_original_saved_and_decoded_events_identical", "all_f32_payloads_bit_exact",
                "all_rows_aggregation_assigned", "all_source_window_boundary_rows_retained"):
        if report[key] is not True:
            raise ValueError("capture prerequisite failed")
    ids = ids_in_order(report["details"], inputs["sources"])
    if ids != ids_in_order(worker["outputs"], inputs["sources"]):
        raise ValueError("legacy cohort order differs")
    sources, counts = [], Counter()
    for row, oldrow in zip(report["details"], worker["outputs"]):
        track = row["track"]
        record = io.read_bound(dense / (track + ".json"), row["record_sha256"], bindings)
        payload = dense / (track + ".f32")
        io.bind(payload, row["payload_sha256"], bindings)
        saved = io.read_bound(legacy / "beat_this" / (track + ".json"), oldrow["events_sha256"], bindings)
        if (record["track"] != track or saved["track"] != track
                or record["audio_sha256"] != saved["audio_sha256"]
                or record["events"] != saved["events"]
                or record["payload_sha256"] != row["payload_sha256"]
                or record["exact_legacy_event_parity"] is not True
                or record["bit_exact_logit_roundtrip"] is not True):
            raise ValueError("saved trace/event identity differs")
        raw = payload.read_bytes()
        beat, downbeat = codec.decode(raw)
        if (len(raw) != record["payload_bytes"] or codec.encode(beat, downbeat) != raw
                or codec.statistics(beat, downbeat) != record["statistics"]):
            raise ValueError("payload codec/statistics differ")
        total, windows = source_geometry(record)
        if len(beat) != record["geometry"]["rows"]:
            raise ValueError("payload rows differ")
        geometry = record["geometry"]
        counts.update(sources=1, windows=len(windows), rows=len(beat), payload_bytes=len(raw),
                      source_owned_rows=sum(w["end_row"]-w["first_row"] for w in geometry["windows"]),
                      exclusive_endpoint_rows=len(geometry["exclusive_endpoint_rows"]),
                      beyond_source_rows=len(geometry["beyond_source_rows"]),
                      legacy_beat_events=len(saved["events"]["beat_frames"]),
                      legacy_downbeat_events=len(saved["events"]["downbeat_frames"]),
                      exclusive_endpoint_beat_events=len(saved["events"]["exclusive_endpoint_events"]["beat_seconds"]),
                      exclusive_endpoint_downbeat_events=len(saved["events"]["exclusive_endpoint_events"]["downbeat_seconds"]))
        sources.append({"track": track, "payload": str(payload.resolve()),
                        "payload_sha256": row["payload_sha256"], "total_frames": total, "windows": windows})
    if dict(counts) != report["counts"] or counts["windows"] != inputs["windows"]:
        raise ValueError("complete capture denominator differs")
    return sources, report


def rss_check(limit):
    import resource
    own = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    children = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    if max(own, children) > limit:
        raise ValueError("process or waited-child peak RSS exceeded")
    return {"self_peak_rss_kib": own, "waited_children_peak_rss_kib": children}


def decode_worker(job_path):
    import resource
    config = protocol()
    limit = config["limits"]["max_host_rss_kib_per_process"]
    # The pure worker has no NumPy/scipy virtual-address reservation; here a hard
    # AS ceiling is applicable in addition to measured RSS. Do not apply it to NumPy.
    resource.setrlimit(resource.RLIMIT_AS, (limit * 1024, limit * 1024))
    started = time.monotonic()
    digest = io.sha(job_path)
    job = json.loads(job_path.read_text())
    if set(job) != {"payload", "payload_sha256", "total_frames", "windows", "result"}:
        raise ValueError("worker job must contain plain permitted input only")
    io.bind(Path(job["payload"]), job["payload_sha256"], {})
    beat, downbeat = codec.decode(Path(job["payload"]).read_bytes())
    candidate = model.construct(beat, downbeat, job["total_frames"], job["windows"])
    io.bind(job_path, digest, {})
    io.bind(Path(job["payload"]), job["payload_sha256"], {})
    stats = rss_check(limit)
    io.write_new(Path(job["result"]), {"candidate": candidate, "resources": dict(
        stats, wall_seconds=time.monotonic()-started, hard_address_space_limit_bytes=limit*1024)})


def construct(dense, legacy, output):
    started = time.monotonic()
    config, bindings = protocol(), {}
    rss_check(config["limits"]["max_host_rss_kib_per_process"])
    io.external(output, ROOT)
    io.disjoint_output(output, (dense, legacy))
    revision = io.source_snapshot(ROOT, bindings)
    sources, capture = bind_inputs(dense, legacy, config, bindings)
    io.runtime_snapshot(bindings, capture["bindings"])
    output.mkdir(parents=True)
    (output / "candidates").mkdir()
    (output / "jobs").mkdir()
    io.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": model.PROTOCOL_SHA256, "bindings": bindings,
        "dense": str(dense.resolve()), "legacy": str(legacy.resolve()),
        "reference_labels_loaded": False})
    freeze_hash = io.sha(output / "freeze.private.json")
    candidates, resources = [], []
    limits = config["limits"]
    for i, source in enumerate(sources):
        track = source["track"]
        job = {k: source[k] for k in ("payload", "payload_sha256", "total_frames", "windows")}
        job["result"] = str((output / "jobs" / (track + ".result.json")).resolve())
        job_path = output / "jobs" / (track + ".job.json")
        io.write_new(job_path, job)
        job_hash = io.sha(job_path)
        supervision = io.supervise(
            [sys.executable, str(Path(__file__).resolve()), "_decode-worker", "--job", str(job_path.resolve())],
            limits["max_construct_source_seconds"], limits["max_host_rss_kib_per_process"],
            limits["max_attempt_output_bytes"], (output,), output / "jobs" / track, group=False)
        io.bind(job_path, job_hash, {})
        worker = json.loads(Path(job["result"]).read_text())
        if max(worker["resources"]["self_peak_rss_kib"], worker["resources"]["waited_children_peak_rss_kib"]) > limits["max_host_rss_kib_per_process"]:
            raise ValueError("worker own peak RSS failed")
        candidate_path = output / "candidates" / (track + ".json")
        io.write_new(candidate_path, worker["candidate"])
        candidates.append({"track": track, "sha256": io.sha(candidate_path),
                           "job_sha256": job_hash, "worker_report_sha256": io.sha(Path(job["result"]))})
        resources.append({"track": track, "worker": worker["resources"], "supervisor": supervision})
        if time.monotonic()-started > limits["max_construct_process_seconds"]:
            raise ValueError("construction total time cap exceeded")
        print(json.dumps({"sources_complete": i+1}), flush=True)
    io.runtime_recheck(bindings, capture["bindings"])
    io.recheck(bindings)
    io.bind(output / "freeze.private.json", freeze_hash, {})
    io.check_source(ROOT, revision)
    for row in candidates:
        io.bind(output / "candidates" / (row["track"] + ".json"), row["sha256"], {})
        io.bind(output / "jobs" / (row["track"] + ".job.json"), row["job_sha256"], {})
        io.bind(output / "jobs" / (row["track"] + ".result.json"), row["worker_report_sha256"], {})
    stats = rss_check(limits["max_host_rss_kib_per_process"])
    value = {"format": "libapta-dense-candidates-frozen-1", "source_revision": revision,
             "protocol_sha256": model.PROTOCOL_SHA256, "source_freeze_sha256": freeze_hash,
             "candidate_module_sha256": io.sha(Path(model.__file__)),
             "runner_module_sha256": io.sha(Path(__file__)),
             "all_candidates_complete": True, "reference_labels_loaded": False,
             "candidates": candidates, "resources": dict(stats, sources=resources,
                 wall_seconds=time.monotonic()-started), "integrity_passed": True}
    # Reserve the final completion record before writing a complete-cohort claim.
    if io.tree_bytes((output,)) + io.json_bytes(value) > limits["max_attempt_output_bytes"]:
        raise ValueError("completion record would exceed output limit")
    io.write_new(output / "candidates-frozen.private.json", value)
    return value


def validate_frozen(candidates, legacy, config, bindings):
    complete_path = candidates / "candidates-frozen.private.json"
    complete = json.loads(complete_path.read_text())
    io.bind(complete_path, io.sha(complete_path), bindings)
    if (complete.get("format") != "libapta-dense-candidates-frozen-1"
            or complete.get("protocol_sha256") != model.PROTOCOL_SHA256
            or complete.get("all_candidates_complete") is not True
            or complete.get("reference_labels_loaded") is not False
            or complete.get("integrity_passed") is not True):
        raise ValueError("candidate completion is not valid")
    io.check_source(ROOT, complete["source_revision"])
    if (complete["candidate_module_sha256"] != io.sha(Path(model.__file__))
            or complete["runner_module_sha256"] != io.sha(Path(__file__))):
        raise ValueError("candidate source bytes changed")
    frozen = io.read_bound(candidates / "freeze.private.json", complete["source_freeze_sha256"], bindings)
    if (frozen["source_revision"] != complete["source_revision"]
            or frozen["protocol_sha256"] != model.PROTOCOL_SHA256
            or frozen["reference_labels_loaded"] is not False
            or Path(frozen["legacy"]).resolve() != legacy.resolve()):
        raise ValueError("construction freeze differs")
    io.recheck(frozen["bindings"])
    bindings.update(frozen["bindings"])
    rows = complete["candidates"]
    ids = ids_in_order(rows, config["inputs"]["sources"])
    supervision_path = Path(str(candidates) + "-supervision") / "report.json"
    supervised = json.loads(supervision_path.read_text())
    io.bind(supervision_path, io.sha(supervision_path), bindings)
    if (supervised.get("status") != "PASS"
            or supervised.get("completion_sha256") != io.sha(complete_path)
            or supervised.get("protocol_sha256") != model.PROTOCOL_SHA256):
        raise ValueError("construction parent watchdog did not pass")
    for row in rows:
        track = row["track"]
        io.bind(candidates / "candidates" / (track + ".json"), row["sha256"], bindings)
        io.bind(candidates / "jobs" / (track + ".job.json"), row["job_sha256"], bindings)
        io.bind(candidates / "jobs" / (track + ".result.json"), row["worker_report_sha256"], bindings)
    return complete, frozen, ids


def actual_points(candidate):
    events = candidate["events"]["beat_frames"]
    return [[(p * Q, (events[i+1]-p)*Q if i+1 < len(events) else None)
             for i, p in enumerate(events) if w["first_frame"] <= p < w["end_frame"]]
            for w in candidate["windows"]]


def score_source(candidate, oldevents, geometry, truth, saved, coarse, event_metrics=None):
    # Canonical analysis imports exist ONLY on the evaluation path.
    import apta_1_1_event_representation as representation
    import apta_1_1_learned_piecewise as previous
    import apta_1_1_neural_comparison as comparison
    ref = representation.ref
    event_metrics = comparison.event_metrics if event_metrics is None else event_metrics
    controls = [ref.project(oldevents["beat_frames"], oldevents["downbeat_frames"],
                            w["first_frame"], w["end_frame"]) for w in geometry]
    projections = [ref.project(candidate["events"]["beat_frames"], candidate["events"]["downbeat_frames"],
                               w["first_frame"], w["end_frame"]) for w in geometry]
    op = previous.control_points(controls, oldevents["beat_frames"])
    old_actual = previous.score_point_windows(op, truth, geometry, True)
    old_nominal = previous.score_point_windows(op, truth, geometry, False)
    raw = actual_points(candidate)
    out = {"old_coarse": [], "old_ols": [], "ols_nominal": old_nominal, "ols_actual": old_actual,
           "candidate_projected": [], "candidate_nominal": [], "candidate_actual": [],
           "paired": {"ols": Counter(), "coarse": Counter(), "actual": Counter()}, "windows": [],
           "original_regressions": []}
    old_metrics = event_metrics(truth, oldevents)
    if old_metrics != saved["models"]["beat_this"]["event_metrics"]:
        raise ValueError("legacy event metrics changed")
    out["old_event_metrics"] = old_metrics
    out["candidate_event_metrics"] = event_metrics(truth, candidate["events"])
    out["event_metric_changes"] = {key:out["candidate_event_metrics"][key]-old_metrics[key] for key in METRICS}
    if len(geometry) != len(coarse["windows"]) or len(geometry) != len(saved["coarse"]):
        raise ValueError("source scoring window count differs")
    for i, (window, control, projected, oldwindow) in enumerate(zip(geometry, controls, projections, coarse["windows"])):
        if {k:oldwindow[k] for k in ("first_frame","end_frame")} != window:
            raise ValueError("coarse geometry differs")
        before = ref.annotated.score_window(oldwindow, truth, ref.RATE)
        old = ref.annotated.score_window(control, truth, ref.RATE)
        after = ref.annotated.score_window(projected, truth, ref.RATE)
        saved_control = saved["models"]["beat_this"]["windows"][i]
        if (control != saved_control["candidate"] or old != saved_control["score"]
                or before != saved["coarse"][i]):
            raise ValueError("frozen OLS/coarse tuple or score changed")
        cn = representation.score_points(representation.points_for_control(projected), truth,
                                         window["first_frame"], window["end_frame"])
        ca = representation.score_points(raw[i], truth, window["first_frame"], window["end_frame"])
        for field in ("period", "phase", "joint"):
            if (cn[field+"_windows"] != after["confirmed_"+field+"_windows"]
                    or old_nominal[i][field+"_windows"] != old["confirmed_"+field+"_windows"]):
                raise ValueError("common/frozen nominal scorer disagreement")
        coarse_common = {k+"_windows": before["confirmed_"+k+"_windows"] for k in ("period","phase","joint")}
        changes = {"ols": previous.paired(old_nominal[i], cn, old, after),
                   "coarse": previous.paired(coarse_common, cn, before, after),
                   "actual": previous.paired(old_actual[i], ca, old, after)}
        for key, value in changes.items():
            out["paired"][key].update(value)
        for key, value in (("old_coarse",before),("old_ols",old),("candidate_projected",after),
                           ("candidate_nominal",cn),("candidate_actual",ca)):
            out[key].append(value)
        if before["confirmed_joint_windows"] and not old["confirmed_joint_windows"]:
            out["original_regressions"].append(i)
        out["windows"].append({"projected": projected, "nominal": cn, "actual": ca, "paired": changes,
                               "source_status":candidate["status"],
                               "event_window_status":candidate["windows"][i]["status"]})
    return out


def gates(config, totals, paired, old_means, new_means):
    limits = config["development_gates"]
    for group, keys in ((totals["candidate_nominal"], ("joint_windows","complete_sequence_windows")),
                        (totals["candidate_actual"], ("complete_sequence_windows",)),
                        (paired["coarse"], ("joint_breaks",)),
                        (paired["ols"], ("period_breaks","phase_breaks","joint_breaks","meter_breaks","downbeat_breaks")),
                        (paired["actual"], ("period_breaks","phase_breaks","joint_breaks"))):
        if any(k not in group or type(group[k]) is not int or group[k] < 0 for k in keys):
            raise ValueError("missing or invalid gate counts")
    if any(k not in means or type(means[k]) not in (int,float)
           or not math.isfinite(means[k]) or not 0 <= means[k] <= 1
           for means in (old_means,new_means) for k in METRICS):
        raise ValueError("missing or invalid event macro metric")
    result = {
        "nominal_joint_improves": totals["candidate_nominal"]["joint_windows"] >= limits["minimum_projected_nominal_joint_windows"],
        "nominal_complete_improves": totals["candidate_nominal"]["complete_sequence_windows"] >= limits["minimum_projected_nominal_complete_sequence_windows"],
        "actual_complete_improves": totals["candidate_actual"]["complete_sequence_windows"] >= limits["minimum_candidate_actual_complete_sequence_windows"],
        "zero_coarse_joint_breaks": paired["coarse"]["joint_breaks"] <= limits["maximum_projected_coarse_joint_breaks"]}
    for kind, prefix in (("ols","projected"),("actual","actual")):
        for metric in ("period","phase","joint"):
            result["zero_"+kind+"_"+metric+"_breaks"] = paired[kind][metric+"_breaks"] <= limits["maximum_"+prefix+"_ols_"+metric+"_breaks"]
    for metric in ("meter","downbeat"):
        result["zero_ols_"+metric+"_breaks"] = paired["ols"][metric+"_breaks"] <= limits["maximum_projected_ols_"+metric+"_breaks"]
    epsilon = limits["event_comparison_absolute_epsilon"]
    for metric in limits["event_macro_no_decrease"]:
        result["event_"+metric+"_no_decrease"] = new_means[metric] + epsilon >= old_means[metric]
    return result


def warm_evaluation_runtime(bindings, expected):
    import apta_1_1_neural_comparison as comparison
    import apta_1_1_learned_piecewise
    import apta_1_1_event_representation
    # Generated warmup loads lazy metric dependencies BEFORE any reference labels.
    generated = {"beat_frames": list(range(0, 48000*36, 24000)),
                 "downbeat_frames": list(range(0, 48000*36, 96000))}
    comparison.event_metrics(generated, generated)
    comparison.event_metrics(generated, {"beat_frames": [], "downbeat_frames": []})
    io.runtime_snapshot(bindings, expected)


def evaluate(candidates, legacy, prepared, output):
    started = time.monotonic()
    config, bindings = protocol(), {}
    rss_check(config["limits"]["max_host_rss_kib_per_process"])
    io.external(output, ROOT)
    io.disjoint_output(output, (candidates, legacy, prepared))
    revision = io.source_snapshot(ROOT, bindings)
    complete, frozen, ids = validate_frozen(candidates, legacy, config, bindings)
    if revision != complete["source_revision"]:
        raise ValueError("construction/evaluation source revision differs")
    dense = Path(frozen["dense"])
    sources, capture = bind_inputs(dense, legacy, config, bindings)
    if ids != [s["track"] for s in sources]:
        raise ValueError("candidate/trace order differs")
    warm_evaluation_runtime(bindings, capture["bindings"])
    inputs = config["inputs"]
    # First label/scoring-report parse occurs only after full candidate validation.
    manifest = io.read_bound(prepared / "manifest.private.json", inputs["prepared_manifest_sha256"], bindings)
    old_report = io.read_bound(legacy / "scoring/report.json", inputs["legacy_scoring_report_sha256"], bindings)
    if (manifest["holdout_performances_opened"] is not False
            or ids != ids_in_order(manifest["tracks"], inputs["sources"])
            or ids != ids_in_order(old_report["details"], inputs["sources"])):
        raise ValueError("reference cohort/holdout boundary differs")
    output.mkdir(parents=True)
    io.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": model.PROTOCOL_SHA256, "bindings": bindings,
        "candidate_freeze_sha256": io.sha(candidates / "candidates-frozen.private.json"),
        "all_candidates_frozen_before_label_loading": True})
    evaluation_freeze_hash = io.sha(output / "freeze.private.json")
    names = ("old_coarse","old_ols","ols_nominal","ols_actual","candidate_projected","candidate_nominal","candidate_actual")
    totals = {name:Counter() for name in names}
    paired = {name:Counter() for name in ("ols","coarse","actual")}
    old_macros, new_macros, by_genre, details, regressions, statuses = Counter(), Counter(), {}, [], [], Counter()
    for truth, source, row, oldrow in zip(manifest["tracks"], sources, complete["candidates"], old_report["details"]):
        track = row["track"]
        saved = io.read_bound(legacy / "scoring" / (track+".json"), oldrow["sha256"], bindings)
        coarse_path = legacy / "scoring" / (track+"-coarse.json")
        coarse_expected = capture["bindings"].get(str(coarse_path.resolve()))
        if coarse_expected is None:
            raise ValueError("original coarse geometry lacks capture binding")
        coarse = io.read_bound(coarse_path, coarse_expected, bindings)
        if (truth["requested_frames"] != source["total_frames"]
                or coarse["requested_frames"] != source["total_frames"] or coarse["sample_rate"] != 48000):
            raise ValueError("reference source total differs")
        candidate = io.read_bound(candidates / "candidates" / (track+".json"), row["sha256"], bindings)
        worker = io.read_bound(candidates / "jobs" / (track+".result.json"), row["worker_report_sha256"], bindings)
        if candidate != worker["candidate"]:
            raise ValueError("candidate output differs from source worker")
        if model.construct(*codec.decode(Path(source["payload"]).read_bytes()), source["total_frames"], source["windows"]) != candidate:
            raise ValueError("candidate replay differs after label loading")
        oldevents = json.loads((legacy / "beat_this" / (track+".json")).read_text())["events"]
        scored = score_source(candidate, oldevents, source["windows"], truth, saved, coarse)
        for name in names:
            for score in scored[name]:
                totals[name].update(score)
        for name in paired:
            paired[name].update(scored["paired"][name])
        old_macros.update(scored["old_event_metrics"])
        new_macros.update(scored["candidate_event_metrics"])
        genre = by_genre.setdefault(truth["genre"], {"sources":0,"legacy":Counter(),"candidate":Counter()})
        genre["sources"] += 1
        genre["legacy"].update(scored["old_event_metrics"])
        genre["candidate"].update(scored["candidate_event_metrics"])
        regressions.extend({"track":track,"window":i} for i in scored["original_regressions"])
        statuses[candidate["status"]] += 1
        target = output / (track+".json")
        io.write_new(target, {"track":track,"scores":scored})
        details.append({"track":track,"sha256":io.sha(target)})
    baseline = config["baseline"]
    for key, actual in (("ols_nominal_joint_windows",totals["ols_nominal"]["joint_windows"]),
                         ("ols_nominal_complete_sequence_windows",totals["ols_nominal"]["complete_sequence_windows"]),
                         ("ols_actual_joint_windows",totals["ols_actual"]["joint_windows"]),
                         ("ols_actual_complete_sequence_windows",totals["ols_actual"]["complete_sequence_windows"]),
                         ("coarse_joint_windows",totals["old_coarse"]["confirmed_joint_windows"]),
                         ("original_coarse_to_ols_joint_breaks",len(regressions))):
        if baseline[key] != actual:
            raise ValueError("fixed baseline aggregate differs")
    if (dict(totals["old_coarse"]) != old_report["totals"]["coarse"]
            or dict(totals["old_ols"]) != old_report["totals"]["beat_this"]
            or totals["candidate_nominal"]["windows"] != inputs["windows"]
            or totals["candidate_nominal"]["reference_complete_windows"] != inputs["reference_complete_windows"]):
        raise ValueError("fixed score totals or denominator differs")
    count = inputs["sources"]
    old_means = {k:old_macros[k]/count for k in METRICS}
    new_means = {k:new_macros[k]/count for k in METRICS}
    if old_means != old_report["event_means"]["beat_this"]:
        raise ValueError("legacy macro event scores differ")
    for genre, data in by_genre.items():
        expected = old_report["by_genre"][genre]["beat_this"]
        if (data["sources"] != expected["tracks"] or
                {k:data["legacy"][k]/data["sources"] for k in METRICS} != expected["event_means"]):
            raise ValueError("legacy genre event metrics differ")
    verdicts = gates(config, totals, paired, old_means, new_means)
    io.runtime_recheck(bindings, capture["bindings"])
    io.recheck(bindings)
    io.bind(output / "freeze.private.json", evaluation_freeze_hash, {})
    io.check_source(ROOT, revision)
    for row in details:
        io.bind(output/(row["track"]+".json"), row["sha256"], {})
    stats = rss_check(config["limits"]["max_host_rss_kib_per_process"])
    result = {"format":"libapta-dense-decoder-development-1","source_revision":revision,
        "protocol_sha256":model.PROTOCOL_SHA256,"integrity_passed":True,
        "development_passed":all(verdicts.values()),
        "status":"DEVELOPMENT_PASS_ONLY" if all(verdicts.values()) else "REJECTED",
        "gates":verdicts,"totals":totals,"paired":paired,"old_event_means":old_means,
        "candidate_event_means":new_means,"by_genre":{g:{"sources":v["sources"],
            "legacy":{k:v["legacy"][k]/v["sources"] for k in METRICS},
            "candidate":{k:v["candidate"][k]/v["sources"] for k in METRICS}}
            for g,v in by_genre.items()},
        "original_regressions":regressions,"source_statuses":statuses,"details":details,
        "bindings":bindings,"resources":dict(stats,wall_seconds=time.monotonic()-started),
        "all_candidates_frozen_before_label_loading":True,"confidence_calibrated":False,
        "independent_transfer":False,"native_graph_constructed":False,"physical_p4":False,
        "formal_acceptance":False,"production_promotion":False}
    roots = (output,candidates,Path(str(candidates)+"-supervision"))
    if io.tree_bytes(roots) + io.json_bytes(result) > config["limits"]["max_attempt_output_bytes"]:
        raise ValueError("final evaluation report exceeds attempt output cap")
    io.write_new(output/"report.private.json", result)
    return result


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="action", required=True)
    for action, names in (("construct",("dense","legacy","output")),
                          ("evaluate",("candidates","legacy","prepared","output")),
                          ("_phase-worker",("request",)),("_decode-worker",("job",))):
        item = sub.add_parser(action)
        for name in names:
            item.add_argument("--"+name, required=True, type=Path)
    return p


def main():
    args = parser().parse_args()
    try:
        if args.action == "_decode-worker":
            decode_worker(args.job)
            return 0
        config = protocol()
        preflight_hash = require_preflight()
        if args.action == "_phase-worker":
            request = json.loads(args.request.read_text())
            if request["preflight_sha256"] != preflight_hash:
                raise ValueError("phase preflight identity differs")
            if request["action"] == "construct":
                construct(*(Path(request[k]) for k in ("dense","legacy","output")))
            elif request["action"] == "evaluate":
                evaluate(*(Path(request[k]) for k in ("candidates","legacy","prepared","output")))
            else:
                raise ValueError("unknown phase")
            return 0
        output = args.output.resolve()
        io.external(output, ROOT)
        input_names = ("dense","legacy") if args.action == "construct" else ("candidates","legacy","prepared")
        io.disjoint_output(output, tuple(getattr(args,k) for k in input_names))
        supervision = Path(str(output)+"-supervision")
        io.external(supervision, ROOT)
        io.disjoint_output(supervision, tuple(getattr(args,k) for k in input_names)+(output,))
        supervision.mkdir(parents=True)
        request = {k:str(getattr(args,k).resolve()) for k in (*input_names,"output")}
        request.update(action=args.action, preflight_sha256=preflight_hash)
        io.write_new(supervision/"request.json",request)
        roots = [output,supervision]
        if args.action == "evaluate":
            roots += [args.candidates,Path(str(args.candidates)+"-supervision")]
        limits = config["limits"]
        result = io.supervise([sys.executable,str(Path(__file__).resolve()),"_phase-worker",
                               "--request",str(supervision/"request.json")],
                              limits["max_"+args.action+"_process_seconds"],
                              limits["max_host_rss_kib_per_process"],limits["max_attempt_output_bytes"],
                              roots,supervision/"phase")
        completion = output/("candidates-frozen.private.json" if args.action=="construct" else "report.private.json")
        value = json.loads(completion.read_text())
        reported = value["resources"]
        if max(reported["self_peak_rss_kib"],reported["waited_children_peak_rss_kib"]) > limits["max_host_rss_kib_per_process"]:
            raise ValueError("phase reported peak RSS failed")
        result.update(protocol_sha256=model.PROTOCOL_SHA256,completion_sha256=io.sha(completion),
                      reported_phase_resources=reported)
        if io.tree_bytes(roots)+io.json_bytes(result) > limits["max_attempt_output_bytes"]:
            raise ValueError("final watchdog report exceeds attempt output cap")
        io.write_new(supervision/"report.json",result)
        if io.tree_bytes(roots) > limits["max_attempt_output_bytes"]:
            raise ValueError("final attempt output exceeds cap")
        print(json.dumps({"status":value.get("status","CANDIDATES_FROZEN"),
                          "protocol_sha256":model.PROTOCOL_SHA256},sort_keys=True))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(json.dumps({"status":"INVALID","error":str(exc)}),file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
