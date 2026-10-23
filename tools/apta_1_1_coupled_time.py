#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""CT-04 construct/verify/evaluate phases. Real dispatch requires frozen tokens."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import time

import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
REGISTRATION_SHA256 = "681fb6f20eb93b2817b7feee683e6f442c142573fe9e4c4860875b209237d4d6"
METRICS = ("beat_f1", "downbeat_f1", "beat_cmlt", "downbeat_cmlt")
RUNTIME_COMPONENTS = ("tools/apta_1_1_coupled_time.py", "tools/apta_coupled_time.py",
                      "tools/apta_coupled_time_verify.py", "tools/apta_dense_runner_support.py")


def settings():
    """No environment/test switches. Missing CT04/CT05 records disable every role."""
    docs = ROOT / "docs/rust"
    bindings = {}
    protocol = io.read_bound(docs / "COUPLED-TIME-DEVELOPMENT-PROTOCOL.json", REGISTRATION_SHA256, bindings)
    for name, digest in protocol["binding"].items():
        io.bind(docs / name, digest, bindings)
    for name, digest in protocol["component_sha256"].items():
        io.bind(ROOT / name, digest, bindings)
    ct02 = json.loads((docs / "COUPLED-TIME-CT02-20261023.json").read_text())
    if not all(ct02.get(key) is True for key in ("generated_preflight_passed", "independent_exact_replay_passed", "resource_enforcement_passed")):
        raise ValueError("CT02 preflight incomplete")
    for name in RUNTIME_COMPONENTS[1:]:
        io.bind(ROOT / name, ct02["component_sha256"][name], bindings)
    preflight_path = docs / "COUPLED-TIME-CT04-20261023.json"
    authorization_path = docs / "COUPLED-TIME-ATTEMPT-AUTHORIZATION.json"
    if not preflight_path.is_file() or not authorization_path.is_file():
        raise ValueError("CT04 preflight and distinct CT05 authorization required; real execution disabled")
    preflight = io.read_bound(preflight_path, io.sha(preflight_path), bindings)
    authorization = io.read_bound(authorization_path, io.sha(authorization_path), bindings)
    if (preflight.get("generated_operational_preflight_passed") is not True
            or preflight.get("registration_sha256") != REGISTRATION_SHA256
            or authorization.get("format") != "libapta-coupled-time-authorized-attempt-1"
            or authorization.get("authorized") is not True
            or authorization.get("scope") != "EXPOSED_DIAGNOSTIC_ONLY"
            or authorization.get("registration_sha256") != REGISTRATION_SHA256
            or authorization.get("ct04_preflight_sha256") != io.sha(preflight_path)
            or type(authorization.get("maximum_attempts")) is not int
            or authorization.get("maximum_attempts") != 1
            or not re.fullmatch(r"[0-9a-f]{64}", authorization.get("attempt_path_sha256", ""))):
        raise ValueError("incomplete or stale operational/authorization token")
    for name, digest in preflight["component_sha256"].items():
        io.bind(ROOT / name, digest, bindings)
    if any(name not in preflight["component_sha256"] for name in RUNTIME_COMPONENTS):
        raise ValueError("operational component binding incomplete")
    return {"protocol": protocol, "source_root": str(ROOT), "bindings": bindings,
            "registration_sha256": REGISTRATION_SHA256,
            "attempt_path_sha256": authorization["attempt_path_sha256"]}


def command(*args):
    return [sys.executable, str(Path(__file__).resolve()), *map(str, args)]


def digest_path(path):
    return hashlib.sha256(str(Path(path).resolve()).encode("utf-8")).hexdigest()


def _read(path, digest, bindings):
    io.bind(path, digest, bindings)
    # Strict duplicate/nonfinite handling without importing the verifier/scorer.
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON field")
            result[key] = value
        return result

    def constant(_name):
        raise ValueError("nonfinite JSON")

    value = json.loads(Path(path).read_text(), object_pairs_hook=pairs, parse_constant=constant)
    io.bind(path, digest, bindings)
    return value


