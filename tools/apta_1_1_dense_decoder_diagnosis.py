#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Post-hoc arithmetic audit of frozen DD-05 outputs; never constructs candidates."""
import argparse
from collections import Counter
import json
from pathlib import Path
import time

import apta_1_1_event_representation as representation
import apta_dense_runner_support as io

ROOT = Path(__file__).resolve().parents[1]
RESULT = ROOT / "docs/rust/DENSE-DECODER-DD05-20261023.json"
RESULT_SHA256 = "40b3ac60c3e19d7bdb747be78c5f345d7ec96f2c7c8938c2f55dc6925ce37b59"
Q = 1 << 32


def interval_audit(frames, rows, truth):
    """Same original event/reference match; decompose delta=960*delta_row+delta_offset.

    Row arithmetic is not an alternative event sequence or window score. No new
    coordinates are returned. Bins/thresholds are fixed before the private audit.
    """
    if (len(frames) != len(rows) or any(type(x) is not int for x in frames + rows)
            or any(a >= b for a, b in zip(frames, frames[1:]))
            or any(a >= b for a, b in zip(rows, rows[1:]))):
        raise ValueError("invalid saved event/row arrays")
    counts, errors, offsets, changes = Counter(), [], [], []
    beats = truth["beat_frames"]
    for index, (frame, row) in enumerate(zip(frames, rows)):
        offset = frame - row * 960
        if abs(offset) > 480:
            raise ValueError("saved refinement exceeds registered bound")
        offsets.append(abs(offset))
        if not beats[0] <= frame <= beats[-1]:
            counts["outside_reference_points"] += 1
            continue
        nearest = min(beats, key=lambda b: abs(b-frame))
        target = representation.ref.annotated.canonical._local_period(beats, nearest)
        counts["scored_points"] += 1
        if index + 1 == len(frames):
            counts["unknown_successor_points"] += 1
            continue
        actual = frames[index+1] - frame
        lattice = (rows[index+1] - row) * 960
        adjustment = actual - lattice
        counts["known_intervals"] += 1
        row_good = abs(lattice-target) <= target * .01
        actual_good = abs(actual-target) <= target * .01
        counts["row_"+("pass" if row_good else "fail")+"_actual_"+("pass" if actual_good else "fail")] += 1
        if not actual_good:
            counts["actual_period_failures"] += 1
        else:
            counts["actual_period_correct"] += 1
        if abs(adjustment) > target * .01:
            counts["refinement_difference_exceeds_one_percent"] += 1
        # Theoretical local lattice coverage only; no path/coordinate selection.
        best_distance = min(abs(d*960-target) for d in range(10, 76))
        if best_distance > target * .01:
            counts["local_target_without_one_percent_lattice_multiple"] += 1
        relative = abs(actual-target) / target
        errors.append(relative * 100)
        changes.append(abs(adjustment))
        band = "le_1pct" if relative <= .01 else "le_2pct" if relative <= .02 else "le_5pct" if relative <= .05 else "gt_5pct"
        counts[band] += 1
    return counts, errors, offsets, changes


def window_reasons(points, truth, first, end):
    """Explain the unchanged score_points boolean; no alternative scoring rule."""
    score = representation.score_points(points, truth, first, end)
    reasons = []
    if not score["reference_complete_windows"]:
        reasons.append("incomplete_reference")
    if not points:
        reasons.append("empty")
    if score["scored_points"] != len(points):
        reasons.append("outside_reference")
    if score["unknown_successor_points"]:
        reasons.append("unknown_successor")
    if score["phase_correct_points"] != score["scored_points"]:
        reasons.append("phase_error")
    known = score["scored_points"] - score["unknown_successor_points"]
    if score["period_correct_points"] != known:
        reasons.append("known_period_error")
    if bool(reasons) == bool(score["joint_windows"]):
        raise ValueError("diagnostic reasons disagree with original scorer")
    return score, "+".join(reasons) or "pass"


