#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explicit host-only, frozen saved-event timing experiment; no inference."""
from __future__ import annotations

import argparse
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path
import subprocess
import time

import apta_1_1_event_representation as representation

ref = representation.ref
Q = 1 << 32
PROTOCOL_PATH = ref.ROOT / "docs/rust/MULTIBEAT-TIMING-PROTOCOL.json"
PROTOCOL_SHA256 = "66ab4e3a5fa05dd5b4eb120609ecfa79d900e8876a131005e147275e02b67bd9"


def protocol() -> dict:
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("multi-beat protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def project(beats: list[int], downbeats: list[int], first: int, end: int,
            total: int, gaps: tuple = ()) -> dict:
    """Label-blind four-neighbor OLS. Unknown support never becomes PCM gaps."""
    config = protocol()["algorithm"]
    if type(total) is not int or not 0 < total <= config["maximum_source_frames"]:
        raise ValueError("source geometry exceeds registered cap")
    ref.valid_events(beats, total)
    ref.valid_events(downbeats, total)
    if (not set(downbeats) <= set(beats) or len(beats) > config["maximum_source_events"]
            or type(first) is not int or type(end) is not int or not 0 <= first < end <= total):
        raise ValueError("invalid event/window geometry")
    previous_end = 0
    for left, right in gaps:
        if type(left) is not int or type(right) is not int or not previous_end <= left < right <= total:
            raise ValueError("invalid ordered disjoint PCM gaps")
        previous_end = right
    result = {"first_frame": first, "end_frame": end,
              "selected": {"kind": "insufficient_events"}, "meter": {}, "support": {}}
    indices = [i for i, value in enumerate(beats) if first <= value < end]
    if not indices:
        return result
    a, b = indices[0], indices[-1] + 1
    start = max(0, a - config["neighbor_events_per_side"])
    stop = min(len(beats), b + config["neighbor_events_per_side"])
    support = beats[start:stop]
    result["support"] = {"first_ordinal": start, "events": len(support),
                         "anchor_ordinal": a, "in_window_events": b - a}

    def reject(reason):
        result["selected"] = {"kind": reason}
        return result

    context_first, context_end = min(first, support[0]), max(end, support[-1] + 1)
    if any(left < context_end and context_first < right for left, right in gaps):
        return reject("known_pcm_gap")
    if len(support) < config["minimum_support_events"]:
        return result
    n = len(support)
    sx = n * (n - 1) // 2
    sxx = n * (n - 1) * (2 * n - 1) // 6
    period = Fraction(n * sum(i * p for i, p in enumerate(support)) - sx * sum(support),
                      n * sxx - sx * sx)
    tempo = ref.rounded(Fraction(ref.RATE * 60000, 1) / period)
    if not 40000 <= tempo <= 300000:
        return reject("unsupported_tempo")
    intercept = Fraction(sum(support), n) - period * Fraction(sx, n)
    anchor = ref.rounded(intercept + (a - start) * period)
    if not first <= anchor < end:
        return reject("anchor_outside_window")
    published_period = (ref.RATE * 60000 << 32) // tempo
    residual = max(abs((p - anchor) * Q - (i + start - a) * published_period)
                   for i, p in enumerate(support))
    result["support"]["maximum_residual_q32"] = residual
    if residual > config["maximum_residual_frames"] * Q:
        return reject("inconsistent_events")
    count = ((end - anchor) * Q + published_period - 1) // published_period
    if count != b - a:
        return reject("event_count_changed")
    result["selected"] = {"kind": "accepted", "anchor_frame": anchor,
                          "tempo_millibpm": tempo, "confidence": 0}
    meter = representation.meter_on_events(beats, downbeats, first, end)
    if meter:
        position = anchor * Q + (meter["downbeat_ordinal"] - a) * published_period
        if first * Q <= position < end * Q:
            result["meter"] = {"kind": "estimated", "numerator": meter["numerator"],
                "denominator": 4, "downbeat_frame": position >> 32,
                "downbeat_fraction_q32": position & (Q - 1)}
            result["support"]["downbeat_ordinal"] = meter["downbeat_ordinal"]
    return result


