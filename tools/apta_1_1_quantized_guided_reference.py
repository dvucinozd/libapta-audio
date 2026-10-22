#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Terminal label-guided representation study; never a musical detector."""
from __future__ import annotations

import argparse
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path
from statistics import median
import subprocess
import time

import apta_1_1_free_phase_reachability as free

seg = free.segmented
ref, representation = seg.ref, seg.representation
Q, K = seg.Q, seg.K
PROTOCOL_PATH = ref.ROOT / "docs/rust/QUANTIZED-GUIDED-PROTOCOL.json"
PROTOCOL_SHA256 = "2f5287b6e36742348ba9a012694c26fe6c495ce17278d146cc525d651b29be5f"


def protocol():
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("quantized-guided protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def anchors(data, reachability):
    if len(reachability["points"]) != len(data["beats"]):
        raise ValueError("saved continuous ordinal count changed")
    result = []
    for domain, position in zip(free.constraints(data)[0], reachability["points"]):
        point = Fraction(*position)
        if not domain.contains(point):
            raise ValueError("saved continuous point outside original domain")
        low, high = seg.ceil(domain.low), seg.floor(domain.high)
        if not domain.low_closed and low == domain.low:
            low += 1
        if not domain.high_closed and high == domain.high:
            high -= 1
        if low > high:
            return None
        result.append(min(max(ref.rounded(point), low), high))
    return result


def candidates(data, guided, start):
    periods, bounds, ownership = (data[k] for k in ("periods", "boundaries", "ownership"))
    anchor = guided[start] * Q
    low, high = max(1, anchor - bounds[start] * Q + 1), K // 40000
    for last in range(start, len(periods)):
        error = periods[last] / 100 - 1
        low = max(low, seg.ceil((periods[last] - error) * Q))
        high = min(high, seg.floor((periods[last] + error) * Q))
        distance = last - start
        if distance:
            low = max(low, seg.ceil(Fraction(ownership[last][0] - anchor, distance)))
            high = min(high, seg.floor(Fraction(ownership[last][1] - anchor, distance)))
        elif not ownership[last][0] <= anchor <= ownership[last][1]:
            break
        if low > high or seg.tempo_interval(low, high) is None:
            break
        candidate_low = max(low, seg.ceil(Fraction(bounds[last + 1] * Q - anchor, distance + 1)))
        candidate_high = high
        if last + 1 < len(periods):
            left = (guided[last + 1] - periods[last] - error) * Q
            right = (guided[last + 1] - periods[last] + error) * Q
            if distance:
                candidate_low = max(candidate_low, seg.ceil((left - anchor) / distance))
                candidate_high = min(candidate_high, seg.floor((right - anchor) / distance))
            elif not left <= anchor <= right:
                continue
        interval = seg.tempo_interval(candidate_low, candidate_high)
        if interval is None:
            continue
        preferred = ref.rounded(Fraction(ref.RATE * 60000) / periods[start])
        tempo = min(max(preferred, interval[0]), interval[1])
        yield {"start_ordinal": start, "end_ordinal": last + 1, "first_frame": bounds[start],
               "end_frame": bounds[last + 1], "anchor_frame": guided[start], "beat_count": distance + 1,
               "tempo_millibpm": tempo, "period_q32": K // tempo}


def plan(truth, windows, total, reachability):
    data = seg.prepare(truth, windows, total)
    if data is None:
        return {"status": "insufficient_reference"}
    if reachability["status"] == "infeasible":
        return {"status": "relaxation_infeasible"}
    if reachability["status"] != "feasible":
        raise ValueError("unsupported saved reachability status")
    guided = anchors(data, reachability)
    if guided is None:
        return {"status": "no_integer_guided_anchor"}
    n = len(guided)
    costs, choice = [n + 1] * n + [0], [None] * n
    for start in range(n - 1, -1, -1):
        for candidate in candidates(data, guided, start):
            end = candidate["end_ordinal"]
            cost = 1 + costs[end]
            if cost < costs[start] or cost == costs[start] and choice[start] is not None and end > choice[start]["end_ordinal"]:
                costs[start], choice[start] = cost, candidate
    if costs[0] > n:
        return {"status": "no_guided_family_path", "guided_anchors": guided}
    pieces, start = [], 0
    while start < n:
        pieces.append(choice[start])
        start = pieces[-1]["end_ordinal"]
    seg.validate_points(data, pieces, seg.expand(pieces))
    return {"status": "witness", "segments": pieces, "guided_anchors": guided, "minimum_segments": len(pieces),
            "wire_capacity_ok": len(pieces) <= protocol()["limits"]["wire_segments"],
            "coverage": [data["boundaries"][0], data["boundaries"][-1]]}


def contradiction_category(data):
    domains, _, _, nominal = free.constraints(data)
    if any(p.empty for p in nominal):
        return "empty_nominal_tempo_range"
    if any(d.empty for d in domains):
        return "empty_geometry_domain"
    return "chain_contradiction"


def native_request(truth, witness, meters, total):
    fields = seg.native_request(truth, witness, meters, total).split()
    # Header and all reference pairs retain the legacy protocol ordering.
    offset = 5 + 2 * len(truth["beat_frames"])
    result = ["SEGMENT_GUIDED_V1", *fields[1:offset]]
    for piece in witness["segments"]:
        result += [str(piece[k]) for k in ("start_ordinal", "end_ordinal", "tempo_millibpm", "anchor_frame")]
    result += fields[offset + 3 * len(witness["segments"]):]
    return " ".join(result) + "\n"


def probe(analyzer, request, witness, truth, windows, total):
    meter = seg.meters(truth, windows, witness)
    completed = subprocess.run([str(analyzer)], input=request(truth, witness, meter, total),
                               text=True, capture_output=True, timeout=protocol()["limits"]["native_probe_timeout_seconds"])
    if completed.returncode:
        raise ValueError("native probe rejected witness: " + completed.stderr.strip())
    actual = json.loads(completed.stdout)
    expected = [[p // Q, p % Q, ordinal] for p, ordinal, _ in seg.expand(witness["segments"])]
    if (actual["points"] != expected or not actual["native_accepts"] or not actual["meter_roundtrip"]
            or actual["meter_records"] != len(meter) or actual["wire_roundtrip"] != witness["wire_capacity_ok"]
            or actual["builder_accepts"] != witness["wire_capacity_ok"]):
        raise ValueError("native/wire/Q32/ordinal/meter mismatch")
    return actual, meter


def run(attempt: Path, prepared: Path, prior_audit: Path, prior_segmented: Path,
        prior_reachability: Path, analyzer: Path, output: Path):
    import resource
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("output exists; refusing retry/overwrite")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before measurements")
    revision, bindings = ref.revision(ref.ROOT), {}
    paths = {"prepared_manifest_sha256": prepared / "manifest.private.json",
             "previous_scoring_sha256": attempt / "scoring/report.json",
             "prior_audit_report_sha256": prior_audit / "report.private.json",
             "segmented_report_sha256": prior_segmented / "report.private.json",
             "reachability_report_sha256": prior_reachability / "report.private.json",
             "reachability_aggregate_sha256": ref.ROOT / "docs/rust/FREE-PHASE-RESULT-20261022.json"}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    manifest, old_scores, old_audit, old_segmented, old_free = (json.loads(paths[k].read_text()) for k in
        ("prepared_manifest_sha256", "previous_scoring_sha256", "prior_audit_report_sha256",
         "segmented_report_sha256", "reachability_report_sha256"))
    ids = [r["track"] for r in manifest["tracks"]]
    if (len(ids) != 100 or len(set(ids)) != 100 or manifest["holdout_performances_opened"] is not False
            or any(ids != [r["track"] for r in rows] for rows in
                   (old_scores["details"], old_audit["details"], old_segmented["details"], old_free["details"]))):
        raise ValueError("fixed complete development cohort/order required")
    for directory, rows in ((attempt / "scoring", old_scores["details"]), (prior_audit, old_audit["details"]),
                            (prior_segmented, old_segmented["details"]), (prior_reachability, old_free["details"])):
        for row in rows:
            ref.bind(directory / (row["track"] + ".json"), row["sha256"], bindings)
    for track in ids:
        for path in (attempt / "scoring" / (track + "-coarse.json"), attempt / "beat_this" / (track + ".json")):
            ref.bind(path, old_segmented["bindings"][str(path.resolve())], bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    ref.bind(analyzer, ref.history.sha256_file(analyzer), bindings)
    output.mkdir(parents=True)
    ref.history.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": PROTOCOL_SHA256, "bindings": bindings, "measurement_started": False})
    started = time.monotonic()
    totals = {k: Counter() for k in ("old_coarse", "old_ols", "constant_reference", "guided_native",
                                   "guided_wire", "union_native", "union_wire")}
    statuses, cross, counts, contradictions = Counter(), Counter(), Counter(), Counter()
    counts.update({k: 0 for k in ("guided_native", "guided_wire", "guided_capacity_rejections",
        "guided_meter_records", "guided_wire_meter_records", "additional_wire_witnesses",
        "legacy_responses_identical", "union_native", "union_wire", "union_meter_records", "union_wire_meter_records")})
    incompatible = {k: Counter() for k in ("guided_wire", "union_wire")}
    details, piece_counts, approximation, representatives = [], [], [], {}
    for truth in manifest["tracks"]:
        track, total = truth["track"], truth["requested_frames"]
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        saved = json.loads((attempt / "scoring" / (track + ".json")).read_text())
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        audit = json.loads((prior_audit / (track + ".json")).read_text())
        old = json.loads((prior_segmented / (track + ".json")).read_text())
        reach = json.loads((prior_reachability / (track + ".json")).read_text())["relaxation"]
        if seg.plan(truth, coarse["windows"], total) != old["witness"] or free.plan(truth, coarse["windows"], total) != reach:
            raise ValueError("prior anchored or continuous plan changed")
        witness = plan(truth, coarse["windows"], total, reach)
        statuses[witness["status"]] += 1
        cross[old["witness"]["status"] + " -> " + witness["status"]] += 1
        native, meter, legacy = None, [], None
        if witness["status"] == "witness":
            native, meter = probe(analyzer, native_request, witness, truth, coarse["windows"], total)
            piece_counts.append(witness["minimum_segments"])
            counts.update(guided_native=1, guided_wire=int(witness["wire_capacity_ok"]),
                          guided_capacity_rejections=int(not witness["wire_capacity_ok"]), guided_meter_records=len(meter),
                          guided_wire_meter_records=len(meter) if witness["wire_capacity_ok"] else 0)
            if old["witness"]["status"] != "witness" and witness["wire_capacity_ok"]:
                counts["additional_wire_witnesses"] += 1
        if old["witness"]["status"] == "witness":
            legacy, _ = probe(analyzer, seg.native_request, old["witness"], truth, coarse["windows"], total)
            if legacy != old["native"]:
                raise ValueError("legacy probe response changed")
            counts["legacy_responses_identical"] += 1
            chosen, chosen_native = old["witness"], legacy
        else:
            chosen, chosen_native = witness, native
        chosen_points = seg.expand(chosen["segments"]) if chosen["status"] == "witness" else []
        guided_points = seg.expand(witness["segments"]) if native else []
        if chosen_native:
            counts.update(union_native=1, union_wire=int(chosen["wire_capacity_ok"]),
                          union_meter_records=chosen_native["meter_records"],
                          union_wire_meter_records=chosen_native["meter_records"] if chosen["wire_capacity_ok"] else 0)
        if reach["status"] == "infeasible":
            data = seg.prepare(truth, coarse["windows"], total)
            category = contradiction_category(data)
            contradictions[category] += 1
            representatives.setdefault(category, track)
        for (p, _, _), reference in zip(chosen_points, truth["beat_frames"]):
            approximation.append(abs(Fraction(p, Q) - reference))
        windows = []
        for i, old_window in enumerate(coarse["windows"]):
            first, end = old_window["first_frame"], old_window["end_frame"]
            control = ref.project(events["beat_frames"], events["downbeat_frames"], first, end)
            constant = ref.project(truth["beat_frames"], truth["downbeat_frames"], first, end)
            before, score, constant_score = (ref.annotated.score_window(w, truth, ref.RATE) for w in (old_window, control, constant))
            if (control != saved["models"]["beat_this"]["windows"][i]["candidate"]
                    or score != saved["models"]["beat_this"]["windows"][i]["score"] or before != saved["coarse"][i]
                    or constant != audit["windows"][i]["reference_witness"] or constant_score != audit["windows"][i]["witness_score"]):
                raise ValueError("old tuple/scoring changed")
            for key, value in (("old_coarse", before), ("old_ols", score), ("constant_reference", constant_score)):
                totals[key].update(value)
            results = {}
            for label, points, wire in (("guided", guided_points, witness.get("wire_capacity_ok", False)),
                                        ("union", chosen_points, chosen.get("wire_capacity_ok", False))):
                actual = [(p, points[j + 1][0] - p if j + 1 < len(points) else None)
                          for j, (p, _, _) in enumerate(points) if first * Q <= p < end * Q]
                results[label + "_native"] = representation.score_points(actual, truth, first, end)
                results[label + "_wire"] = representation.score_points(actual if wire else [], truth, first, end)
            for key, value in results.items():
                totals[key].update(value)
                if key in incompatible and audit["windows"][i]["model_score"]["reference_complete_windows"] and audit["windows"][i]["flags"]["reference_period_incompatibility"]:
                    incompatible[key].update(value)
            windows.append(results)
        target = output / (track + ".json")
        ref.history.write_new(target, {"track": track, "guided": witness, "native": native, "meters": meter,
            "prior_status": old["witness"]["status"], "union_uses_prior_control": legacy is not None, "windows": windows})
        details.append({"track": track, "sha256": ref.history.sha256_file(target)})
        if time.monotonic() - started > config["limits"]["max_run_seconds"]:
            raise ValueError("registered runtime cap exceeded")
    if (dict(totals["old_coarse"]) != old_scores["totals"]["coarse"] or dict(totals["old_ols"]) != old_scores["totals"]["beat_this"]
            or dict(totals["constant_reference"]) != old_audit["totals"]["reference_witness"]
            or counts["legacy_responses_identical"] != 47 or sum(contradictions.values()) != 25
            or counts["union_wire"] != 47 + counts["additional_wire_witnesses"]
            or totals["union_wire"]["windows"] != 600 or totals["union_wire"]["reference_complete_windows"] != 401
            or incompatible["union_wire"]["windows"] != 106):
        raise ValueError("fixed control/cohort aggregate mismatch")
    ref.recheck(bindings)
    for row in details:
        ref.bind(output / (row["track"] + ".json"), row["sha256"], {})
    if ref.revision(ref.ROOT) != revision or subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("source changed during measurements")
    elapsed, rss = time.monotonic() - started, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if elapsed > config["limits"]["max_run_seconds"] or rss > config["limits"]["max_host_rss_kib"]:
        raise ValueError("registered resource cap exceeded")
    result = {"format": config["format"], "source_revision": revision, "protocol_sha256": PROTOCOL_SHA256,
        "native_probe_sha256": ref.history.sha256_file(analyzer), "formal_acceptance": False,
        "reference_witness_only": True, "new_detector": False, "production_promotion": False,
        "read_audio": False, "inference_run": False, "transfer_opened": False, "physical_p4": False,
        "reference_research_closed": True, "statuses": dict(statuses), "cross_tab": dict(cross), "counts": dict(counts),
        "contradictions": dict(contradictions), "guided_segment_histogram": dict(Counter(piece_counts)),
        "totals": {k: dict(v) for k, v in totals.items()}, "incompatible_106": {k: dict(v) for k, v in incompatible.items()},
        "union_approximation": {"events": len(approximation), "moved_events": sum(p != 0 for p in approximation),
            "median_abs_frames": float(median(approximation)) if approximation else None,
            "max_abs_frames": float(max(approximation, default=0))},
        "resources": {"run_wall_seconds": elapsed, "rss_peak_kib": rss},
        "private_representatives": representatives, "details": details, "bindings": bindings}
    ref.history.write_new(output / "report.private.json", result)
    return {k: v for k, v in result.items() if k not in ("details", "bindings", "private_representatives")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("attempt", "prepared", "prior-audit", "prior-segmented", "prior-reachability", "analyzer", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.attempt, args.prepared, args.prior_audit, args.prior_segmented,
                         args.prior_reachability, args.analyzer, args.output), sort_keys=True))


if __name__ == "__main__":
    main()