def inputs(path, cfg, bindings):
    p = cfg["protocol"]
    value = _read(path, p["input_identity"]["ct03_label_free_inputs_sha256"], bindings)
    required = {"format", "scope", "registration_parent_revision", "source_sample_rate", "sources", "bindings",
                "input_order", "protocol_sha256", "candidate_constructed", "execution_enabled"}
    if (set(value) != required or value["format"] != "libapta-coupled-time-label-free-inputs-1"
            or value["source_sample_rate"] != 48000 or value["protocol_sha256"] != p["synthetic_protocol_sha256"]):
        raise ValueError("unsupported input manifest")
    sources = value["sources"]
    if len(sources) != p["input_identity"]["sources"]:
        raise ValueError("incomplete source cohort")
    keys = {"track", "events_path", "events_sha256", "capture_record_path", "capture_record_sha256",
            "source_audio_sha256", "total_frames", "windows", "beat_frames", "downbeat_ordinals", "known_gaps"}
    ids = []
    for row in sources:
        if set(row) != keys or not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", row["track"]):
            raise ValueError("plain source input fields required")
        ids.append(row["track"])
        events = _read(row["events_path"], row["events_sha256"], bindings)
        record = _read(row["capture_record_path"], row["capture_record_sha256"], bindings)
        if (events["track"] != row["track"] or record["track"] != row["track"]
                or events["audio_sha256"] != row["source_audio_sha256"]
                or record["audio_sha256"] != row["source_audio_sha256"]
                or events["events"] != record["events"] or row["beat_frames"] != events["events"]["beat_frames"]
                or [row["beat_frames"][i] for i in row["downbeat_ordinals"]] != events["events"]["downbeat_frames"]
                or row["windows"] != [{k: w[k] for k in ("first_frame", "end_frame")} for w in record["geometry"]["windows"]]
                or row["total_frames"] != row["windows"][-1]["end_frame"]):
            raise ValueError("original event/ordinal/geometry provenance differs")
    if len(set(ids)) != len(ids) or sum(len(row["windows"]) for row in sources) != p["input_identity"]["windows"]:
        raise ValueError("source/window denominator differs")
    expected_bindings = {row[k]: row[h] for row in sources for k, h in
                         (("events_path", "events_sha256"), ("capture_record_path", "capture_record_sha256"))}
    if value["bindings"] != expected_bindings:
        raise ValueError("label-free binding map differs")
    return sources


def _start(cfg, attempt):
    if digest_path(attempt) != cfg["attempt_path_sha256"]:
        raise ValueError("attempt destination not authorized")
    io.external(attempt, ROOT)
    bindings = dict(cfg["bindings"])
    revision = io.source_snapshot(Path(cfg["source_root"]), bindings)
    return revision, bindings


def _finish(cfg, revision, bindings):
    io.recheck(bindings)
    io.check_source(Path(cfg["source_root"]), revision)


def _write(attempt, path, value, cfg):
    if io.tree_bytes((attempt,)) + io.json_bytes(value) > cfg["protocol"]["limits"]["max_attempt_output_bytes"]:
        raise ValueError("completion output cap exceeded")
    io.write_new(path, value)


def _envelope(attempt, role, cfg, bindings):
    report_path = attempt / (role + ".completion.json")
    report = _read(report_path, io.sha(report_path), bindings)
    resources = _read(attempt / (role + ".phase.resources.json"), io.sha(attempt / (role + ".phase.resources.json")), bindings)
    envelope = _read(attempt / (role + ".supervised.json"), io.sha(attempt / (role + ".supervised.json")), bindings)
    limits = cfg["protocol"]["limits"]
    if (resources["status"] != "PASS" or envelope["status"] != "PASS"
            or resources["wall_seconds"] > limits["max_phase_seconds"]
            or resources["max_observed_process_rss_kib"] > limits["max_process_rss_kib"]
            or envelope["completion_sha256"] != io.sha(report_path)
            or envelope["resources_sha256"] != io.sha(attempt / (role + ".phase.resources.json"))
            or envelope["registration_sha256"] != cfg["registration_sha256"]):
        raise ValueError("phase completion is not independently supervised")
    io.recheck(report["bindings"])
    return report