def check_partition(windows: list[dict], total: int) -> dict:
    """Reject overlapping geometry; preserve analysis abstention separately."""
    cap = protocol()["algorithm"]["maximum_windows"]
    if not windows or len(windows) > cap:
        raise ValueError("window count exceeds registered cap")
    first, previous_point, next_ordinal = 0, None, None
    unsupported, accepted = [], 0
    for window in windows:
        if window["first_frame"] != first or not first < window["end_frame"] <= total:
            raise ValueError("windows must retain the complete fixed source partition")
        first = window["end_frame"]
        if window["selected"]["kind"] != "accepted":
            unsupported.append([window["first_frame"], first])
            previous_point = next_ordinal = None
            continue
        points = representation.points_for_control(window)
        ordinal = window["support"]["anchor_ordinal"]
        if (not points or len(points) != window["support"]["in_window_events"]
                or points[0][0] < window["first_frame"] * Q
                or points[-1][0] >= first * Q
                or previous_point is not None and (points[0][0] <= previous_point or ordinal != next_ordinal)):
            raise ValueError("reconstruction changed event count/order/ordinal continuity")
        previous_point, next_ordinal = points[-1][0], ordinal + len(points)
        accepted += 1
    if first != total:
        raise ValueError("source tail must remain in the diagnostic")
    return {"accepted_segments": accepted, "unknown_support_ranges": unsupported,
            "known_pcm_gaps": [], "full_source_support": not unsupported,
            "native_graph_constructed": False, "production_activation": False}


def paired(before: dict, after: dict) -> dict:
    result = {}
    for field in ("period", "phase", "joint"):
        key = "confirmed_" + field + "_windows"
        result[field + "_fixes"] = int(not before[key] and bool(after[key]))
        result[field + "_breaks"] = int(bool(before[key]) and not after[key])
    result["meter_breaks"] = int(bool(before["meter_correct"]) and not after["meter_correct"])
    result["downbeat_breaks"] = int(bool(before["downbeat_correct"]) and not after["downbeat_correct"])
    return result


def gates(totals: dict, changes: dict) -> dict:
    config = protocol()["evaluation"]
    result = {
        "joint_improves": totals["candidate"]["confirmed_joint_windows"] >= config["minimum_joint_windows"],
        "complete_sequence_improves": totals["common_candidate"]["complete_sequence_windows"] >= config["minimum_complete_sequence_windows"],
        "zero_coarse_joint_breaks": changes["coarse"]["joint_breaks"] <= config["maximum_coarse_joint_breaks"],
        "zero_old_correct_meter_breaks": changes["ols"]["meter_breaks"] <= config["maximum_old_correct_meter_breaks"],
        "zero_old_correct_downbeat_breaks": changes["ols"]["downbeat_breaks"] <= config["maximum_old_correct_downbeat_breaks"],
    }
    for field in ("period", "phase", "joint"):
        result["zero_ols_" + field + "_breaks"] = changes["ols"][field + "_breaks"] <= config["maximum_ols_" + field + "_breaks"]
    return result


