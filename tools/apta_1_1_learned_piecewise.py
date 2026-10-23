#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Frozen label-blind construction, then separately bound development scoring."""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import time

import apta_learned_piecewise as model
import apta_1_1_event_representation as representation

ref = representation.ref
Q = model.Q
PROTOCOL_PATH = ref.ROOT / "docs/rust/LEARNED-PIECEWISE-PROTOCOL.json"
PROTOCOL_SHA256 = "007b1c0cb834c5768947e2df9f1431e6a8483a9ea1a51eace42d0f76c7f4e7fc"
METER_KEYS = ("meter_emitted", "meter_correct", "downbeat_scored", "downbeat_correct")


def protocol():
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("learned-piecewise protocol changed")
    value = json.loads(PROTOCOL_PATH.read_text())
    for key in ("maximum_source_frames", "maximum_events", "maximum_windows", "maximum_pieces",
                "minimum_piece_events", "maximum_residual_frames", "maximum_seam_error_frames"):
        if value["limits"][key] != getattr(model, key.upper()):
            raise ValueError("constructor differs from registered constants")
    return value


def candidate_points(candidate):
    return [model.expand(w["pieces"]) for w in candidate["windows"]]


def control_points(controls, beats):
    result = []
    for window in controls:
        ordinal = next((i for i, p in enumerate(beats) if window["first_frame"] <= p < window["end_frame"]), None)
        points = representation.points_for_control(window)
        if points and ordinal is None:
            raise ValueError("OLS grid without learned ordinal")
        result.append([(p, ordinal + i, period) for i, (p, period) in enumerate(points)])
    return result


def score_point_windows(points, truth, windows, actual):
    results = []
    for i, (sequence, window) in enumerate(zip(points, windows)):
        values = []
        for j, (point, ordinal, period) in enumerate(sequence):
            if actual:
                next_point = sequence[j + 1] if j + 1 < len(sequence) else (
                    points[i + 1][0] if i + 1 < len(points) and points[i + 1] else None)
                period = next_point[0] - point if next_point is not None and next_point[1] == ordinal + 1 and next_point[0] > point else None
            values.append((point, period))
        results.append(representation.score_points(values, truth, window["first_frame"], window["end_frame"]))
    return results


def meter_score(candidate_window, truth):
    if candidate_window["status"] != "accepted":
        return {key: 0 for key in METER_KEYS}
    piece = candidate_window["pieces"][0]
    proxy = {"first_frame": candidate_window["first_frame"], "end_frame": candidate_window["end_frame"],
             "selected": {"kind": "accepted", "anchor_frame": piece["anchor_frame"],
                          "tempo_millibpm": piece["tempo_millibpm"], "confidence": 0},
             "meter": candidate_window["meter"]}
    # The constant-grid proxy is used only to invoke the unchanged meter logic.
    result = ref.annotated.score_window(proxy, truth, ref.RATE)
    return {key: result[key] for key in METER_KEYS}


def paired(before, after, old_meter, new_meter):
    result = {}
    for key in ("phase", "period", "joint"):
        result[key + "_fixes"] = int(not before[key + "_windows"] and bool(after[key + "_windows"]))
        result[key + "_breaks"] = int(bool(before[key + "_windows"]) and not after[key + "_windows"])
    for key in ("meter", "downbeat"):
        result[key + "_breaks"] = int(bool(old_meter[key + "_correct"]) and not new_meter[key + "_correct"])
    return result


def gates(totals, changes):
    limits = protocol()["evaluation"]["gates"]
    result = {
        "nominal_joint_improves": totals["candidate_nominal"]["joint_windows"] >= limits["minimum_nominal_joint_windows"],
        "nominal_sequence_improves": totals["candidate_nominal"]["complete_sequence_windows"] >= limits["minimum_nominal_complete_sequence_windows"],
        "actual_sequence_improves": totals["candidate_actual"]["complete_sequence_windows"] > totals["ols_actual"]["complete_sequence_windows"],
        "zero_coarse_joint_breaks": changes["coarse"]["joint_breaks"] <= limits["maximum_coarse_joint_breaks"],
        "zero_meter_breaks": changes["ols"]["meter_breaks"] <= limits["maximum_meter_breaks"],
        "zero_downbeat_breaks": changes["ols"]["downbeat_breaks"] <= limits["maximum_downbeat_breaks"]}
    for field in ("period", "phase", "joint"):
        result["zero_ols_" + field + "_breaks"] = changes["ols"][field + "_breaks"] <= limits["maximum_ols_" + field + "_breaks"]
    return result


