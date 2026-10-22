#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only attribution of saved timing failures; no detector or audio input."""
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
PROTOCOL_PATH = ref.ROOT / "docs/rust/TIMING-FAILURE-AUDIT-PROTOCOL.json"
PROTOCOL_SHA256 = "8f47045ccd26983b04085aabb4a58d2868f074528bb2031804aed2a3e6270867"
FAMILIES = (("same_period", Fraction(1)), ("half_period_double_bpm", Fraction(1, 2)),
            ("double_period_half_bpm", Fraction(2)), ("two_thirds_period", Fraction(2, 3)),
            ("three_halves_period", Fraction(3, 2)), ("one_third_period", Fraction(1, 3)),
            ("triple_period", Fraction(3)))


def protocol():
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("failure-attribution protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def median(values):
    ordered = sorted(values)
    if not ordered:
        return None
    n = len(ordered)
    return Fraction(ordered[n // 2]) if n % 2 else Fraction(ordered[n // 2 - 1] + ordered[n // 2], 2)


def period_stats(periods, raw_intervals):
    if not periods:
        return {"local_min_frames": None, "local_median_frames": None, "local_max_frames": None,
                "constant_period_intersection_empty": None, "intersection_lower": None,
                "intersection_upper": None, "local_relative_spread": None,
                "raw_interval_min_frames": min(raw_intervals, default=None),
                "raw_interval_max_frames": max(raw_intervals, default=None)}
    lower = max(p * Fraction(99, 100) for p in periods)
    upper = min(p * Fraction(101, 100) for p in periods)
    return {"local_min_frames": float(min(periods)), "local_median_frames": float(median(periods)),
            "local_max_frames": float(max(periods)), "constant_period_intersection_empty": lower > upper,
            "intersection_lower": [lower.numerator, lower.denominator],
            "intersection_upper": [upper.numerator, upper.denominator],
            "local_relative_spread": float(max(periods) / min(periods) - 1),
            "raw_interval_min_frames": min(raw_intervals, default=None),
            "raw_interval_max_frames": max(raw_intervals, default=None)}


def family(window, reference_period):
    if reference_period is None:
        return {"kind": "unavailable_reference", "period_ratio": None}
    if window["selected"]["kind"] != "accepted":
        return {"kind": "unavailable_projection", "period_ratio": None}
    period = Fraction((ref.RATE * 60000 << 32) // window["selected"]["tempo_millibpm"], 1 << 32)
    ratio = period / reference_period
    name = next((name for name, target in FAMILIES if abs(ratio - target) <= target / 100), "unmatched_ratio")
    return {"kind": name, "period_ratio": float(ratio)}


def correspondence(beats, truth_beats, periods, first, end):
    expected = {b for b in truth_beats if first <= b < end}
    counts = Counter({key: 0 for key in ("raw_events", "phase_matched_events", "phase_mismatched_events",
                      "unknown_events", "duplicate_matches", "outside_window_matches")})
    matches = Counter()
    for beat in beats:
        if not first <= beat < end:
            continue
        counts["raw_events"] += 1
        if not truth_beats[0] <= beat <= truth_beats[-1]:
            counts["unknown_events"] += 1
            continue
        nearest = min(truth_beats, key=lambda b: abs(b - beat))
        if abs(beat - nearest) > periods[nearest] / 10:
            counts["phase_mismatched_events"] += 1
            continue
        counts["phase_matched_events"] += 1
        if nearest not in expected:
            counts["outside_window_matches"] += 1
        else:
            matches[nearest] += 1
    counts["reference_events"] = len(expected)
    counts["duplicate_matches"] = sum(max(0, n - 1) for n in matches.values())
    counts["covered_reference"] = sum(matches[b] > 0 for b in expected)
    counts["uniquely_covered_reference"] = sum(matches[b] == 1 for b in expected)
    counts["uncovered_reference"] = sum(matches[b] == 0 for b in expected)
    counts["nonunique_reference"] = sum(matches[b] != 1 for b in expected)
    return dict(counts)


def analyse_window(beats, downbeats, truth, first, end, total):
    limits = protocol()["limits"]
    if (type(total) is not int or not 0 < total <= limits["maximum_source_frames"]
            or type(first) is not int or type(end) is not int or not 0 <= first < end <= total):
        raise ValueError("invalid bounded source/window geometry")
    for sequence in (beats, downbeats, truth["beat_frames"], truth["downbeat_frames"]):
        ref.valid_events(sequence, total)
        if len(sequence) > limits["maximum_events_per_sequence"]:
            raise ValueError("sequence exceeds audit cap")
    if len(truth["beat_frames"]) < 2:
        raise ValueError("at least two reference beats required")
    model = ref.project(beats, downbeats, first, end)
    witness = ref.project(truth["beat_frames"], truth["downbeat_frames"], first, end)
    model_score = ref.annotated.score_window(model, truth, ref.RATE)
    witness_score = ref.annotated.score_window(witness, truth, ref.RATE)
    model_common = representation.score_points(representation.points_for_control(model), truth, first, end)
    witness_common = representation.score_points(representation.points_for_control(witness), truth, first, end)
    for score, common in ((model_score, model_common), (witness_score, witness_common)):
        for key in ("period", "phase", "joint"):
            if score["confirmed_" + key + "_windows"] != common[key + "_windows"]:
                raise ValueError("unchanged scorer/common diagnostic disagreement")
    reference = truth["beat_frames"]
    periods = {b: Fraction(ref.annotated.canonical._local_period(reference, b)) for b in reference}
    inside = [b for b in reference if first <= b < end]
    stats = period_stats([periods[b] for b in inside], [b - a for a, b in zip(inside, inside[1:])])
    tempo_family = family(model, median([periods[b] for b in inside]))
    events = correspondence(beats, reference, periods, first, end)
    flags = {
        "reference_period_incompatibility": stats["constant_period_intersection_empty"] is True,
        "non_unit_tempo_family": tempo_family["kind"] in {name for name, _ in FAMILIES if name != "same_period"},
        "event_correspondence_mismatch": any(events[k] > 0 for k in ("phase_mismatched_events", "duplicate_matches", "outside_window_matches", "uncovered_reference")),
        "reference_witness_joint_fail": not bool(witness_score["confirmed_joint_windows"]),
        "reference_witness_sequence_fail": not bool(witness_common["complete_sequence_windows"]),
    }
    if not model_score["reference_complete_windows"]:
        group = "reference_context_incomplete"
    elif model_score["confirmed_joint_windows"]:
        group = "model_joint_pass"
    elif model["selected"]["kind"] != "accepted":
        group = "projection_unsupported"
    elif flags["reference_period_incompatibility"]:
        group = "reference_period_incompatibility"
    elif flags["non_unit_tempo_family"]:
        group = "non_unit_tempo_family"
    elif flags["event_correspondence_mismatch"]:
        group = "event_correspondence_mismatch"
    elif not model_score["confirmed_period_windows"]:
        group = "period_precision_or_projection"
    else:
        group = "phase_or_projection"
    if group not in protocol()["diagnostics"]["triage_order"]:
        raise ValueError("unregistered triage group")
    return {"first_frame": first, "end_frame": end, "model": model, "reference_witness": witness,
            "model_score": model_score, "witness_score": witness_score,
            "model_common": model_common, "witness_common": witness_common,
            "reference_periods": stats, "tempo_family": tempo_family,
            "correspondence": events, "flags": flags, "triage_group": group}


def aggregate(records):
    totals = {name: Counter() for name in ("old_coarse", "old_ols", "reference_witness", "model_common", "witness_common")}
    groups, failures, families, failure_families = Counter(), Counter(), Counter(), Counter()
    flags, failure_flags, cross, correspondences = Counter(), Counter(), Counter(), Counter()
    compatible = Counter()
    by_genre = {}
    for row in records:
        genre = by_genre.setdefault(row["genre"], {"windows": 0, "complete": 0, "model_joint": 0, "witness_joint": 0})
        for r in row["windows"]:
            for key, value in (("old_coarse", r["coarse_score"]), ("old_ols", r["model_score"]),
                               ("reference_witness", r["witness_score"]), ("model_common", r["model_common"]), ("witness_common", r["witness_common"])):
                totals[key].update(value)
            groups[r["triage_group"]] += 1
            genre["windows"] += 1
            if not r["model_score"]["reference_complete_windows"]:
                continue
            genre["complete"] += 1
            model_ok = bool(r["model_score"]["confirmed_joint_windows"])
            witness_ok = bool(r["witness_score"]["confirmed_joint_windows"])
            genre["model_joint"] += model_ok
            genre["witness_joint"] += witness_ok
            cross[("model_pass" if model_ok else "model_fail") + "_" + ("witness_pass" if witness_ok else "witness_fail")] += 1
            families[r["tempo_family"]["kind"]] += 1
            flags.update({key: int(value) for key, value in r["flags"].items()})
            correspondences.update(r["correspondence"])
            status = r["reference_periods"]["constant_period_intersection_empty"]
            compatible["incompatible" if status is True else "compatible" if status is False else "unavailable"] += 1
            if not model_ok:
                failures[r["triage_group"]] += 1
                failure_families[r["tempo_family"]["kind"]] += 1
                failure_flags.update({key: int(value) for key, value in r["flags"].items()})
    return {"totals": {k: dict(v) for k, v in totals.items()}, "triage_all": dict(groups),
            "triage_complete_failures": dict(failures), "complete_context_families": dict(families),
            "failure_families": dict(failure_families), "complete_context_flags": dict(flags),
            "failure_flags": dict(failure_flags), "model_witness_cross_tab": dict(cross),
            "complete_context_correspondence": dict(correspondences),
            "constant_period_compatibility": dict(compatible), "by_genre": by_genre}


def run(attempt: Path, prepared: Path, output: Path):
    import resource
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("audit output exists; refusing retry/overwrite")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before audit")
    revision = ref.revision(ref.ROOT)
    bindings = {}
    paths = {"previous_freeze_sha256": attempt / "freeze.private.json",
             "previous_scoring_sha256": attempt / "scoring/report.json",
             "beat_this_worker_report_sha256": attempt / "beat_this/report.json",
             "prepared_manifest_sha256": prepared / "manifest.private.json",
             "pcm_result_sha256": ref.ROOT / "docs/rust/PCM-ANCHOR-TIMING-RESULT-20261022.json"}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    manifest = json.loads(paths["prepared_manifest_sha256"].read_text())
    worker = json.loads(paths["beat_this_worker_report_sha256"].read_text())
    old_report = json.loads(paths["previous_scoring_sha256"].read_text())
    ids = [r["track"] for r in manifest["tracks"]]
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
        "protocol_sha256": PROTOCOL_SHA256, "bindings": bindings, "diagnostic_run_started": False})
    started = time.monotonic()
    records, details, regressions = [], [], []
    for truth in manifest["tracks"]:
        track, total = truth["track"], truth["requested_frames"]
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        saved = json.loads((attempt / "scoring" / (track + ".json")).read_text())
        if not 0 < len(coarse["windows"]) <= config["limits"]["maximum_windows_per_source"]:
            raise ValueError("window count exceeds audit cap")
        windows, end = [], 0
        for i, old in enumerate(coarse["windows"]):
            if old["first_frame"] != end:
                raise ValueError("source partition has a gap or overlap")
            end = old["end_frame"]
            record = analyse_window(events["beat_frames"], events["downbeat_frames"], truth, old["first_frame"], end, total)
            coarse_score = ref.annotated.score_window(old, truth, ref.RATE)
            previous = saved["models"]["beat_this"]["windows"][i]
            if (record["model"] != previous["candidate"] or record["model_score"] != previous["score"]
                    or coarse_score != saved["coarse"][i]):
                raise ValueError("saved model/coarse tuples or scorer changed")
            record["coarse_score"] = coarse_score
            windows.append(record)
            if coarse_score["confirmed_joint_windows"] and not record["model_score"]["confirmed_joint_windows"]:
                regressions.append({"track": track, "window": i, "diagnostic": record})
        if end != total:
            raise ValueError("source tail omitted")
        row = {"track": track, "genre": truth["genre"], "windows": windows}
        path = output / (track + ".json")
        ref.history.write_new(path, row)
        details.append({"track": track, "sha256": ref.history.sha256_file(path)})
        records.append(row)
        if time.monotonic() - started > config["limits"]["max_audit_seconds"]:
            raise ValueError("registered audit time cap exceeded")
    result = aggregate(records)
    if (result["totals"]["old_coarse"] != old_report["totals"]["coarse"]
            or result["totals"]["old_ols"] != old_report["totals"]["beat_this"]
            or result["totals"]["old_ols"]["windows"] != 600
            or result["totals"]["old_ols"]["reference_complete_windows"] != 401
            or len(regressions) != 2 or sum(result["triage_complete_failures"].values()) != 214):
        raise ValueError("fixed cohort, regressions or old aggregates differ")
    ref.recheck(bindings)
    for row in details:
        ref.bind(output / (row["track"] + ".json"), row["sha256"], {})
    if ref.revision(ref.ROOT) != revision or subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("source changed during audit")
    elapsed, rss = time.monotonic() - started, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if elapsed > config["limits"]["max_audit_seconds"] or rss > config["limits"]["max_host_rss_kib"]:
        raise ValueError("registered audit resource cap exceeded")
    result.update(format=config["format"], source_revision=revision, protocol_sha256=PROTOCOL_SHA256,
        formal_acceptance=False, production_promotion=False, new_candidate=False, read_audio=False,
        inference_run=False, transfer_opened=False, confidence_calibrated=False, physical_p4=False,
        integrity_passed=True, resources={"audit_wall_seconds": elapsed, "rss_peak_kib": rss},
        details=details, bindings=bindings, original_regressions=regressions)
    ref.history.write_new(output / "report.private.json", result)
    return {k: v for k, v in result.items() if k not in ("details", "bindings", "original_regressions")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("attempt", "prepared", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.attempt, args.prepared, args.output), sort_keys=True))


if __name__ == "__main__":
    main()