def frozen(attempt, cfg, bindings):
    complete = _envelope(attempt, "construct", cfg, bindings)
    if (complete["format"] != "libapta-coupled-time-frozen-1" or complete["reference_labels_loaded"] is not False
            or complete["registration_sha256"] != cfg["registration_sha256"]):
        raise ValueError("unsupported candidate freeze")
    io.check_source(Path(cfg["source_root"]), complete["source_revision"])
    sources = inputs(Path(complete["inputs_path"]), cfg, bindings)
    rows = complete["candidates"]
    if [row["track"] for row in rows] != [row["track"] for row in sources]:
        raise ValueError("candidate cohort differs")
    for index, row in enumerate(rows):
        io.bind(attempt / "candidates" / (str(index) + ".json"), row["sha256"], bindings)
    return complete, sources


def source_worker(role, attempt, index):
    cfg = settings()
    limits = cfg["protocol"]["limits"]
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (limits["max_process_rss_kib"] * 1024,) * 2)
    if digest_path(attempt) != cfg["attempt_path_sha256"]:
        raise ValueError("worker attempt identity differs")
    declaration = _read(attempt / "construction.json", io.sha(attempt / "construction.json"), {})
    bindings = dict(cfg["bindings"])
    io.check_source(Path(cfg["source_root"]), declaration["source_revision"])
    rows = inputs(Path(declaration["inputs_path"]), cfg, bindings)
    if not 0 <= index < len(rows):
        raise ValueError("source index outside registered cohort")
    row = rows[index]
    plain = {key: row[key] for key in ("beat_frames", "downbeat_ordinals", "total_frames", "windows", "known_gaps")}
    job = _read(attempt / "jobs" / (str(index) + ".json"), declaration["job_sha256"][index], bindings)
    if job != plain or set(job) != set(plain):
        raise ValueError("worker job contains unregistered fields or observations")
    started = time.monotonic()
    target = attempt / "candidates" / (str(index) + ".json")
    if role == "construct":
        import apta_coupled_time as model
        io.runtime_snapshot(bindings)
        candidate = model.construct(job["beat_frames"], job["downbeat_ordinals"], job["total_frames"], job["windows"], job["known_gaps"])
        payload = json.dumps(candidate, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
        with target.open("xb") as stream:
            stream.write(payload)
    elif role == "verify":
        import apta_coupled_time_verify as verifier
        io.runtime_snapshot(bindings)
        complete, _ = frozen(attempt, cfg, bindings)
        expected = complete["candidates"][index]["sha256"]
        payload = target.read_bytes()
        verifier.verify_bytes(job["beat_frames"], job["downbeat_ordinals"], job["total_frames"], job["windows"], payload, expected, job["known_gaps"])
    else:
        raise ValueError("invalid source role")
    io.runtime_recheck(bindings)
    _finish(cfg, declaration["source_revision"], bindings)
    if any(name in sys.modules for name in ("apta_1_1_dense_decoder", "apta_1_1_neural_comparison", "mir_eval")):
        raise ValueError("scoring imported before evaluation")
    if ("apta_coupled_time" in sys.modules) != (role == "construct"):
        raise ValueError("constructor/verifier import isolation differs")
    _write(attempt, attempt / role / (str(index) + ".json"),
           {"status": "PASS", "candidate_sha256": hashlib.sha256(payload).hexdigest(),
            "self_peak_rss_kib": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
            "wall_seconds": time.monotonic() - started, "bindings": bindings,
            "scoring_imported": False, "reference_labels_loaded": False}, cfg)


def phase(role, attempt, inputs_path=None, inventory_path=None):
    cfg = settings()
    revision, bindings = _start(cfg, attempt)
    p, limits = cfg["protocol"], cfg["protocol"]["limits"]
    if role == "construct":
        sources = inputs(inputs_path, cfg, bindings)
        io.disjoint_output(attempt / "candidates", tuple(bindings))
        (attempt / "candidates").mkdir()
        (attempt / "jobs").mkdir()
        (attempt / role).mkdir()
        jobs = []
        for index, row in enumerate(sources):
            job = {key: row[key] for key in ("beat_frames", "downbeat_ordinals", "total_frames", "windows", "known_gaps")}
            target = attempt / "jobs" / (str(index) + ".json")
            _write(attempt, target, job, cfg)
            io.bind(target, io.sha(target), bindings)
            jobs.append(io.sha(target))
        _write(attempt, attempt / "construction.json", {"source_revision": revision,
               "inputs_path": str(inputs_path.resolve()), "job_sha256": jobs}, cfg)
        io.bind(attempt / "construction.json", io.sha(attempt / "construction.json"), bindings)
    else:
        complete, sources = frozen(attempt, cfg, bindings)
        if role == "evaluate":
            verification = _envelope(attempt, "verify", cfg, bindings)
            if (verification["format"] != "libapta-coupled-time-verified-1"
                    or verification["source_revision"] != revision
                    or verification["registration_sha256"] != cfg["registration_sha256"]
                    or verification["reference_labels_loaded"] is not False
                    or verification["candidate_freeze_sha256"] != io.sha(attempt / "construct.completion.json")
                    or verification["candidate_hashes"] != [row["sha256"] for row in complete["candidates"]]):
                raise ValueError("verification freeze mismatch")
            # Only after complete freeze AND independent supervised verification.
            return evaluate(attempt, inventory_path, cfg, complete, sources, revision, bindings)
        (attempt / role).mkdir()
    rows = []
    for index, source in enumerate(sources):
        result = io.supervise(command("_worker", role, str(index), "--attempt", attempt),
                              limits["max_source_seconds"], limits["max_process_rss_kib"],
                              limits["max_attempt_output_bytes"], (attempt,), attempt / role / (str(index) + ".worker"), group=False)
        if result["status"] != "PASS":
            raise ValueError("source supervision failed")
        path = attempt / role / (str(index) + ".json")
        report = _read(path, io.sha(path), bindings)
        if report["status"] != "PASS" or report["self_peak_rss_kib"] > limits["max_process_rss_kib"]:
            raise ValueError("invalid source completion")
        io.recheck(report["bindings"])
        target = attempt / "candidates" / (str(index) + ".json")
        io.bind(target, report["candidate_sha256"], bindings)
        rows.append({"track": source["track"], "sha256": report["candidate_sha256"], "worker_report_sha256": io.sha(path)})
    _finish(cfg, revision, bindings)
    result = {"format": "libapta-coupled-time-frozen-1" if role == "construct" else "libapta-coupled-time-verified-1",
              "status": "PASS", "source_revision": revision, "registration_sha256": cfg["registration_sha256"],
              "reference_labels_loaded": False, "bindings": bindings}
    if role == "construct":
        result.update(inputs_path=str(inputs_path.resolve()), candidates=rows)
    else:
        result.update(candidate_freeze_sha256=io.sha(attempt / "construct.completion.json"),
                      candidate_hashes=[row["sha256"] for row in rows])
    _write(attempt, attempt / (role + ".completion.json"), result, cfg)
    return result


def evaluate(attempt, inventory_path, cfg, complete, sources, revision, bindings):
    """Only this path imports scoring modules or parses reference labels."""
    p = cfg["protocol"]
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
        _write(attempt, path, {"track": row["track"], "scores": scored}, cfg)
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
    verdicts = scorer.gates(p, totals, paired, old_means, new_means)
    io.runtime_recheck(bindings, original_freeze["bindings"])
    _finish(cfg, revision, bindings)
    result = {"format": "libapta-coupled-time-evaluation-1", "status": "EXPOSED_DIAGNOSTIC_PASS_ONLY" if all(verdicts.values()) else "REJECTED",
              "source_revision": revision, "registration_sha256": cfg["registration_sha256"],
              "candidate_freeze_sha256": io.sha(attempt / "construct.completion.json"),
              "verification_sha256": io.sha(attempt / "verify.completion.json"),
              "gates": verdicts, "totals": totals, "paired": paired, "old_event_means": old_means,
              "candidate_event_means": new_means, "by_genre": genres, "original_regressions": regressions,
              "details": details, "bindings": bindings, "sources": count, "reference_labels_loaded": True,
              "confidence_calibrated": False, "independent_transfer": False, "native_graph_constructed": False,
              "physical_p4": False, "formal_acceptance": False, "production_promotion": False}
    _write(attempt, attempt / "evaluate.completion.json", result, cfg)
    return result


def dispatch(role, attempt, inputs_path=None, inventory_path=None):
    cfg = settings()
    attempt = Path(attempt).resolve()
    _start(cfg, attempt)
    if role == "construct":
        io.disjoint_output(attempt, (inputs_path,))
        attempt.mkdir(parents=True)
    else:
        if not attempt.is_dir() or (attempt / (role + ".phase.stdout")).exists():
            raise ValueError("phase missing attempt or already started")
    limits = cfg["protocol"]["limits"]
    args = ["_phase", role, "--attempt", str(attempt)]
    if inputs_path is not None:
        args += ["--inputs", str(Path(inputs_path).resolve())]
    if inventory_path is not None:
        args += ["--inventory", str(Path(inventory_path).resolve())]
    try:
        supervision = io.supervise(command(*args), limits["max_phase_seconds"], limits["max_process_rss_kib"],
                                    limits["max_attempt_output_bytes"], (attempt,), attempt / (role + ".phase"))
        if supervision["status"] != "PASS":
            raise ValueError("phase supervision invalid")
        completion = attempt / (role + ".completion.json")
        if not completion.is_file():
            raise ValueError("phase completion absent")
        result = {"status": "PASS", "completion_sha256": io.sha(completion),
                  "resources_sha256": io.sha(attempt / (role + ".phase.resources.json")),
                  "registration_sha256": cfg["registration_sha256"]}
        _write(attempt, attempt / (role + ".supervised.json"), result, cfg)
        return result
    except BaseException as error:
        invalid = attempt / (role + ".INVALID.json")
        if not invalid.exists():
            io.write_new(invalid, {"status": "INVALID", "error": str(error), "musical_verdict_valid": False})
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    for role in ("construct", "verify", "evaluate"):
        part = sub.add_parser(role)
        part.add_argument("--attempt", required=True, type=Path)
        if role == "construct":
            part.add_argument("--inputs", required=True, type=Path)
        if role == "evaluate":
            part.add_argument("--inventory", required=True, type=Path)
    for action in ("_phase", "_worker"):
        part = sub.add_parser(action)
        part.add_argument("role", choices=("construct", "verify") if action == "_worker" else ("construct", "verify", "evaluate"))
        if action == "_worker":
            part.add_argument("index", type=int)
        part.add_argument("--attempt", required=True, type=Path)
        part.add_argument("--inputs", type=Path)
        part.add_argument("--inventory", type=Path)
    args = parser.parse_args()
    if args.action == "_worker":
        source_worker(args.role, args.attempt, args.index)
    elif args.action == "_phase":
        phase(args.role, args.attempt, args.inputs, args.inventory)
    else:
        dispatch(args.action, args.attempt, getattr(args, "inputs", None), getattr(args, "inventory", None))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, TypeError, IndexError) as error:
        print(json.dumps({"status": "INVALID", "error": str(error)}), file=sys.stderr)
        sys.exit(2)
