#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exact reference-only timing relaxation; no native construction or detector."""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from fractions import Fraction
import json
from math import lcm
from pathlib import Path
from statistics import median
import subprocess
import time

import apta_1_1_segmented_reference as segmented

ref, representation = segmented.ref, segmented.representation
Q, K = segmented.Q, segmented.K
PROTOCOL_PATH = ref.ROOT / "docs/rust/FREE-PHASE-REACHABILITY-PROTOCOL.json"
PROTOCOL_SHA256 = "54a5cac6ee6ce86c82589fd34728fc54d9a9a5bf0b66ba5c67d5f473a9b71f41"


def protocol():
    if ref.history.sha256_file(PROTOCOL_PATH) != PROTOCOL_SHA256:
        raise ValueError("free-phase protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def rational(value):
    value = Fraction(value)
    return [value.numerator, value.denominator]


@dataclass(frozen=True)
class Interval:
    low: Fraction
    high: Fraction
    low_closed: bool = True
    high_closed: bool = True

    def __post_init__(self):
        object.__setattr__(self, "low", Fraction(self.low))
        object.__setattr__(self, "high", Fraction(self.high))

    @property
    def empty(self):
        return self.low > self.high or (self.low == self.high and not (self.low_closed and self.high_closed))

    def contains(self, value):
        return not self.empty and (value > self.low or value == self.low and self.low_closed) and (
            value < self.high or value == self.high and self.high_closed)

    def intersect(self, other):
        low, high = max(self.low, other.low), min(self.high, other.high)
        lc = (self.low_closed if self.low == low else True) and (other.low_closed if other.low == low else True)
        hc = (self.high_closed if self.high == high else True) and (other.high_closed if other.high == high else True)
        return Interval(low, high, lc, hc)

    def add(self, other):
        return Interval(self.low + other.low, self.high + other.high,
                        self.low_closed and other.low_closed, self.high_closed and other.high_closed)

    def choose(self, preferred):
        if self.empty:
            raise ValueError("cannot choose from empty interval")
        if self.contains(preferred):
            return Fraction(preferred)
        if preferred <= self.low and self.low_closed:
            return self.low
        if preferred >= self.high and self.high_closed:
            return self.high
        return (self.low + self.high) / 2

    def record(self):
        return {"low": rational(self.low), "high": rational(self.high),
                "low_closed": self.low_closed, "high_closed": self.high_closed}


def reachable(domains, steps, preferred):
    """Exact projection of a chain of interval difference constraints."""
    if not domains or len(steps) != len(domains) - 1 or len(preferred) != len(domains):
        raise ValueError("invalid interval chain")
    forward = []
    for i, domain in enumerate(domains):
        current = domain if not i else domain.intersect(forward[-1].add(steps[i - 1]))
        if domain.empty or i and steps[i - 1].empty or current.empty:
            return {"status": "infeasible", "first_empty_ordinal": i,
                    "domain": domain.record(), "reachable": current.record(),
                    "previous_reachable": forward[-1].record() if i else None,
                    "incoming_step": steps[i - 1].record() if i else None}, []
        forward.append(current)
    points = [forward[-1].choose(preferred[-1])]
    for i in range(len(domains) - 2, -1, -1):
        step, following = steps[i], points[-1]
        predecessors = Interval(following - step.high, following - step.low,
                                step.high_closed, step.low_closed)
        points.append(forward[i].intersect(predecessors).choose(preferred[i]))
    points.reverse()
    return {"status": "feasible", "final_reachable": forward[-1].record()}, points


def difference_constraints_feasible(nodes, edges):
    """Independent Bellman-Ford: x_v-x_u <= bound (or < when strict).

    Integer scale is exact; lexicographic (weight,-strict_count) represents an
    infinitesimal solely for cycle detection, never a numeric epsilon.
    """
    scale = lcm(*(Fraction(bound).denominator for _, _, bound, _ in edges)) if edges else 1
    weights = [(u, v, int(Fraction(bound) * scale), -int(strict)) for u, v, bound, strict in edges]
    if nodes < 1 or any(not 0 <= u < nodes or not 0 <= v < nodes for u, v, _, _ in weights):
        raise ValueError("invalid difference graph")
    distance, predecessor = [(0, 0)] * nodes, [None] * nodes
    changed = None
    for _ in range(nodes):
        changed = None
        for index, (u, v, bound, strict) in enumerate(weights):
            candidate = (distance[u][0] + bound, distance[u][1] + strict)
            if candidate < distance[v]:
                distance[v], predecessor[v], changed = candidate, index, v
        if changed is None:
            return True, None
    vertex = changed
    for _ in range(nodes):
        vertex = weights[predecessor[vertex]][0]
    start, cycle = vertex, []
    while True:
        index = predecessor[vertex]
        cycle.append(index)
        vertex = weights[index][0]
        if vertex == start:
            break
        if len(cycle) > nodes:
            raise ValueError("invalid negative-cycle certificate")
    total = sum((Fraction(edges[i][2]) for i in cycle), Fraction())
    strict_count = sum(int(edges[i][3]) for i in cycle)
    if not (total < 0 or total == 0 and strict_count):
        raise ValueError("uncertified negative cycle")
    return False, {"edge_indices": cycle, "total_bound": rational(total), "strict_edges": strict_count}


def constraints(data):
    """Keep raw inequalities for the checker, not solver-intersected domains."""
    domains, steps, edges, nominal = [], [], [], []
    native = Interval(Fraction(K // 300000, Q), Fraction(K // 40000, Q))
    for i, (local, (lo, hi)) in enumerate(zip(data["periods"], data["ownership"])):
        error = local / 100 - 1
        period = Interval(local - error, local + error)
        allowed = period.intersect(native)
        nominal.append(allowed)
        cell_first, cell_end = data["boundaries"][i:i + 2]
        raw = Interval(Fraction(lo, Q), Fraction(hi, Q))
        geometry = Interval(cell_end - allowed.high, cell_first + allowed.high, True, False)
        domain = raw.intersect(geometry)
        vertex = i + 1
        edges += [(0, vertex, raw.high, False), (vertex, 0, -raw.low, False),
                  (0, vertex, cell_first + allowed.high, True),
                  (vertex, 0, allowed.high - cell_end, False)]
        if allowed.empty:
            domain = Interval(1, 0)
            edges.append((vertex, vertex, Fraction(-1), False))
        domains.append(domain)
        if i + 1 < len(data["beats"]):
            steps.append(period)
            edges += [(vertex, vertex + 1, period.high, False),
                      (vertex + 1, vertex, -period.low, False)]
    return domains, steps, edges, nominal


def validate(data, points, nominal):
    if len(points) != len(data["beats"]) or len(nominal) != len(points):
        raise ValueError("changed continuous ordinal count")
    for i, point in enumerate(points):
        local = data["periods"][i]
        lo, hi = data["ownership"][i]
        period = nominal[i]
        if (not Fraction(lo, Q) <= point <= Fraction(hi, Q) or period.empty
                or not point - period.high < data["boundaries"][i]
                or not point + period.high >= data["boundaries"][i + 1]
                or i + 1 < len(points) and not local - (local / 100 - 1) <= points[i + 1] - point <= local + (local / 100 - 1)):
            raise ValueError("continuous witness violates original inequality")


def plan(truth, windows, total, gaps=()):
    data = segmented.prepare(truth, windows, total)
    previous = 0
    for first, end in gaps:
        if type(first) is not int or type(end) is not int or not previous <= first < end <= total:
            raise ValueError("invalid declared input gaps")
        previous = end
    if data is None:
        return {"status": "insufficient_reference"}
    if any(first < data["boundaries"][-1] and data["boundaries"][0] < end for first, end in gaps):
        return {"status": "unsupported_pcm_gap"}
    domains, steps, edges, nominal = constraints(data)
    result, points = reachable(domains, steps, data["beats"])
    independent, certificate = difference_constraints_feasible(len(domains) + 1, edges)
    if independent != (result["status"] == "feasible"):
        raise ValueError("interval/difference-constraint disagreement")
    result["difference_constraints_agree"] = True
    if points:
        validate(data, points, nominal)
        result["points"] = [rational(p) for p in points]
        result["nominal_periods"] = [rational(p.high) for p in nominal]
    else:
        result["negative_cycle"] = certificate
    return result


def run(attempt: Path, prepared: Path, prior_audit: Path, prior_segmented: Path, output: Path):
    import resource
    config = protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("output exists; refusing retry/overwrite")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("commit implementation before reachability measurements")
    revision, bindings = ref.revision(ref.ROOT), {}
    paths = {"prepared_manifest_sha256": prepared / "manifest.private.json",
             "previous_scoring_sha256": attempt / "scoring/report.json",
             "prior_audit_report_sha256": prior_audit / "report.private.json",
             "segmented_report_sha256": prior_segmented / "report.private.json",
             "segmented_aggregate_sha256": ref.ROOT / "docs/rust/SEGMENTED-REFERENCE-RESULT-20261022.json",
             "segmented_protocol_sha256": segmented.PROTOCOL_PATH}
    for key, path in paths.items():
        ref.bind(path, config["inputs"][key], bindings)
    manifest, old_report, old_audit, old_segmented = (json.loads(paths[k].read_text()) for k in
        ("prepared_manifest_sha256", "previous_scoring_sha256", "prior_audit_report_sha256", "segmented_report_sha256"))
    ids = [r["track"] for r in manifest["tracks"]]
    if (len(ids) != 100 or len(set(ids)) != 100 or manifest["holdout_performances_opened"] is not False
            or any(ids != [r["track"] for r in rows] for rows in (old_report["details"], old_audit["details"], old_segmented["details"]))):
        raise ValueError("fixed complete development cohort/order required")
    for directory, rows in ((attempt / "scoring", old_report["details"]), (prior_audit, old_audit["details"]),
                            (prior_segmented, old_segmented["details"])):
        for row in rows:
            ref.bind(directory / (row["track"] + ".json"), row["sha256"], bindings)
    for track in ids:
        for path in (attempt / "scoring" / (track + "-coarse.json"), attempt / "beat_this" / (track + ".json")):
            ref.bind(path, old_segmented["bindings"][str(path.resolve())], bindings)
    for name in subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0"):
        if name:
            path = ref.ROOT / name
            ref.bind(path, ref.history.sha256_file(path), bindings)
    output.mkdir(parents=True)
    ref.history.write_new(output / "freeze.private.json", {"source_revision": revision,
        "protocol_sha256": PROTOCOL_SHA256, "bindings": bindings, "measurement_started": False})
    started = time.monotonic()
    totals = {k: Counter() for k in ("old_coarse", "old_ols", "constant_reference", "continuous_actual")}
    statuses, cross, incompatible, details = Counter(), Counter(), Counter(), []
    approximation, maximum_phase, old_witnesses = [], Fraction(), 0
    for truth in manifest["tracks"]:
        track, total = truth["track"], truth["requested_frames"]
        coarse = json.loads((attempt / "scoring" / (track + "-coarse.json")).read_text())
        saved = json.loads((attempt / "scoring" / (track + ".json")).read_text())
        events = json.loads((attempt / "beat_this" / (track + ".json")).read_text())["events"]
        audit = json.loads((prior_audit / (track + ".json")).read_text())
        prior = json.loads((prior_segmented / (track + ".json")).read_text())
        if segmented.plan(truth, coarse["windows"], total) != prior["witness"]:
            raise ValueError("prior anchored plan changed")
        result = plan(truth, coarse["windows"], total)
        statuses[result["status"]] += 1
        cross[prior["witness"]["status"] + " -> " + result["status"]] += 1
        data = segmented.prepare(truth, coarse["windows"], total)
        nominal = constraints(data)[3]
        if prior["witness"]["status"] == "witness":
            if result["status"] != "feasible":
                raise ValueError("old native witness excluded")
            validate(data, [Fraction(p, Q) for p, _, _ in segmented.expand(prior["witness"]["segments"])], nominal)
            old_witnesses += 1
        points = [Fraction(*p) for p in result.get("points", [])]
        for p, b, local in zip(points, truth["beat_frames"], data["periods"]):
            approximation.append(abs(p - b))
            maximum_phase = max(maximum_phase, abs(p - b) / local)
        windows = []
        for i, old in enumerate(coarse["windows"]):
            first, end = old["first_frame"], old["end_frame"]
            control = ref.project(events["beat_frames"], events["downbeat_frames"], first, end)
            constant = ref.project(truth["beat_frames"], truth["downbeat_frames"], first, end)
            before = ref.annotated.score_window(old, truth, ref.RATE)
            score = ref.annotated.score_window(control, truth, ref.RATE)
            constant_score = ref.annotated.score_window(constant, truth, ref.RATE)
            if (control != saved["models"]["beat_this"]["windows"][i]["candidate"]
                    or score != saved["models"]["beat_this"]["windows"][i]["score"] or before != saved["coarse"][i]
                    or control != audit["windows"][i]["model"] or constant != audit["windows"][i]["reference_witness"]
                    or constant_score != audit["windows"][i]["witness_score"]):
                raise ValueError("frozen old tuple/score changed")
            for key, value in (("old_coarse", before), ("old_ols", score), ("constant_reference", constant_score)):
                totals[key].update(value)
            actual = [(p * Q, (points[j + 1] - p) * Q if j + 1 < len(points) else None)
                      for j, p in enumerate(points) if first <= p < end]
            diagnostic = representation.score_points(actual, truth, first, end)
            totals["continuous_actual"].update(diagnostic)
            if (audit["windows"][i]["model_score"]["reference_complete_windows"]
                    and audit["windows"][i]["flags"]["reference_period_incompatibility"]):
                incompatible.update(diagnostic)
            windows.append(diagnostic)
        target = output / (track + ".json")
        ref.history.write_new(target, {"track": track, "prior_status": prior["witness"]["status"],
                                      "relaxation": result, "windows": windows})
        details.append({"track": track, "sha256": ref.history.sha256_file(target)})
        if time.monotonic() - started > config["limits"]["max_run_seconds"]:
            raise ValueError("registered runtime cap exceeded")
    if (dict(totals["old_coarse"]) != old_report["totals"]["coarse"] or dict(totals["old_ols"]) != old_report["totals"]["beat_this"]
            or dict(totals["constant_reference"]) != old_audit["totals"]["reference_witness"] or old_witnesses != 47
            or totals["continuous_actual"]["windows"] != 600 or totals["continuous_actual"]["reference_complete_windows"] != 401
            or incompatible["windows"] != 106 or sum(statuses.values()) != 100):
        raise ValueError("fixed cohort/baseline/witness aggregate mismatch")
    ref.recheck(bindings)
    for row in details:
        ref.bind(output / (row["track"] + ".json"), row["sha256"], {})
    if ref.revision(ref.ROOT) != revision or subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True):
        raise ValueError("source changed during measurements")
    elapsed, rss = time.monotonic() - started, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    if elapsed > config["limits"]["max_run_seconds"] or rss > config["limits"]["max_host_rss_kib"]:
        raise ValueError("registered resource cap exceeded")
    approximation.sort()
    result = {"format": config["format"], "source_revision": revision, "protocol_sha256": PROTOCOL_SHA256,
        "reference_relaxation_only": True, "formal_acceptance": False, "new_detector": False,
        "native_construction": False, "production_promotion": False, "read_audio": False,
        "inference_run": False, "transfer_opened": False, "physical_p4": False,
        "statuses": dict(statuses), "prior_cross_tab": dict(cross), "old_native_witnesses_admissible": old_witnesses,
        "all_100_interval_and_difference_constraint_verdicts_agree": True,
        "all_100_prior_anchored_plans_reproduced": True,
        "totals": {k: dict(v) for k, v in totals.items()}, "incompatible_106": dict(incompatible),
        "approximation": {"events": len(approximation), "moved_events": sum(p != 0 for p in approximation),
            "median_abs_frames": float(median(approximation)) if approximation else None,
            "max_abs_frames": float(max(approximation, default=0)), "max_phase_beats": float(maximum_phase)},
        "resources": {"run_wall_seconds": elapsed, "rss_peak_kib": rss}, "details": details, "bindings": bindings}
    ref.history.write_new(output / "report.private.json", result)
    return {k: v for k, v in result.items() if k not in ("details", "bindings")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("attempt", "prepared", "prior-audit", "prior-segmented", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.attempt, args.prepared, args.prior_audit, args.prior_segmented, args.output), sort_keys=True))


if __name__ == "__main__":
    main()
