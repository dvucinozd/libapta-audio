#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only saved-event audit; no inference, tuning or product promotion."""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import time

import apta_1_1_neural_reference as ref

PROTOCOL_PATH = ref.ROOT / "docs/rust/EVENT-REPRESENTATION-PROTOCOL.json"
PROTOCOL_SHA256 = "86cc8fec3ff7488c613b555248b6bb7a70bb8eeeca52a8e22f01bf9e4cc21b8c"
Q = 1 << 32


def protocol():
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("representation protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def exact_segments(beats: list[int]) -> list[dict]:
    """Compress equal integer intervals exactly, without duplicated boundaries."""
    if len(beats) < 2:
        return []
    ref.valid_events(beats, beats[-1] + 1)
    segments, start = [], 0
    while start + 1 < len(beats):
        period, end = beats[start + 1] - beats[start], start + 1
        while end + 1 < len(beats) and beats[end + 1] - beats[end] == period:
            end += 1
        final = end == len(beats) - 1
        segments.append({"first_frame": beats[start], "end_frame": beats[end] + int(final),
            "anchor_frame": beats[start], "anchor_ordinal": start, "period_frames": period,
            "beat_count": end - start + int(final),
            "tempo_millibpm": ref.rounded(ref.Fraction(ref.RATE * 60000, period))})
        start = end
    return segments


def expand_segments(segments: list[dict]) -> list[int]:
    return [s["anchor_frame"] + i * s["period_frames"] for s in segments for i in range(s["beat_count"])]


def points_for_control(window: dict) -> list[tuple[int, int]]:
    selected = window["selected"]
    if selected["kind"] != "accepted":
        return []
    period = (ref.RATE * 60000 << 32) // selected["tempo_millibpm"]
    point, result = selected["anchor_frame"] << 32, []
    while point < window["end_frame"] << 32:
        result.append((point, period))
        point += period
    return result


def points_for_events(beats: list[int], first: int, end: int) -> list[tuple[int, int | None]]:
    return [(p << 32, (beats[i + 1] - p) << 32 if i + 1 < len(beats) else None)
            for i, p in enumerate(beats) if first <= p < end]


def score_points(points: list, truth: dict, first: int, end: int) -> dict:
    """Common diagnostics, not a replacement for frozen score_window."""
    beats = truth["beat_frames"]
    context = beats[0] <= first and end - 1 <= beats[-1]
    phase, period, matches = [], [], []
    unknown_period = 0
    for position, interval in points:
        frame = position / Q
        if beats[0] <= frame <= beats[-1]:
            nearest = min(beats, key=lambda b: abs(b - frame))
            local = ref.annotated.canonical._local_period(beats, nearest)
            good = abs(frame - nearest) <= local * .10
            phase.append(good)
            matches.append(nearest if good else None)
            if interval is None:
                unknown_period += 1
            else:
                period.append(abs(interval / Q - local) <= local * .01)
    complete = context and bool(points) and len(phase) == len(points)
    phase_ok = complete and all(phase)
    period_ok = complete and len(period) == len(points) and all(period)
    expected = {b for b in beats if first <= b < end}
    one_to_one = phase_ok and len(matches) == len(set(matches)) and set(matches) == expected
    return {"windows": 1, "reference_complete_windows": int(context), "points": len(points),
            "scored_points": len(phase), "phase_correct_points": sum(phase),
            "period_correct_points": sum(period), "unknown_successor_points": unknown_period,
            "phase_windows": int(phase_ok), "period_windows": int(period_ok),
            "joint_windows": int(phase_ok and period_ok),
            "one_to_one_phase_windows": int(one_to_one),
            "complete_sequence_windows": int(one_to_one and period_ok)}


def meter_on_events(beats: list[int], downbeats: list[int], first: int, end: int) -> dict | None:
    index = {p: i for i, p in enumerate(beats)}
    db = [p for p in downbeats if first <= p < end]
    for a, b, c in zip(db, db[1:], db[2:]):
        numerator = index[b] - index[a]
        if numerator in (3, 4) and index[c] - index[b] == numerator:
            return {"first_frame": first, "end_frame": end, "downbeat_frame": a,
                    "downbeat_ordinal": index[a], "numerator": numerator}
    return None


def native_request(beats: list[int], downbeats: list[int], meters: list[dict], total: int, tempo: int) -> str:
    index = {p: i for i, p in enumerate(beats)}
    fields = ["EVENT_REPR_V1", str(ref.RATE), str(total), str(tempo), str(len(beats)), str(len(downbeats)), str(len(meters))]
    fields += list(map(str, beats))
    for p in downbeats:
        fields += [str(index[p]), str(p)]
    for m in meters:
        fields += [str(m[k]) for k in ("first_frame", "end_frame", "downbeat_frame", "downbeat_ordinal", "numerator")]
    return " ".join(fields) + "\n"


def run(attempt: Path, prepared: Path, analyzer: Path, output: Path, consumer_root: Path, consumer_report: Path):
    import resource
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("audit output exists; refusing retry/overwrite")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before auditing saved events")
    bindings = {}
    consumer = config["consumer_snapshot"]
    if ref.revision(consumer_root) != consumer["revision"]:
        raise ValueError("consumer source revision differs")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=consumer_root, text=True):
        raise ValueError("consumer source checkout must be unchanged")
    adapter = consumer_root / "firmware-rust/crates/pajoniiir-apta-adapter"
    ref.bind(adapter / "src/lib.rs", consumer["adapter_lib_rs_sha256"], bindings)
    ref.bind(adapter / "Cargo.toml", consumer["adapter_cargo_toml_sha256"], bindings)
    for crate in ("pajoniiir-apta-adapter", "pajoniiir-track-analysis", "pajoniiir-waveform"):
        crate_root = consumer_root / "firmware-rust/crates" / crate
        for path in list((crate_root / "src").rglob("*.rs")) + [crate_root / "Cargo.toml"]:
            ref.bind(path, ref.history.sha256_file(path), bindings)
    ref.bind(consumer_report, ref.history.sha256_file(consumer_report), bindings)
    policy = json.loads(consumer_report.read_text())
    if policy.get("checks_passed") != 8 or policy.get("synthetic_only") is not True or policy.get("production_activation") is not False:
        raise ValueError("actual consumer synthetic policy prerequisites failed")
    paths = {"previous_freeze_sha256": attempt / "freeze.private.json",
             "previous_scoring_sha256": attempt / "scoring/report.json",
             "beat_this_worker_report_sha256": attempt / "beat_this/report.json",
             "prepared_manifest_sha256": prepared / "manifest.private.json"}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    ref.bind(analyzer, ref.history.sha256_file(analyzer), bindings)
    manifest = json.loads(paths["prepared_manifest_sha256"].read_text())
    old_report = json.loads(paths["previous_scoring_sha256"].read_text())
    worker = json.loads(paths["beat_this_worker_report_sha256"].read_text())
    ids = [row["track"] for row in manifest["tracks"]]
    if (len(ids) != 100 or len(set(ids)) != 100 or ids != [r["track"] for r in worker["outputs"]]
        or ids != [r["track"] for r in old_report["details"]]
        or manifest["holdout_performances_opened"] is not False):
        raise ValueError("registered complete cohort required")
    for row in worker["outputs"]:
        ref.bind(attempt / "beat_this" / (row["track"] + ".json"), row["events_sha256"], bindings)
    for row in old_report["details"]:
        ref.bind(attempt / "scoring" / (row["track"] + ".json"), row["sha256"], bindings)
        coarse = attempt / "scoring" / (row["track"] + "-coarse.json")
        ref.bind(coarse, ref.history.sha256_file(coarse), bindings)
    output.mkdir(parents=True)
    totals = {name: Counter() for name in ("old_coarse", "old_ols", "common_ols", "common_explicit")}
    changes, counts, details = Counter(), Counter(), []
    old_breaks, displacement, segment_counts = [], [], []
    started = time.monotonic()
    for truth in manifest["tracks"]:
        track = truth["track"]
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        beats, db = events["beat_frames"], events["downbeat_frames"]
        ref.valid_events(beats, truth["requested_frames"])
        ref.valid_events(db, truth["requested_frames"])
        saved = json.loads((attempt / "scoring" / (track + ".json")).read_text())
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        segments = exact_segments(beats)
        segment_counts.append(len(segments))
        if len(beats) >= 2 and expand_segments(segments) != beats:
            raise ValueError("exact segment compression changed events")
        meters, windows = [], []
        for i, old in enumerate(coarse["windows"]):
            first, end = old["first_frame"], old["end_frame"]
            control = ref.project(beats, db, first, end)
            previous = saved["models"]["beat_this"]["windows"][i]
            before = ref.annotated.score_window(old, truth, ref.RATE)
            old_score = ref.annotated.score_window(control, truth, ref.RATE)
            if control != previous["candidate"] or old_score != previous["score"] or before != saved["coarse"][i]:
                raise ValueError("frozen control tuples/scorer no longer reproduce")
            points = points_for_control(control)
            fitted = score_points(points, truth, first, end)
            raw = score_points(points_for_events(beats, first, end), truth, first, end)
            for key in ("period", "phase", "joint"):
                if fitted[key + "_windows"] != old_score["confirmed_" + key + "_windows"]:
                    raise ValueError("common control does not reproduce old definitions")
                changes[key + "_fixes"] += int(not fitted[key + "_windows"] and bool(raw[key + "_windows"]))
                changes[key + "_breaks"] += int(bool(fitted[key + "_windows"]) and not raw[key + "_windows"])
            for name, result in (("old_coarse", before), ("old_ols", old_score), ("common_ols", fitted), ("common_explicit", raw)):
                totals[name].update(result)
            if before["confirmed_joint_windows"] and not old_score["confirmed_joint_windows"]:
                old_breaks.append({"track": track, "window": i, "raw": raw})
            m = meter_on_events(beats, db, first, end)
            if m:
                meters.append(m)
                counts["meter_records"] += 1
                counts["meter_correct"] += int(m["numerator"] == truth["meter_numerator"])
                if truth["beat_frames"][0] <= m["downbeat_frame"] <= truth["beat_frames"][-1]:
                    nearest = min(truth["downbeat_frames"], key=lambda p: abs(p - m["downbeat_frame"]))
                    local = ref.annotated.canonical._local_period(truth["beat_frames"], nearest)
                    counts["downbeat_scored"] += 1
                    counts["downbeat_correct"] += int(abs(nearest - m["downbeat_frame"]) <= local * .10)
            for position, _ in points:
                if beats:
                    displacement.append(min(abs(position / Q - p) for p in beats))
            windows.append({"first_frame": first, "end_frame": end, "old_ols": old_score,
                            "common_ols": fitted, "common_explicit": raw, "meter": m})
        for i, p in enumerate(beats):
            counts["raw_events"] += 1
            counts["events_on_960_frame_lattice"] += int(p % 960 == 0)
            counts["events_on_480_frame_lattice"] += int(p % 480 == 0)
            if i + 1 < len(beats) and p % 960 == 0 and beats[i + 1] % 960 == 0 and truth["beat_frames"][0] <= p <= truth["beat_frames"][-1]:
                nearest = min(truth["beat_frames"], key=lambda t: abs(t - p))
                period = ref.annotated.canonical._local_period(truth["beat_frames"], nearest)
                counts["960_lattice_reference_intervals"] += 1
                counts["960_lattice_min_interval_error_over_one_percent"] += int(abs(ref.rounded(period / 960) * 960 - period) > period * .01)
        whole = ref.project(beats, db, 0, truth["requested_frames"])
        tempo = whole["selected"].get("tempo_millibpm", 0)
        request = native_request(beats, db, meters, truth["requested_frames"], tempo)
        completed = subprocess.run([str(analyzer)], input=request, text=True, capture_output=True, timeout=10)
        if completed.returncode:
            raise ValueError("native representation diagnostic failed: " + completed.stderr.strip())
        native = json.loads(completed.stdout)
        if native["explicit_events"] != beats or native["required_segments"] != len(segments):
            raise ValueError("native event/segment identity mismatch")
        counts["explicit_wire_roundtrips"] += int(native["explicit_wire_roundtrip"])
        counts["explicit_native_accepts"] += int(native["explicit_native_accepts"])
        counts["segmented_wire_roundtrips"] += int(native["segmented_wire_roundtrip"])
        counts["segmented_native_accepts"] += int(native["segmented_native_accepts"])
        counts["explicit_builder_accepts"] += int(native["explicit_builder_accepts"])
        counts["segmented_builder_accepts"] += int(native["segmented_builder_accepts"])
        counts["segment_capacity_rejections"] += int(len(segments) > 8)
        counts["same_grid_meter_lookups"] += native["meter_lookups"]
        counts["unsupported_segment_periods"] += native["unsupported_segment_periods"]
        record = {"track": track, "windows": windows, "required_segments": len(segments), "native": native}
        target = output / (track + ".json")
        ref.history.write_new(target, record)
        details.append({"track": track, "sha256": ref.history.sha256_file(target)})
        if time.monotonic() - started > config["resources"]["max_audit_seconds"]:
            raise ValueError("registered audit time cap exceeded")
    if dict(totals["old_coarse"]) != old_report["totals"]["coarse"] or dict(totals["old_ols"]) != old_report["totals"]["beat_this"]:
        raise ValueError("frozen aggregate no longer reproduces")
    if len(old_breaks) != 2 or totals["common_explicit"]["windows"] != 600 or totals["common_explicit"]["reference_complete_windows"] != 401:
        raise ValueError("frozen window/regression scope differs")
    ref.recheck(bindings)
    for row in details:
        ref.bind(output / (row["track"] + ".json"), row["sha256"], {})
    rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if rss > config["resources"]["max_host_rss_kib"]:
        raise ValueError("registered audit memory cap exceeded")
    displacement.sort()
    segment_counts.sort()
    result = {"format": config["format"], "source_revision": ref.revision(ref.ROOT), "protocol_sha256": PROTOCOL_SHA256,
        "formal_acceptance": False, "promotion_allowed": False, "inference_run": False,
        "tracks": 100, "totals": {k: dict(v) for k, v in totals.items()}, "paired_common": dict(changes),
        "counts": dict(counts), "original_joint_breaks": old_breaks,
        "consumer_policy": policy,
        "exact_segments_per_source": {"min": min(segment_counts), "median": segment_counts[len(segment_counts) // 2], "max": max(segment_counts)},
        "projection_distance_to_nearest_raw_event_frames": {"count": len(displacement),
            "median": displacement[len(displacement) // 2] if displacement else None,
            "max": max(displacement, default=None)},
        "resources": {"audit_wall_seconds": time.monotonic() - started, "rss_peak_kib": rss},
        "details": details, "bindings": bindings}
    ref.history.write_new(output / "report.private.json", result)
    return {k: v for k, v in result.items() if k not in ("details", "bindings", "original_joint_breaks")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("attempt", "prepared", "analyzer", "output", "consumer-root", "consumer-report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    result = run(args.attempt, args.prepared, args.analyzer, args.output, args.consumer_root, args.consumer_report)
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
