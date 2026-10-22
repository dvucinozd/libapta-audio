#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Label-based segmented representation witnesses; never a musical detector."""
from __future__ import annotations
import argparse
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path
import subprocess
import time

import apta_1_1_timing_failure_audit as audit

ref, representation = audit.ref, audit.representation
Q = 1 << 32
K = ref.RATE * 60000 * Q
PROTOCOL_PATH = ref.ROOT / "docs/rust/SEGMENTED-REFERENCE-FEASIBILITY-PROTOCOL.json"
PROTOCOL_SHA256 = "483fca380e70810d793ca02c739a342f3c720ad370823e28a53f557b40e991c1"


def protocol():
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("segmented-reference protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def ceil(value):
    value = Fraction(value)
    return -(-value.numerator // value.denominator)


def floor(value):
    value = Fraction(value)
    return value.numerator // value.denominator


def tempo_interval(low, high):
    if low > high or high <= 0:
        return None
    low = max(1, low)
    lower, upper = max(40000, K // (high + 1) + 1), min(300000, K // low)
    return (lower, upper) if lower <= upper else None


def prepare(truth, windows, total):
    limits = protocol()["limits"]
    if type(total) is not int or not 0 < total <= limits["maximum_source_frames"]:
        raise ValueError("invalid bounded source")
    for key in ("beat_frames", "downbeat_frames"):
        ref.valid_events(truth[key], total)
        if len(truth[key]) > limits["maximum_reference_events"]:
            raise ValueError("reference event cap exceeded")
    beats = truth["beat_frames"]
    if not set(truth["downbeat_frames"]) <= set(beats):
        raise ValueError("downbeat outside reference sequence")
    if not 0 < len(windows) <= limits["maximum_windows"]:
        raise ValueError("invalid window count")
    previous = 0
    for window in windows:
        if window["first_frame"] != previous or not previous < window["end_frame"] <= total:
            raise ValueError("fixed complete source partition required")
        previous = window["end_frame"]
    if previous != total:
        raise ValueError("source tail omitted")
    if len(beats) < 2:
        return None
    periods = [Fraction(ref.annotated.canonical._local_period(beats, b)) for b in beats]
    boundaries = [beats[0]] + [(a + b) // 2 + 1 for a, b in zip(beats, beats[1:])] + [beats[-1] + 1]
    ownership, wi = [], 0
    for i, beat in enumerate(beats):
        while beat >= windows[wi]["end_frame"]:
            wi += 1
        tolerance = periods[i] / 10 - 1
        lower = max(Fraction((beat * Q)) - tolerance * Q, boundaries[i] * Q, windows[wi]["first_frame"] * Q)
        upper = min(Fraction(beat * Q) + tolerance * Q, boundaries[i + 1] * Q - 1, windows[wi]["end_frame"] * Q - 1)
        if i + 1 < len(beats):
            upper = min(upper, Fraction((beat + beats[i + 1]) * Q, 2))
        ownership.append((ceil(lower), floor(upper)))
    return {"beats": beats, "periods": periods, "boundaries": boundaries, "ownership": ownership}


def candidates(data, start):
    beats, periods, bounds, ownership = (data[k] for k in ("beats", "periods", "boundaries", "ownership"))
    anchor = beats[start] * Q
    low, high = max(1, anchor - bounds[start] * Q + 1), K // 40000
    for last in range(start, len(beats)):
        tolerance = periods[last] / 100 - 1
        low, high = max(low, ceil((periods[last] - tolerance) * Q)), min(high, floor((periods[last] + tolerance) * Q))
        distance = last - start
        if distance:
            low = max(low, ceil(Fraction(ownership[last][0] - anchor, distance)))
            high = min(high, floor(Fraction(ownership[last][1] - anchor, distance)))
        elif not ownership[last][0] <= anchor <= ownership[last][1]:
            break
        if low > high or tempo_interval(low, high) is None:
            break
        count = distance + 1
        candidate_low = max(low, ceil(Fraction(bounds[last + 1] * Q - anchor, count)))
        candidate_high = high
        if last + 1 < len(beats):
            # Actual outgoing interval to the next piece's fixed first anchor.
            left_position = (Fraction(beats[last + 1]) - periods[last] - tolerance) * Q
            right_position = (Fraction(beats[last + 1]) - periods[last] + tolerance) * Q
            if distance:
                candidate_low = max(candidate_low, ceil((left_position - anchor) / distance))
                candidate_high = min(candidate_high, floor((right_position - anchor) / distance))
            elif not left_position <= anchor <= right_position:
                continue
        interval = tempo_interval(candidate_low, candidate_high)
        if interval is None:
            continue
        preferred = ref.rounded(Fraction(ref.RATE * 60000, 1) / periods[start])
        tempo = min(max(preferred, interval[0]), interval[1])
        yield {"start_ordinal": start, "end_ordinal": last + 1, "first_frame": bounds[start],
               "end_frame": bounds[last + 1], "anchor_frame": beats[start],
               "beat_count": count, "tempo_millibpm": tempo, "period_q32": K // tempo}


def plan(truth, windows, total, gaps=()):
    data = prepare(truth, windows, total)
    previous = 0
    for first, end in gaps:
        if type(first) is not int or type(end) is not int or not previous <= first < end <= total:
            raise ValueError("invalid declared input gaps")
        previous = end
    if data is None:
        return {"status": "insufficient_reference", "segments": [], "minimum_segments": None}
    if any(first < data["boundaries"][-1] and data["boundaries"][0] < end for first, end in gaps):
        return {"status": "unsupported_pcm_gap", "segments": [], "minimum_segments": None}
    n = len(data["beats"])
    cost, choice = [n + 1] * n + [0], [None] * n
    for start in range(n - 1, -1, -1):
        for candidate in candidates(data, start):
            end = candidate["end_ordinal"]
            value = 1 + cost[end]
            if value < cost[start] or (value == cost[start] and choice[start] is not None and end > choice[start]["end_ordinal"]):
                cost[start], choice[start] = value, candidate
    if choice[0] is None or cost[0] > n:
        return {"status": "no_anchored_family_path", "segments": [], "minimum_segments": None}
    pieces, start = [], 0
    while start < n:
        pieces.append(choice[start])
        start = pieces[-1]["end_ordinal"]
    points = expand(pieces)
    validate_points(data, pieces, points)
    return {"status": "witness", "segments": pieces, "minimum_segments": len(pieces),
            "wire_capacity_ok": len(pieces) <= protocol()["limits"]["wire_segments"],
            "coverage": [data["boundaries"][0], data["boundaries"][-1]]}


def expand(pieces):
    return [(s["anchor_frame"] * Q + i * s["period_q32"], s["start_ordinal"] + i, s["period_q32"])
            for s in pieces for i in range(s["beat_count"])]


def validate_points(data, pieces, points):
    beats, periods = data["beats"], data["periods"]
    if len(points) != len(beats):
        raise ValueError("witness changed reference event count")
    for i, (position, ordinal, period) in enumerate(points):
        tolerance = periods[i] / 100 - 1
        if (ordinal != i or not data["ownership"][i][0] <= position <= data["ownership"][i][1]
                or abs(Fraction(period, Q) - periods[i]) > tolerance
                or i + 1 < len(points) and abs(Fraction(points[i + 1][0] - position, Q) - periods[i]) > tolerance):
            raise ValueError("witness phase/period/ordinal/junction mismatch")
    previous_end = data["boundaries"][0]
    for segment in pieces:
        anchor, period, count = segment["anchor_frame"] * Q, segment["period_q32"], segment["beat_count"]
        if (segment["first_frame"] != previous_end or anchor - period >= segment["first_frame"] * Q
                or anchor + count * period < segment["end_frame"] * Q):
            raise ValueError("segment count hides a natural in-range point")
        previous_end = segment["end_frame"]
    if previous_end != data["boundaries"][-1]:
        raise ValueError("reference coverage changed")


def meters(truth, windows, witness):
    points = expand(witness["segments"])
    result = []
    for w in windows:
        meter = representation.meter_on_events(truth["beat_frames"], truth["downbeat_frames"], w["first_frame"], w["end_frame"])
        if meter:
            meter = dict(meter, first_frame=max(w["first_frame"], witness["coverage"][0]),
                         end_frame=min(w["end_frame"], witness["coverage"][1]),
                         downbeat_frame=points[meter["downbeat_ordinal"]][0] // Q)
            if not meter["first_frame"] <= meter["downbeat_frame"] < meter["end_frame"]:
                raise ValueError("mapped meter escaped source window")
            result.append(meter)
    return result


def native_request(truth, witness, meter, total):
    fields = ["SEGMENT_REF_V1", str(total), str(len(truth["beat_frames"])), str(len(witness["segments"])), str(len(meter))]
    for frame in truth["beat_frames"]:
        local = Fraction(ref.annotated.canonical._local_period(truth["beat_frames"], frame)) * 2
        if local.denominator != 1:
            raise ValueError("non-half-integer local reference period")
        fields += [str(frame), str(local.numerator)]
    for segment in witness["segments"]:
        fields += [str(segment[k]) for k in ("start_ordinal", "end_ordinal", "tempo_millibpm")]
    for m in meter:
        fields += [str(m[k]) for k in ("first_frame", "end_frame", "downbeat_frame", "downbeat_ordinal", "numerator")]
    return " ".join(fields) + "\n"


def run(attempt: Path, prepared: Path, prior_audit: Path, analyzer: Path, output: Path):
    import resource
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("output exists; refusing retry/overwrite")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before witness measurements")
    revision, bindings = ref.revision(ref.ROOT), {}
    paths = {"prepared_manifest_sha256": prepared / "manifest.private.json",
             "previous_scoring_sha256": attempt / "scoring/report.json",
             "beat_this_worker_report_sha256": attempt / "beat_this/report.json",
             "prior_audit_report_sha256": prior_audit / "report.private.json",
             "prior_audit_aggregate_sha256": ref.ROOT / "docs/rust/TIMING-FAILURE-AUDIT-RESULT-20261022.json"}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    ref.bind(analyzer, ref.history.sha256_file(analyzer), bindings)
    manifest, old_report, worker, old_audit = (json.loads(paths[k].read_text()) for k in
        ("prepared_manifest_sha256", "previous_scoring_sha256", "beat_this_worker_report_sha256", "prior_audit_report_sha256"))
    ids = [r["track"] for r in manifest["tracks"]]
    if (len(ids) != 100 or len(set(ids)) != 100 or manifest["holdout_performances_opened"] is not False
            or any(ids != [r["track"] for r in rows] for rows in (old_report["details"], worker["outputs"], old_audit["details"]))):
        raise ValueError("fixed complete development cohort/order required")
    for directory, rows, key in ((attempt / "scoring", old_report["details"], "sha256"),
                                  (attempt / "beat_this", worker["outputs"], "events_sha256"),
                                  (prior_audit, old_audit["details"], "sha256")):
        for row in rows:
            ref.bind(directory / (row["track"] + ".json"), row[key], bindings)
    for track in ids:
        path = attempt / "scoring" / (track + "-coarse.json")
        ref.bind(path, ref.history.sha256_file(path), bindings)
    output.mkdir(parents=True)
    ref.history.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": PROTOCOL_SHA256, "bindings": bindings, "witness_measurement_started": False})
    started = time.monotonic()
    totals = {k: Counter() for k in ("old_coarse", "old_ols", "constant_reference", "native_nominal", "native_actual", "wire_actual")}
    counts, statuses, segment_counts, details = Counter(), Counter(), [], []
    incompatible = {k: Counter() for k in ("native_actual", "wire_actual")}
    for truth in manifest["tracks"]:
        track, total = truth["track"], truth["requested_frames"]
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        saved = json.loads((attempt / "scoring" / (track + ".json")).read_text())
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        prior = json.loads((prior_audit / (track + ".json")).read_text())
        witness = plan(truth, coarse["windows"], total)
        statuses[witness["status"]] += 1
        points, native, meter = [], None, []
        if witness["status"] == "witness":
            points, meter = expand(witness["segments"]), meters(truth, coarse["windows"], witness)
            completed = subprocess.run([str(analyzer)], input=native_request(truth, witness, meter, total), text=True,
                                       capture_output=True, timeout=config["limits"]["native_probe_timeout_seconds"])
            if completed.returncode:
                raise ValueError("native witness probe failed: " + completed.stderr.strip())
            native = json.loads(completed.stdout)
            expected = [[p // Q, p % Q, ordinal] for p, ordinal, _ in points]
            if (native["points"] != expected or not native["native_accepts"] or not native["meter_roundtrip"]
                    or native["meter_records"] != len(meter)
                    or native["wire_roundtrip"] != witness["wire_capacity_ok"]
                    or native["builder_accepts"] != witness["wire_capacity_ok"]):
                raise ValueError("native/wire/Q32/ordinal/meter identity mismatch")
            segment_counts.append(witness["minimum_segments"])
            counts.update(native_graphs=1, wire_graphs=int(native["wire_roundtrip"]), meter_records=len(meter),
                          meter_lookups=native["meter_records"], capacity_rejections=int(not witness["wire_capacity_ok"]))
        windows = []
        for i, old in enumerate(coarse["windows"]):
            first, end = old["first_frame"], old["end_frame"]
            control = ref.project(events["beat_frames"], events["downbeat_frames"], first, end)
            score = ref.annotated.score_window(control, truth, ref.RATE)
            before = ref.annotated.score_window(old, truth, ref.RATE)
            constant = ref.project(truth["beat_frames"], truth["downbeat_frames"], first, end)
            constant_score = ref.annotated.score_window(constant, truth, ref.RATE)
            previous = saved["models"]["beat_this"]["windows"][i]
            if (control != previous["candidate"] or score != previous["score"] or before != saved["coarse"][i]
                    or control != prior["windows"][i]["model"] or constant != prior["windows"][i]["reference_witness"]
                    or constant_score != prior["windows"][i]["witness_score"]):
                raise ValueError("frozen old tuples or witness/scorer changed")
            for key, result in (("old_coarse", before), ("old_ols", score), ("constant_reference", constant_score)):
                totals[key].update(result)
            nominal = [(p, period) for p, _, period in points if first * Q <= p < end * Q]
            actual = [(p, points[j + 1][0] - p if j + 1 < len(points) else None)
                      for j, (p, _, _) in enumerate(points) if first * Q <= p < end * Q]
            results = {"native_nominal": representation.score_points(nominal, truth, first, end),
                       "native_actual": representation.score_points(actual, truth, first, end),
                       "wire_actual": representation.score_points(actual if witness.get("wire_capacity_ok") else [], truth, first, end)}
            for key, result in results.items():
                totals[key].update(result)
                if key in incompatible and prior["windows"][i]["model_score"]["reference_complete_windows"] and prior["windows"][i]["flags"]["reference_period_incompatibility"]:
                    incompatible[key].update(result)
            windows.append({"first_frame": first, "end_frame": end, **results})
        target = output / (track + ".json")
        ref.history.write_new(target, {"track": track, "witness": witness, "native": native,
                                      "meters": meter, "windows": windows})
        details.append({"track": track, "sha256": ref.history.sha256_file(target)})
        if time.monotonic() - started > config["limits"]["max_run_seconds"]:
            raise ValueError("registered runtime cap exceeded")
    if (dict(totals["old_coarse"]) != old_report["totals"]["coarse"] or dict(totals["old_ols"]) != old_report["totals"]["beat_this"]
            or dict(totals["constant_reference"]) != old_audit["totals"]["reference_witness"]
            or totals["native_actual"]["windows"] != 600 or totals["native_actual"]["reference_complete_windows"] != 401
            or incompatible["native_actual"]["windows"] != 106):
        raise ValueError("fixed cohort/control aggregate mismatch")
    ref.recheck(bindings)
    for row in details:
        ref.bind(output / (row["track"] + ".json"), row["sha256"], {})
    if ref.revision(ref.ROOT) != revision or subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("source changed during witness measurements")
    elapsed, rss = time.monotonic() - started, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if elapsed > config["limits"]["max_run_seconds"] or rss > config["limits"]["max_host_rss_kib"]:
        raise ValueError("registered resource cap exceeded")
    segment_counts.sort()
    result = {"format": config["format"], "source_revision": revision, "protocol_sha256": PROTOCOL_SHA256,
        "label_based_witness": True, "formal_acceptance": False, "production_promotion": False, "new_detector": False,
        "inference_run": False, "read_audio": False, "transfer_opened": False, "physical_p4": False,
        "counts": dict(counts), "statuses": dict(statuses), "totals": {k: dict(v) for k, v in totals.items()},
        "incompatible_106": {k: dict(v) for k, v in incompatible.items()},
        "segments": {"histogram": dict(Counter(segment_counts)), "min": min(segment_counts, default=None),
                     "median": segment_counts[len(segment_counts) // 2] if segment_counts else None, "max": max(segment_counts, default=None)},
        "resources": {"run_wall_seconds": elapsed, "rss_peak_kib": rss}, "details": details, "bindings": bindings}
    ref.history.write_new(output / "report.private.json", result)
    return {k: v for k, v in result.items() if k not in ("details", "bindings")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("attempt", "prepared", "prior-audit", "analyzer", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.attempt, args.prepared, args.prior_audit, args.analyzer, args.output), sort_keys=True))


if __name__ == "__main__":
    main()