def run(attempt: Path, prepared: Path, output: Path):
    import resource
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("output exists; refusing retry/overwrite")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before measurement")
    revision, bindings = ref.revision(ref.ROOT), {}
    paths = {"prepared_manifest_sha256": prepared / "manifest.private.json",
             "previous_scoring_sha256": attempt / "scoring/report.json",
             "beat_this_worker_report_sha256": attempt / "beat_this/report.json"}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    worker = json.loads(paths["beat_this_worker_report_sha256"].read_text())
    ids = [r["track"] for r in worker["outputs"]]
    if len(ids) != 100 or len(set(ids)) != 100:
        raise ValueError("fixed 100-source cohort required")
    for row in worker["outputs"]:
        ref.bind(attempt / "beat_this" / (row["track"] + ".json"), row["events_sha256"], bindings)
        coarse = attempt / "scoring" / (row["track"] + "-coarse.json")
        ref.bind(coarse, ref.history.sha256_file(coarse), bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    output.mkdir(parents=True)
    (output / "candidates").mkdir()
    (output / "scoring").mkdir()
    ref.history.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": PROTOCOL_SHA256, "bindings": bindings, "reference_labels_loaded": False})
    started = time.monotonic()
    candidates = []
    # This phase has not parsed the prepared reference manifest or saved scores.
    for track in ids:
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        windows = [{key: w[key] for key in ("first_frame", "end_frame")} for w in coarse["windows"]]
        candidate = model.construct(events["beat_frames"], events["downbeat_frames"], windows, coarse["requested_frames"])
        target = output / "candidates" / (track + ".json")
        ref.history.write_new(target, candidate)
        candidates.append({"track": track, "sha256": ref.history.sha256_file(target)})
        if time.monotonic() - started > config["limits"]["max_run_seconds"]:
            raise ValueError("registered runtime cap exceeded")
    ref.history.write_new(output / "candidates-frozen.private.json", {"source_revision": revision,
        "candidates": candidates, "reference_labels_loaded": False, "all_candidates_complete": True})
    candidate_freeze_hash = ref.history.sha256_file(output / "candidates-frozen.private.json")
    # Labels and old scoring are first parsed only after all candidates are frozen.
    manifest = json.loads(paths["prepared_manifest_sha256"].read_text())
    old_report = json.loads(paths["previous_scoring_sha256"].read_text())
    if (manifest["holdout_performances_opened"] is not False or ids != [r["track"] for r in manifest["tracks"]]
            or ids != [r["track"] for r in old_report["details"]]):
        raise ValueError("fixed reference cohort/order differs")
    for row in old_report["details"]:
        ref.bind(attempt / "scoring" / (row["track"] + ".json"), row["sha256"], bindings)
    totals = {key: Counter() for key in ("old_coarse", "old_ols", "ols_nominal", "ols_actual",
                                        "candidate_nominal", "candidate_actual", "candidate_meter")}
    changes = {key: Counter() for key in ("ols", "coarse")}
    counts, statuses, pieces, original_breaks, details = Counter(), Counter(), Counter(), [], []
    for truth, candidate_row in zip(manifest["tracks"], candidates):
        track = truth["track"]
        path = output / "candidates" / (track + ".json")
        ref.bind(path, candidate_row["sha256"], {})
        candidate = json.loads(path.read_text())
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        geometry = [{key: w[key] for key in ("first_frame", "end_frame")} for w in coarse["windows"]]
        # Replay while labels are in evaluator memory: pure constructor output must be identical.
        if candidate != model.construct(events["beat_frames"], events["downbeat_frames"], geometry, coarse["requested_frames"]):
            raise ValueError("candidate changed after label loading")
        saved = json.loads((attempt / "scoring" / (track + ".json")).read_text())
        controls = [ref.project(events["beat_frames"], events["downbeat_frames"], w["first_frame"], w["end_frame"]) for w in coarse["windows"]]
        cp, op = candidate_points(candidate), control_points(controls, events["beat_frames"])
        diagnostics = {label + "_" + mode: score_point_windows(points, truth, geometry, mode == "actual")
            for label, points in (("candidate", cp), ("ols", op)) for mode in ("nominal", "actual")}
        counts.update(sources=1, full_source_support=int(candidate["full_source_support"]),
            capacity_rejected_sources=int(candidate["capacity_exceeded"]),
            source_seam_rejected_windows=len(candidate["failed_source_seams"]),
            accepted_pieces=sum(len(w["pieces"]) for w in candidate["windows"]))
        pieces[candidate["pieces_before_capacity"]] += 1
        records = []
        for i, (before_window, control, cw) in enumerate(zip(coarse["windows"], controls, candidate["windows"])):
            before = ref.annotated.score_window(before_window, truth, ref.RATE)
            old = ref.annotated.score_window(control, truth, ref.RATE)
            if (control != saved["models"]["beat_this"]["windows"][i]["candidate"]
                    or old != saved["models"]["beat_this"]["windows"][i]["score"] or before != saved["coarse"][i]):
                raise ValueError("old tuple/score changed")
            for field in ("period", "phase", "joint"):
                if diagnostics["ols_nominal"][i][field + "_windows"] != old["confirmed_" + field + "_windows"]:
                    raise ValueError("common OLS/frozen scorer disagreement")
            meter = meter_score(cw, truth)
            cn = diagnostics["candidate_nominal"][i]
            before_common = {key + "_windows": before["confirmed_" + key + "_windows"] for key in ("period", "phase", "joint")}
            paired_changes = {"ols": paired(diagnostics["ols_nominal"][i], cn, old, meter),
                              "coarse": paired(before_common, cn, before, meter)}
            for key, value in (("old_coarse", before), ("old_ols", old), ("candidate_meter", meter)):
                totals[key].update(value)
            for key, values in diagnostics.items():
                totals[key].update(values[i])
            for key, value in paired_changes.items():
                changes[key].update(value)
            statuses[cw["status"]] += 1
            if before["confirmed_joint_windows"] and not old["confirmed_joint_windows"]:
                original_breaks.append({"track": track, "window": i, "nominal": cn,
                                        "actual": diagnostics["candidate_actual"][i], "meter": meter})
            records.append({"diagnostics": {key: value[i] for key, value in diagnostics.items()},
                            "meter": meter, "paired": paired_changes})
        target = output / "scoring" / (track + ".json")
        ref.history.write_new(target, {"track": track, "windows": records})
        details.append({"track": track, "sha256": ref.history.sha256_file(target)})
        if time.monotonic() - started > config["limits"]["max_run_seconds"]:
            raise ValueError("registered runtime cap exceeded")
    if (dict(totals["old_coarse"]) != old_report["totals"]["coarse"] or dict(totals["old_ols"]) != old_report["totals"]["beat_this"]
            or totals["ols_nominal"]["complete_sequence_windows"] != 178 or len(original_breaks) != 2
            or totals["candidate_nominal"]["windows"] != 600 or totals["candidate_nominal"]["reference_complete_windows"] != 401):
        raise ValueError("fixed denominator/baseline aggregate mismatch")
    ref.recheck(bindings)
    ref.bind(output / "candidates-frozen.private.json", candidate_freeze_hash, {})
    for directory, rows in (("candidates", candidates), ("scoring", details)):
        for row in rows:
            ref.bind(output / directory / (row["track"] + ".json"), row["sha256"], {})
    if ref.revision(ref.ROOT) != revision or subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("source mutated during run")
    elapsed, rss = time.monotonic() - started, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if elapsed > config["limits"]["max_run_seconds"] or rss > config["limits"]["max_host_rss_kib"]:
        raise ValueError("registered resource cap exceeded")
    verdicts = gates(totals, changes)
    result = {"format": config["format"], "source_revision": revision, "protocol_sha256": PROTOCOL_SHA256,
        "formal_acceptance": False, "production_promotion": False, "new_audio": False, "inference_run": False,
        "confidence_calibrated": False, "independent_transfer": False, "physical_p4": False,
        "candidate_module_sha256": ref.history.sha256_file(Path(model.__file__)),
        "all_candidates_frozen_before_label_loading": True,
        "candidate_freeze_sha256": candidate_freeze_hash,
        "development_gate_passed": all(verdicts.values()), "gates": verdicts,
        "totals": {key: dict(value) for key, value in totals.items()}, "paired": {key: dict(value) for key, value in changes.items()},
        "counts": dict(counts), "window_statuses": dict(statuses), "source_pieces_before_capacity_histogram": dict(pieces),
        "resources": {"run_wall_seconds": elapsed, "rss_peak_kib": rss},
        "original_regressions": original_breaks, "candidates": candidates, "details": details, "bindings": bindings}
    ref.history.write_new(output / "report.private.json", result)
    return {key: value for key, value in result.items() if key not in ("bindings", "details", "candidates", "original_regressions")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("attempt", "prepared", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.attempt, args.prepared, args.output), sort_keys=True))


if __name__ == "__main__":
    main()