def distribution(values):
    if not values:
        return {"count": 0, "p50": None, "p90": None, "p99": None, "max": None}
    ordered = sorted(values)
    # Empirical nearest-rank percentiles, no interpolation or fitted cutoff.
    return {"count": len(values), "p50": ordered[(len(values)*50+99)//100-1],
            "p90": ordered[(len(values)*90+99)//100-1],
            "p99": ordered[(len(values)*99+99)//100-1], "max": ordered[-1]}


def annotation_consistency(truth, total):
    """Reference interval vs its five-gap median target, not an ideal candidate score."""
    beats = truth["beat_frames"]
    count = Counter()
    for left, right in zip(beats, beats[1:]):
        if 0 <= left < right < total:
            local = representation.ref.annotated.canonical._local_period(beats, left)
            count["in_source_reference_pairs"] += 1
            count["outside_one_percent_local_median"] += int(abs(right-left-local) > local*.01)
    return count


def diagnose(candidates, evaluation, prepared, output):
    started = time.monotonic()
    bindings = {}
    io.external(output, ROOT)
    io.disjoint_output(output, (candidates, evaluation, prepared))
    revision = io.source_snapshot(ROOT, bindings)
    public = io.read_bound(RESULT, RESULT_SHA256, bindings)
    if public["status"] != "REJECTED" or public["integrity_passed"] is not True:
        raise ValueError("only the fixed rejected attempt may be diagnosed")
    values = {}
    locations = {"construct": (candidates, "candidates-frozen.private.json"),
                 "verify": (Path(str(candidates)+"-verification"), "report.private.json"),
                 "evaluate": (evaluation, "report.private.json")}
    for phase, (directory, name) in locations.items():
        identity = public["phase_identity"][phase]
        values[phase] = io.read_bound(directory/name, identity["completion_sha256"], bindings)
        envelope = io.read_bound(Path(str(directory)+"-supervision")/"report.json",
                                 identity["supervisor_report_sha256"], bindings)
        if (envelope["status"] != "PASS" or envelope["completion_sha256"] != identity["completion_sha256"]
                or values[phase]["source_revision"] != public["source_revision"]
                or values[phase]["protocol_sha256"] != public["protocol_sha256"]):
            raise ValueError("original phase identity differs")
    frozen, report = values["construct"], values["evaluate"]
    manifest = io.read_bound(prepared/"manifest.private.json", public["input_identity"]["prepared_manifest_sha256"], bindings)
    if (manifest["holdout_performances_opened"] is not False
            or values["verify"]["sources_verified"] != 100
            or report["integrity_passed"] is not True or report["status"] != "REJECTED"):
        raise ValueError("original verification/reference boundary differs")
    # The old report binds all original tracked sources, including the unchanged
    # scorer and local-period definition. Later documentation commits are allowed.
    for name in ("apta_1_1_event_representation.py", "apta_1_1_asap_meter_validation.py",
                 "apta_1_1_neural_reference.py", "apta_1_1_learned_piecewise.py"):
        path = ROOT/"tools"/name
        io.bind(path, report["bindings"][str(path.resolve())], bindings)
    ids = [row["track"] for row in frozen["candidates"]]
    if (len(ids) != 100 or len(set(ids)) != 100
            or ids != [row["track"] for row in report["details"]]
            or ids != [row["track"] for row in manifest["tracks"]]):
        raise ValueError("original cohort order/count differs")
    io.runtime_snapshot(bindings)
    counts, annotations, reasons, break_reasons, projection_reasons = [Counter() for _ in range(5)]
    row_changes, successor_kind = Counter(), Counter()
    errors, offsets, differences = [], [], []
    totals, old_totals = Counter(), Counter()
    private = []
    for entry, scored, truth in zip(frozen["candidates"], report["details"], manifest["tracks"]):
        track = entry["track"]
        if not track or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in track):
            raise ValueError("unsafe saved identity")
        candidate = io.read_bound(candidates/"candidates"/(track+".json"), entry["sha256"], bindings)
        saved = io.read_bound(evaluation/(track+".json"), scored["sha256"], bindings)["scores"]
        frames, rows = candidate["events"]["beat_frames"], candidate["events"]["beat_rows"]
        counter, relative, absolute_offsets, absolute_differences = interval_audit(frames, rows, truth)
        counts.update(counter)
        errors.extend(relative)
        offsets.extend(absolute_offsets)
        differences.extend(absolute_differences)
        annotations.update(annotation_consistency(truth, candidate["total_frames"]))
        delta_rows = [b-a for a,b in zip(rows,rows[1:])]
        for a,b in zip(delta_rows,delta_rows[1:]):
            row_changes[str(abs(b-a))] += 1
        window_details = []
        for i, window in enumerate(candidate["windows"]):
            indices = window["beat_ordinals"]
            points = [(frames[j]*Q, (frames[j+1]-frames[j])*Q if j+1 < len(frames) else None) for j in indices]
            actual, reason = window_reasons(points, truth, window["first_frame"], window["end_frame"])
            if actual != saved["candidate_actual"][i]:
                raise ValueError("saved actual score does not reproduce")
            totals.update(actual)
            old = saved["ols_actual"][i]
            old_totals.update(old)
            reasons[reason] += 1
            if old["joint_windows"] and not actual["joint_windows"]:
                break_reasons[reason] += 1
            if saved["candidate_nominal"][i]["joint_windows"] and not actual["joint_windows"]:
                projection_reasons[reason] += 1
            if indices:
                last = indices[-1]
                successor_kind["final_source_unknown" if last+1 == len(frames) else
                               "known_cross_window_successor"] += 1
            window_details.append({"window": i, "reason": reason})
        private.append({"track": track, "counts": dict(counter), "window_reasons": window_details})
    if (dict(totals) != report["totals"]["candidate_actual"]
            or dict(old_totals) != report["totals"]["ols_actual"]
            or sum(break_reasons.values()) != report["paired"]["actual"]["joint_breaks"]
            or counts["actual_period_correct"] != totals["period_correct_points"]
            or counts["unknown_successor_points"] != totals["unknown_successor_points"]):
        raise ValueError("diagnostic aggregates differ from frozen evaluation")
    io.runtime_recheck(bindings)
    io.recheck(bindings)
    io.check_source(ROOT, revision)
    result = {"format": "libapta-dense-decoder-posthoc-diagnosis-1", "source_revision": revision,
              "original_measurement_revision": public["source_revision"], "original_result_sha256": RESULT_SHA256,
              "protocol_sha256": public["protocol_sha256"], "status": "AUDIT_COMPLETE_REJECTION_UNCHANGED",
              "sources": 100, "windows": sum(reasons.values()), "scorer_replay_exact": True,
              "interval_counts": dict(counts), "actual_window_reasons": dict(reasons),
              "original_actual_joint_break_reasons": dict(break_reasons),
              "nominal_joint_correct_actual_joint_failed_reasons": dict(projection_reasons),
              "reference_annotation_consistency": dict(annotations),
              "absolute_interval_error_percent": distribution(errors),
              "absolute_saved_refinement_frames": distribution(offsets),
              "absolute_adjacent_refinement_difference_frames": distribution(differences),
              "absolute_row_interval_change_counts": dict(row_changes),
              "last_event_per_nonempty_window_successor_kind": dict(successor_kind),
              "candidate_redecoded": False, "alternative_candidate_scored": False,
              "raw_logits_or_audio_read": False, "new_inference": False, "thresholds_changed": False,
              "posthoc_on_observed_development_labels": True, "original_verdict": "REJECTED",
              "wall_seconds": time.monotonic()-started}
    output.mkdir(parents=True)
    io.write_new(output/"details.private.json", {"sources": private, "bindings": bindings})
    io.write_new(output/"aggregate.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("candidates", "evaluation", "prepared", "output"):
        parser.add_argument("--"+name, type=Path, required=True)
    args = parser.parse_args()
    try:
        value = diagnose(args.candidates, args.evaluation, args.prepared, args.output)
        print(json.dumps(value, sort_keys=True))
        return 0
    except (ValueError, KeyError, TypeError, OSError) as exc:
        import sys
        print(json.dumps({"status": "INVALID", "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    import sys
    sys.exit(main())