def run(attempt: Path, prepared: Path, output: Path) -> dict:
    import resource
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("output exists; refusing retry or overwrite")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before scoring saved events")
    revision = ref.revision(ref.ROOT)
    bindings = {}
    paths = {"previous_freeze_sha256": attempt / "freeze.private.json",
             "previous_scoring_sha256": attempt / "scoring/report.json",
             "beat_this_worker_report_sha256": attempt / "beat_this/report.json",
             "prepared_manifest_sha256": prepared / "manifest.private.json",
             "representation_result_sha256": ref.ROOT / "docs/rust/EVENT-REPRESENTATION-RESULT-20261022.json"}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    manifest = json.loads(paths["prepared_manifest_sha256"].read_text())
    old_report = json.loads(paths["previous_scoring_sha256"].read_text())
    worker = json.loads(paths["beat_this_worker_report_sha256"].read_text())
    ids = [row["track"] for row in manifest["tracks"]]
    if (len(ids) != 100 or len(set(ids)) != 100 or ids != [r["track"] for r in worker["outputs"]]
            or ids != [r["track"] for r in old_report["details"]]
            or manifest["holdout_performances_opened"] is not False):
        raise ValueError("fixed development cohort/order required")
    for row in worker["outputs"]:
        ref.bind(attempt / "beat_this" / (row["track"] + ".json"), row["events_sha256"], bindings)
    for row in old_report["details"]:
        ref.bind(attempt / "scoring" / (row["track"] + ".json"), row["sha256"], bindings)
        path = attempt / "scoring" / (row["track"] + "-coarse.json")
        ref.bind(path, ref.history.sha256_file(path), bindings)
    output.mkdir(parents=True)
    ref.history.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": PROTOCOL_SHA256, "bindings": bindings, "track_order": ids,
        "candidate_scoring_started": False, "formal_acceptance": False})
    started = time.monotonic()
    totals = {name: Counter() for name in ("old_coarse", "old_ols", "candidate", "common_ols", "common_candidate")}
    changes = {name: Counter() for name in ("ols", "coarse")}
    kinds, counts, original_breaks, details = Counter(), Counter(), [], []
    for truth in manifest["tracks"]:
        track, total = truth["track"], truth["requested_frames"]
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        beats, downbeats = events["beat_frames"], events["downbeat_frames"]
        saved = json.loads((attempt / "scoring" / (track + ".json")).read_text())
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        # Candidate sees saved events and geometry only, never truth or scores.
        candidates = [project(beats, downbeats, w["first_frame"], w["end_frame"], total) for w in coarse["windows"]]
        partition = check_partition(candidates, total)
        counts["sources"] += 1
        counts["full_source_support"] += int(partition["full_source_support"])
        counts["accepted_segments"] += partition["accepted_segments"]
        records = []
        for i, (old, candidate) in enumerate(zip(coarse["windows"], candidates)):
            first, end = old["first_frame"], old["end_frame"]
            control = ref.project(beats, downbeats, first, end)
            old_score = ref.annotated.score_window(control, truth, ref.RATE)
            before = ref.annotated.score_window(old, truth, ref.RATE)
            previous = saved["models"]["beat_this"]["windows"][i]
            if control != previous["candidate"] or old_score != previous["score"] or before != saved["coarse"][i]:
                raise ValueError("saved baseline tuples/verdicts changed")
            score = ref.annotated.score_window(candidate, truth, ref.RATE)
            common = representation.score_points(representation.points_for_control(candidate), truth, first, end)
            common_ols = representation.score_points(representation.points_for_control(control), truth, first, end)
            for result, point_result in ((score, common), (old_score, common_ols)):
                for field in ("period", "phase", "joint"):
                    if result["confirmed_" + field + "_windows"] != point_result[field + "_windows"]:
                        raise ValueError("common diagnostic/scorer disagreement")
            for name, result in (("old_coarse", before), ("old_ols", old_score), ("candidate", score),
                                 ("common_ols", common_ols), ("common_candidate", common)):
                totals[name].update(result)
            deltas = {"ols": paired(old_score, score), "coarse": paired(before, score)}
            for name, result in deltas.items():
                changes[name].update(result)
            kinds[candidate["selected"]["kind"]] += 1
            if before["confirmed_joint_windows"] and not old_score["confirmed_joint_windows"]:
                original_breaks.append({"track": track, "window": i, "candidate": score})
            records.append({"candidate": candidate, "score": score, "old_ols": old_score,
                            "old_coarse": before, "common_candidate": common, "common_ols": common_ols,
                            "paired": deltas})
        target = output / (track + ".json")
        ref.history.write_new(target, {"track": track, "windows": records, "partition": partition})
        details.append({"track": track, "sha256": ref.history.sha256_file(target)})
        if time.monotonic() - started > config["resources"]["max_run_seconds"]:
            raise ValueError("registered wall-time cap exceeded")
    if (dict(totals["old_ols"]) != old_report["totals"]["beat_this"]
            or dict(totals["old_coarse"]) != old_report["totals"]["coarse"]
            or totals["candidate"]["windows"] != 600
            or totals["candidate"]["reference_complete_windows"] != 401
            or len(original_breaks) != 2):
        raise ValueError("fixed cohort/baseline aggregate mismatch")
    ref.recheck(bindings)
    for row in details:
        ref.bind(output / (row["track"] + ".json"), row["sha256"], {})
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True) or ref.revision(ref.ROOT) != revision:
        raise ValueError("source changed during run")
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if rss > config["resources"]["max_host_rss_kib"]:
        raise ValueError("registered RSS cap exceeded")
    if time.monotonic() - started > config["resources"]["max_run_seconds"]:
        raise ValueError("registered wall-time cap exceeded during final recheck")
    verdicts = gates(totals, changes)
    result = {"format": config["format"], "source_revision": revision, "protocol_sha256": PROTOCOL_SHA256,
        "formal_acceptance": False, "production_promotion": False, "inference_run": False,
        "confidence_calibrated": False, "independent_transfer": False, "physical_p4": False,
        "development_gate_passed": all(verdicts.values()), "gates": verdicts,
        "totals": {k: dict(v) for k, v in totals.items()}, "paired": {k: dict(v) for k, v in changes.items()},
        "kinds": dict(kinds), "counts": dict(counts), "original_joint_breaks": original_breaks,
        "resources": {"run_wall_seconds": time.monotonic() - started, "rss_peak_kib": rss},
        "details": details, "bindings": bindings}
    ref.history.write_new(output / "report.private.json", result)
    return {k: v for k, v in result.items() if k not in ("details", "bindings", "original_joint_breaks")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("attempt", "prepared", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.attempt, args.prepared, args.output), sort_keys=True))


if __name__ == "__main__":
    main()
