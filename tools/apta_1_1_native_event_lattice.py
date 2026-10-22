#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Frozen host event-lattice oracle on hash-bound, spent native diagnostics.

Window diagnostics only. No FINAL export, acceptance evaluation or native
selection. Labels never enter proposal generation. Private inputs stay local.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
import re
from pathlib import Path
import time

import apta_1_1_dj_acceptance_eval as canonical
import apta_1_1_native_history_development as history

FORMAT = "libapta-native-event-lattice-development-1"


def select(events: list[list], first: int, end: int, rate: int) -> dict:
    """Pre-registered event-support selector; receives no reference labels."""
    if not events:
        return {"kind": "insufficient_events"}
    votes: Counter = Counter()
    for left, (frame, strength) in enumerate(events):
        for other, weight in events[left + 1:]:
            span = other - frame
            for step in range(1, 9):
                if span * 300 < rate * 60 * step or span * 40 > rate * 60 * step:
                    continue
                period = ((2 * span + 64 * step) // (128 * step)) * 64
                if rate * 60 <= period * 300 and period * 40 <= rate * 60:
                    votes[period] += min(strength, weight) / step
    periods = sorted(votes, key=lambda period: (-votes[period], period))[:24]
    total = sum(weight for _, weight in events)
    maximum = max(weight for _, weight in events)
    states = []
    for period in periods:
        best = None
        for anchor in sorted({first + (frame - first) % period for frame, _ in events}):
            beats = (end - 1 - anchor) // period + 1
            if beats < 8:
                continue
            strengths: dict[int, float] = {}
            mass = 0.0
            for frame, weight in events:
                ordinal = (2 * (frame - anchor) + period) // (2 * period)
                if 0 <= ordinal < beats and abs(frame - anchor - ordinal * period) * 10 <= period:
                    strengths[ordinal] = max(strengths.get(ordinal, 0.0), weight)
                    mass += weight
            coverage = sum(strengths.values()) / (beats * maximum)
            explained = mass / total
            score = 2 * coverage * explained / (coverage + explained) if coverage + explained else 0.0
            state = {"period_frames": period, "anchor_frame": anchor, "score": score,
                     "weighted_coverage": coverage, "explained_mass": explained, "beats": beats}
            if best is None or score > best["score"]:
                best = state
        if best is not None:
            states.append(best)
    if not states:
        return {"kind": "insufficient_grid_beats"}
    states.sort(key=lambda state: (-state["score"], state["period_frames"], state["anchor_frame"]))
    winner = states[0]
    period = winner["period_frames"]
    runner = 0.0
    for other in states[1:]:
        delta = abs(other["anchor_frame"] - winner["anchor_frame"]) % period
        if abs(other["period_frames"] - period) > period * 0.01 or min(delta, period - delta) > period * 0.10:
            runner = other["score"]
            break
    detail = dict(winner, runner_score=runner, period_candidates=len(periods))
    if winner["weighted_coverage"] < 0.75 or winner["explained_mass"] < 0.50:
        return dict(detail, kind="insufficient_support")
    if winner["score"] - runner <= 0.05:
        return dict(detail, kind="ambiguous")
    tempo = (rate * 60000 + period // 2) // period
    return dict(detail, kind="accepted", tempo_millibpm=tempo, confidence=49)


def metrics(window: dict, truth: dict, rate: int) -> dict:
    selected = window["selected"]
    result = {"emitted": selected["kind"] == "accepted", "period_correct": False,
              "beat_phase_correct": False, "joint_correct": False, "meter_emitted": False,
              "meter_correct": False, "downbeat_correct": False, "high_confidence_grid_error": False,
              "high_confidence_meter_error": False, "high_confidence_downbeat_error": False}
    if not result["emitted"]:
        return result
    period_q32 = (rate * 60000 << 32) // selected["tempo_millibpm"]
    period = period_q32 / (1 << 32)
    result["period_correct"] = canonical._period_correct(truth, {"beat_period_frames": period})
    position = selected["anchor_frame"] << 32
    phases = []
    reference = float(truth["beat_period_frames"])
    while position < window["end_frame"] << 32:
        delta = abs(position / (1 << 32) - int(truth["downbeat_frame"])) % reference
        phases.append(min(delta, reference - delta) <= reference * canonical.DOWNBEAT_PHASE_TOLERANCE_BEATS)
        position += period_q32
    result["beat_phase_correct"] = bool(phases) and all(phases)
    result["joint_correct"] = result["period_correct"] and result["beat_phase_correct"]
    result["high_confidence_grid_error"] = selected["confidence"] >= canonical.HIGH_CONFIDENCE and not result["joint_correct"]
    meter = window.get("meter", {})
    if meter.get("kind") == "estimated":
        result["meter_emitted"] = True
        result["meter_correct"] = canonical._meter_correct(truth, {"meter_numerator": meter["numerator"], "meter_denominator": meter["denominator"]})
        downbeat = meter["downbeat_frame"] + meter["downbeat_fraction_q32"] / (1 << 32)
        result["downbeat_correct"] = canonical._downbeat_correct(truth, {"downbeat_frame": downbeat})
        result["high_confidence_meter_error"] = meter.get("confidence", 0) >= canonical.HIGH_CONFIDENCE and not result["meter_correct"]
        result["high_confidence_downbeat_error"] = meter.get("confidence", 0) >= canonical.HIGH_CONFIDENCE and not result["downbeat_correct"]
    return result


def evaluate(coverage_path: Path, expected_coverage: str, labels_path: Path, label_manifest_path: Path,
             expected_labels: str, output: Path) -> dict:
    if history.sha256_file(coverage_path) != expected_coverage:
        raise ValueError("coverage report hash mismatch")
    manifest = canonical._load_manifest(label_manifest_path)
    manifest_hash = history.sha256_file(label_manifest_path)
    if expected_labels != manifest["labels_sha256"] or history.sha256_file(labels_path) != expected_labels:
        raise ValueError("verified labels hash mismatch")
    labels = canonical._read_labels(labels_path)
    truth = {row["track"]: row for row in labels}
    if sorted(truth) != manifest["track_ids"] or any(not math.isfinite(row["beat_period_frames"]) for row in labels):
        raise ValueError("invalid reference label cohort/period")
    coverage = json.loads(coverage_path.read_text())
    if coverage.get("format") != history.REPORT or coverage.get("formal_acceptance") is not False or coverage.get("evidence_level") != "spent-development coverage only" or coverage.get("traces_requested") is not True:
        raise ValueError("only complete explicitly spent trace diagnostics are supported")
    rows = coverage["outputs"]
    if [row["track"] for row in rows] != manifest["track_ids"]:
        raise ValueError("diagnostic cohort differs from verified labels")
    totals = {mode: Counter() for mode in ("coarse", "attack", "novelty", "event_oracle")}
    reasons: Counter = Counter()
    changes: Counter = Counter()
    details = []
    started = time.perf_counter()
    for row in rows:
        track = row["track"]
        if not re.fullmatch(r"track-[0-9a-f]{24}", track):
            raise ValueError("invalid opaque track identity")
        pair = {}
        for mode in ("coarse", "attack", "novelty", "novelty-trace"):
            path = coverage_path.parent / f"{track}-{mode}.json"
            if history.sha256_file(path) != row["outputs_sha256"][mode]:
                raise ValueError("native output hash mismatch")
            value = json.loads(path.read_text())
            if not history.HEX.fullmatch(value.get("input_sha256", "")) or "track-" + value["input_sha256"][:24] != track:
                raise ValueError("native output audio identity differs from labels")
            scope = {key: value[key] for key in ("sample_rate", "channel_count", "source_total_frames", "requested_frames")}
            scope["sha256"] = value["input_sha256"]
            if scope["sample_rate"] != 48000 or scope["channel_count"] != 2 or not isinstance(scope["source_total_frames"], int) or scope["source_total_frames"] <= 0 or not isinstance(scope["requested_frames"], int) or not 0 < scope["requested_frames"] <= 1440000:
                raise ValueError("invalid canonical diagnostic source geometry")
            history.validate_output(value, scope, "novelty" if mode == "novelty-trace" else mode)
            pair[mode] = value
        history.validate_onset_trace(pair["novelty-trace"])
        for mode in pair:
            for key in ("input_sha256", "sample_rate", "channel_count", "source_total_frames", "requested_frames", "analyzed_frames"):
                if pair[mode][key] != pair["coarse"][key]:
                    raise ValueError("paired source/scope mismatch")
            if [window["baseline"] for window in pair[mode]["windows"]] != [window["selected"] for window in pair["coarse"]["windows"]]:
                raise ValueError("retained baseline mismatch")
        stripped = dict(pair["novelty-trace"], windows=[{k: v for k, v in w.items() if k != "onset_trace"} for w in pair["novelty-trace"]["windows"]])
        if stripped != pair["novelty"]:
            raise ValueError("onset capture altered native evidence")
        track_windows = []
        for index, trace_window in enumerate(pair["novelty-trace"]["windows"]):
            first, end = trace_window["first_frame"], trace_window["end_frame"]
            proposal = select(trace_window["onset_trace"]["events"], first, end, pair["coarse"]["sample_rate"])
            reasons[proposal["kind"]] += 1
            candidates = {mode: pair[mode]["windows"][index] for mode in ("coarse", "attack", "novelty")}
            candidates["event_oracle"] = dict(first_frame=first, end_frame=end, selected=proposal, meter={})
            scores = {}
            for mode, window in candidates.items():
                scores[mode] = metrics(window, truth[track], pair[mode]["sample_rate"] if mode != "event_oracle" else pair["coarse"]["sample_rate"])
                totals[mode]["windows"] += 1
                totals[mode].update({key: int(value) for key, value in scores[mode].items()})
            before, after = scores["coarse"]["joint_correct"], scores["event_oracle"]["joint_correct"]
            changes["fixes"] += after and not before
            changes["breaks_including_abstentions"] += before and not after
            changes["changed_timing_verdicts"] += proposal.get("kind") != candidates["coarse"]["selected"]["kind"] or any(proposal.get(key) != candidates["coarse"]["selected"].get(key) for key in ("anchor_frame", "tempo_millibpm"))
            track_windows.append({"first_frame": first, "end_frame": end, "oracle": proposal, "metrics": scores})
        details.append({"track": track, "windows": track_windows})
    improved = totals["event_oracle"]["joint_correct"] > totals["coarse"]["joint_correct"]
    safe = changes["breaks_including_abstentions"] == 0 and totals["event_oracle"]["high_confidence_grid_error"] <= totals["coarse"]["high_confidence_grid_error"]
    rates = {}
    for mode, counts in totals.items():
        emitted, windows = counts["emitted"], counts["windows"]
        wrong = emitted - counts["joint_correct"]
        rates[mode] = {"false_lattice_windows": wrong,
                       "joint_accuracy_all_windows": counts["joint_correct"] / windows,
                       "conditional_false_lattice_rate": wrong / emitted if emitted else None}
    report = {"format": FORMAT, "formal_acceptance": False, "evidence_level": "spent-development window diagnostics",
              "status": "requires_independent_transfer" if improved and safe else "rejected_before_native_implementation",
              "label_sha256": expected_labels, "label_manifest_sha256": manifest_hash,
              "coverage_report_sha256": expected_coverage, "source_revision": coverage["source_revision"],
              "candidate_inventory_sha256": coverage["candidate_inventory_sha256"], "analyzer_sha256": coverage["analyzer_sha256"],
              "reference_model": "verified constant beat period and bar phase; no dense per-beat annotations",
              "elapsed_host_seconds": time.perf_counter() - started,
              "totals": {key: dict(value) for key, value in totals.items()}, "rates": rates, "oracle_reasons": dict(reasons),
              "paired_changes": dict(changes), "tracks": details}
    if history.sha256_file(coverage_path) != expected_coverage or history.sha256_file(labels_path) != expected_labels or history.sha256_file(label_manifest_path) != manifest_hash:
        raise ValueError("frozen inputs changed during scoring")
    history.write_new(output, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("coverage-report", "labels", "label-manifest", "output"):
        parser.add_argument("--" + field, type=Path, required=True)
    parser.add_argument("--expected-coverage-sha256", required=True)
    parser.add_argument("--expected-labels-sha256", required=True)
    args = parser.parse_args()
    try:
        report = evaluate(args.coverage_report, args.expected_coverage_sha256, args.labels, args.label_manifest, args.expected_labels_sha256, args.output)
    except (ValueError, OSError, KeyError, canonical.EvaluationError) as exc:
        parser.exit(1, f"event-lattice diagnostic failed: {exc}\n")
    print(json.dumps({"status": report["status"], "totals": report["totals"], "changes": report["paired_changes"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
