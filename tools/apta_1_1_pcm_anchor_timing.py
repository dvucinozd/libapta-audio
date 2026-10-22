#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Opt-in fixed-anchor PCM timing experiment; no stage-A model inference."""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import sys
import time
import wave

import apta_1_1_multibeat_timing as previous

representation = previous.representation
ref = previous.ref
PROTOCOL_PATH = ref.ROOT / "docs/rust/PCM-ANCHOR-TIMING-PROTOCOL.json"
PROTOCOL_SHA256 = "0668dc34481a8c6021ea972866ed02d66f763694c8dc2607475143f5febc72de"


def protocol() -> dict:
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("PCM-anchor protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def energy_prefix(samples):
    import numpy as np
    config = protocol()["algorithm"]
    pcm = np.asarray(samples)
    if (pcm.dtype != np.dtype("<i2") or pcm.ndim != 2 or pcm.shape[1] != 2
            or not 0 < len(pcm) <= config["maximum_source_frames"]):
        raise ValueError("bounded stereo PCM16 required")
    energy = np.sum(np.square(pcm, dtype=np.int64), axis=1, dtype=np.int64)
    prefix = np.empty(len(pcm) + 1, dtype=np.int64)
    prefix[0] = 0
    np.cumsum(energy, dtype=np.int64, out=prefix[1:])
    return prefix


def read_prefix(path: Path, requested: int, source_total: int):
    import numpy as np
    if type(requested) is not int or not 0 < requested <= min(source_total, protocol()["algorithm"]["maximum_source_frames"]):
        raise ValueError("invalid requested PCM geometry")
    with wave.open(str(path), "rb") as source:
        if (source.getframerate() != 48000 or source.getnchannels() != 2
                or source.getsampwidth() != 2 or source.getcomptype() != "NONE"
                or source.getnframes() != source_total):
            raise ValueError("canonical PCM format differs")
        data = source.readframes(requested)
    if len(data) != requested * 4:
        raise ValueError("truncated canonical PCM")
    return energy_prefix(np.frombuffer(data, dtype="<i2").reshape(requested, 2))


def validate_geometry(beats, downbeats, windows, total, gaps):
    config = protocol()["algorithm"]
    ref.valid_events(beats, total)
    ref.valid_events(downbeats, total)
    if len(beats) > config["maximum_events"] or not set(downbeats) <= set(beats):
        raise ValueError("invalid event capacity/downbeat association")
    if not 0 < len(windows) <= config["maximum_windows"]:
        raise ValueError("invalid window capacity")
    end = 0
    for window in windows:
        first, right = window["first_frame"], window["end_frame"]
        if type(first) is not int or type(right) is not int or first != end or not first < right <= total:
            raise ValueError("fixed complete source partition required")
        end = right
    if end != total:
        raise ValueError("source tail missing from partition")
    end = 0
    for first, right in gaps:
        if type(first) is not int or type(right) is not int or not end <= first < right <= total:
            raise ValueError("invalid declared PCM gaps")
        end = right


def overlaps(first, end, gaps):
    return any(left < end and first < right for left, right in gaps)


def refine(prefix, beats: list[int], downbeats: list[int], windows: list[dict], gaps: tuple = ()) -> dict:
    """Candidate sees PCM/events/geometry only; every event retains its identity."""
    import numpy as np
    config = protocol()["algorithm"]
    values = np.asarray(prefix)
    total = len(values) - 1 if values.ndim == 1 else -1
    if (values.dtype != np.dtype("int64") or not 0 < total <= config["maximum_source_frames"]
            or values[0] != 0 or np.any(values[1:] < values[:-1])
            or int(values[-1]) > total * 2 * 32768 ** 2):
        raise ValueError("invalid bounded stereo-energy prefix")
    validate_geometry(beats, downbeats, windows, total, gaps)
    radius, step, width = (config[key] for key in ("search_radius_frames", "search_step_frames", "energy_half_window_frames"))
    minimum_energy = 2 * width * 2 * config["minimum_rms_pcm16"] ** 2
    records, refined, counts = [], [], Counter()
    window_index = 0
    unavailable = {i for i, w in enumerate(windows) if overlaps(w["first_frame"], w["end_frame"], gaps)}
    for ordinal, anchor in enumerate(beats):
        while anchor >= windows[window_index]["end_frame"]:
            window_index += 1
        window = windows[window_index]
        left, right = anchor - radius - width, anchor + radius + width
        chosen, state, detail = anchor, "unrefined_source_boundary", {}
        if overlaps(max(0, left), min(total, right), gaps):
            state = "unavailable_pcm_gap"
            unavailable.add(window_index)
        elif 0 <= left and right <= total:
            positions = list(range(anchor - radius, anchor + radius + 1, step))
            energies = [(int(values[p]) - int(values[p - width]),
                         int(values[p + width]) - int(values[p])) for p in positions]
            scores = [after - before for before, after in energies]
            winner = max(range(len(positions)), key=lambda i: scores[i])
            position, contrast = positions[winner], scores[winner]
            combined = sum(energies[winner])
            competitor = max((max(0, score) for p, score in zip(positions, scores)
                              if abs(p - position) > config["peak_exclusion_radius_frames"]), default=0)
            detail = {"peak_frame": position, "contrast": contrast, "combined_energy": combined,
                      "competitor": competitor}
            if contrast <= 0:
                state = "unrefined_no_rise"
            elif combined < minimum_energy:
                state = "unrefined_low_energy"
            elif contrast * 4 < combined:
                state = "unrefined_low_contrast"
            elif contrast < config["minimum_peak_dominance"] * competitor:
                state = "unrefined_ambiguous"
            elif winner in (0, len(positions) - 1):
                state = "unrefined_search_edge"
            elif (not window["first_frame"] <= position < window["end_frame"]
                    or ordinal > 0 and 2 * position <= beats[ordinal - 1] + anchor
                    or ordinal + 1 < len(beats) and 2 * position >= anchor + beats[ordinal + 1]):
                state = "unrefined_ownership"
            else:
                chosen, state = position, "supported"
        refined.append(chosen)
        counts[state] += 1
        counts["moved"] += int(chosen != anchor)
        records.append({"ordinal": ordinal, "original_frame": anchor, "refined_frame": chosen,
                        "state": state, **detail})
    validate_geometry(refined, [refined[beats.index(p)] for p in downbeats], windows, total, gaps)
    if any(not w["first_frame"] <= p < w["end_frame"]
           for w in windows for p, original in zip(refined, beats)
           if w["first_frame"] <= original < w["end_frame"]):
        raise ValueError("refinement changed window membership")
    return {"beat_frames": refined, "downbeat_frames": [refined[beats.index(p)] for p in downbeats],
            "records": records, "counts": dict(counts), "unavailable_windows": sorted(unavailable)}


def project_windows(refined: dict, windows: list[dict]) -> list[dict]:
    result = []
    for i, window in enumerate(windows):
        first, end = window["first_frame"], window["end_frame"]
        if i in refined["unavailable_windows"]:
            result.append({"first_frame": first, "end_frame": end,
                           "selected": {"kind": "insufficient_pcm"}, "meter": {}})
        else:
            result.append(ref.project(refined["beat_frames"], refined["downbeat_frames"], first, end))
    return result


def point_phase_changes(beats, refined, truth):
    result = Counter()
    reference = truth["beat_frames"]
    for before, after in zip(beats, refined):
        verdicts = []
        for frame in (before, after):
            if reference[0] <= frame <= reference[-1]:
                nearest = min(reference, key=lambda p: abs(p - frame))
                period = ref.annotated.canonical._local_period(reference, nearest)
                verdicts.append(abs(frame - nearest) <= .10 * period)
        result["unknown"] += int(len(verdicts) != 2)
        if len(verdicts) == 2:
            result["scored"] += 1
            result["fixes"] += int(not verdicts[0] and verdicts[1])
            result["breaks"] += int(verdicts[0] and not verdicts[1])
    return dict(result)


def gates(totals, changes, new_coarse_breaks):
    config = protocol()["evaluation"]
    result = {"joint_improves": totals["candidate"]["confirmed_joint_windows"] >= config["minimum_joint_windows"],
        "complete_sequence_improves": totals["common_candidate"]["complete_sequence_windows"] >= config["minimum_complete_sequence_windows"],
        "no_new_coarse_joint_breaks": new_coarse_breaks == 0}
    for field in ("period", "phase", "joint", "meter", "downbeat"):
        result["zero_ols_" + field + "_breaks"] = changes["ols"][field + "_breaks"] == 0
    return result


def bind_inputs(attempt: Path, prepared: Path):
    import numpy as np
    config = protocol()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before PCM experiment")
    bindings = {}
    paths = {"previous_freeze_sha256": attempt / "freeze.private.json",
             "previous_scoring_sha256": attempt / "scoring/report.json",
             "beat_this_worker_report_sha256": attempt / "beat_this/report.json",
             "prepared_manifest_sha256": prepared / "manifest.private.json",
             "rejected_multibeat_result_sha256": ref.ROOT / "docs/rust/MULTIBEAT-TIMING-RESULT-20261022.json"}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    numpy_root = Path(np.__file__).resolve().parent
    for path in (Path(sys.executable).resolve(), Path(wave.__file__).resolve()):
        ref.bind(path, ref.history.sha256_file(path), bindings)
    for pattern in ("*.py", "*.so"):
        for path in numpy_root.rglob(pattern):
            ref.bind(path, ref.history.sha256_file(path), bindings)
    manifest = json.loads(paths["prepared_manifest_sha256"].read_text())
    worker = json.loads(paths["beat_this_worker_report_sha256"].read_text())
    old_report = json.loads(paths["previous_scoring_sha256"].read_text())
    ids = [row["track"] for row in manifest["tracks"]]
    if (len(ids) != 100 or len(set(ids)) != 100 or ids != [r["track"] for r in worker["outputs"]]
            or ids != [r["track"] for r in old_report["details"]]
            or manifest["holdout_performances_opened"] is not False):
        raise ValueError("fixed development cohort/order required")
    for row in manifest["tracks"]:
        path = prepared / "audio" / row["file"]
        if not path.resolve().is_relative_to((prepared / "audio").resolve()):
            raise ValueError("audio escapes prepared input root")
        ref.bind(path, row["sha256"], bindings)
    for row in worker["outputs"]:
        ref.bind(attempt / "beat_this" / (row["track"] + ".json"), row["events_sha256"], bindings)
    for row in old_report["details"]:
        ref.bind(attempt / "scoring" / (row["track"] + ".json"), row["sha256"], bindings)
        path = attempt / "scoring" / (row["track"] + "-coarse.json")
        ref.bind(path, ref.history.sha256_file(path), bindings)
    return manifest, old_report, bindings, np.__version__


def run(attempt: Path, prepared: Path, output: Path):
    import resource
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("output exists; refusing retry/overwrite")
    manifest, old_report, bindings, numpy_version = bind_inputs(attempt, prepared)
    revision = ref.revision(ref.ROOT)
    output.mkdir(parents=True)
    ref.history.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": PROTOCOL_SHA256, "bindings": bindings, "numpy_version": numpy_version,
        "candidate_scoring_started": False, "formal_acceptance": False})
    started = time.monotonic()
    totals = {name: Counter() for name in ("old_coarse", "old_ols", "candidate", "common_ols", "common_candidate", "raw_events", "refined_events")}
    changes = {name: Counter() for name in ("ols", "coarse")}
    counts, kinds, point_changes = Counter(), Counter(), Counter()
    details, original_breaks, new_coarse_breaks = [], [], []
    movements = []
    for truth in manifest["tracks"]:
        track = truth["track"]
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        beats, downbeats = events["beat_frames"], events["downbeat_frames"]
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        saved = json.loads((attempt / "scoring" / (track + ".json")).read_text())
        prefix = read_prefix(prepared / "audio" / truth["file"], truth["requested_frames"], truth["source_total_frames"])
        # Reconstruction receives no truth, baseline decision or score.
        refined = refine(prefix, beats, downbeats, coarse["windows"])
        candidates = project_windows(refined, coarse["windows"])
        del prefix
        counts.update(refined["counts"])
        point_changes.update(point_phase_changes(beats, refined["beat_frames"], truth))
        movements.extend(abs(a - b) for a, b in zip(beats, refined["beat_frames"]) if a != b)
        records = []
        for i, (old, candidate) in enumerate(zip(coarse["windows"], candidates)):
            first, end = old["first_frame"], old["end_frame"]
            control = ref.project(beats, downbeats, first, end)
            old_score = ref.annotated.score_window(control, truth, ref.RATE)
            before = ref.annotated.score_window(old, truth, ref.RATE)
            previous_record = saved["models"]["beat_this"]["windows"][i]
            if control != previous_record["candidate"] or old_score != previous_record["score"] or before != saved["coarse"][i]:
                raise ValueError("frozen OLS/coarse tuples or scores changed")
            score = ref.annotated.score_window(candidate, truth, ref.RATE)
            common = {}
            for name, points in (("common_ols", representation.points_for_control(control)),
                                 ("common_candidate", representation.points_for_control(candidate)),
                                 ("raw_events", representation.points_for_events(beats, first, end)),
                                 ("refined_events", representation.points_for_events(refined["beat_frames"], first, end))):
                common[name] = representation.score_points(points, truth, first, end)
                totals[name].update(common[name])
            for base, diagnostic in ((old_score, common["common_ols"]), (score, common["common_candidate"])):
                for field in ("period", "phase", "joint"):
                    if base["confirmed_" + field + "_windows"] != diagnostic[field + "_windows"]:
                        raise ValueError("common diagnostic/scorer disagreement")
            for name, value in (("old_coarse", before), ("old_ols", old_score), ("candidate", score)):
                totals[name].update(value)
            delta = {"ols": previous.paired(old_score, score), "coarse": previous.paired(before, score)}
            for name, value in delta.items():
                changes[name].update(value)
            if before["confirmed_joint_windows"] and not old_score["confirmed_joint_windows"]:
                original_breaks.append({"track": track, "window": i, "candidate": score})
            elif before["confirmed_joint_windows"] and not score["confirmed_joint_windows"]:
                new_coarse_breaks.append({"track": track, "window": i})
            kinds[candidate["selected"]["kind"]] += 1
            records.append({"candidate": candidate, "score": score, "old_ols": old_score,
                            "old_coarse": before, "paired": delta, **common})
        target = output / (track + ".json")
        ref.history.write_new(target, {"track": track, "refinement": refined, "windows": records})
        details.append({"track": track, "sha256": ref.history.sha256_file(target)})
        if time.monotonic() - started > config["resources"]["max_stage_a_seconds"]:
            raise ValueError("registered stage-A time cap exceeded")
    if (dict(totals["old_ols"]) != old_report["totals"]["beat_this"]
            or dict(totals["old_coarse"]) != old_report["totals"]["coarse"]
            or totals["candidate"]["windows"] != 600
            or totals["candidate"]["reference_complete_windows"] != 401 or len(original_breaks) != 2):
        raise ValueError("fixed cohort/baseline aggregate differs")
    ref.recheck(bindings)
    for row in details:
        ref.bind(output / (row["track"] + ".json"), row["sha256"], {})
    if ref.revision(ref.ROOT) != revision or subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("source changed during experiment")
    elapsed, rss = time.monotonic() - started, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if elapsed > config["resources"]["max_stage_a_seconds"] or rss > config["resources"]["max_host_rss_kib"]:
        raise ValueError("registered stage-A resource cap exceeded")
    verdicts = gates(totals, changes, len(new_coarse_breaks))
    movements.sort()
    result = {"format": config["format"], "source_revision": revision, "protocol_sha256": PROTOCOL_SHA256,
        "formal_acceptance": False, "production_promotion": False, "inference_run": False,
        "stage_a_gate_passed": all(verdicts.values()), "gates": verdicts, "transfer_opened": False,
        "confidence_calibrated": False, "physical_p4": False, "numpy_version": numpy_version,
        "totals": {k: dict(v) for k, v in totals.items()}, "paired": {k: dict(v) for k, v in changes.items()},
        "anchor_counts": dict(counts), "projection_kinds": dict(kinds), "point_phase_changes": dict(point_changes),
        "new_coarse_joint_break_count": len(new_coarse_breaks), "new_coarse_joint_breaks": new_coarse_breaks,
        "original_joint_breaks": original_breaks,
        "absolute_movement_frames": {"count": len(movements), "median": movements[len(movements) // 2] if movements else None,
                                     "max": max(movements, default=None)},
        "resources": {"run_wall_seconds": elapsed, "rss_peak_kib": rss}, "details": details, "bindings": bindings}
    ref.history.write_new(output / "report.private.json", result)
    return {k: v for k, v in result.items() if k not in ("details", "bindings", "original_joint_breaks", "new_coarse_joint_breaks")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("attempt", "prepared", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.attempt, args.prepared, args.output), sort_keys=True))


if __name__ == "__main__":
    main()
