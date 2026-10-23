#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent scalar DD-04 replay; no production recurrence or labels."""
from array import array
from fractions import Fraction
import json
import math
import struct

PROTOCOL_SHA256 = "5dc1241087a08663b06766f91faade5a5758f9791fd85e0077c62e78d5a186fd"


def quantized(value):
    if type(value) not in (float, int):
        raise ValueError("expected finite exact f32")
    try:
        if not math.isfinite(value) or struct.unpack("<f", struct.pack("<f", value))[0] != value:
            raise ValueError("expected finite exact f32")
    except (OverflowError, struct.error) as exc:
        raise ValueError("expected finite exact f32") from exc
    scaled = Fraction(min(32, max(-32, value))) * 1024
    magnitude = abs(scaled) + Fraction(1, 2)
    rounded = magnitude.numerator // magnitude.denominator
    return -rounded if scaled < 0 else rounded


def scalar_meter(beat, downbeat, total, meter):
    """Phase/interval/row tables, unlike production's row-major flat layout."""
    count = len(beat)
    scores = [[array("q", [-(1 << 62)]) * count for _ in range(66)] for _ in range(meter)]
    previous = [[bytearray([255]) * count for _ in range(66)] for _ in range(meter)]
    terminal_candidates = []
    transitions = 0
    for row in range(count):
        for phase in range(meter):
            emission = beat[row] + (downbeat[row] if phase == 0 else 0)
            for interval in range(10, 76):
                slot = interval - 10
                if row < interval:
                    score, parent = emission, 0
                else:
                    choices = []
                    for before in range(10, 76):
                        if abs(before - interval) <= 2:
                            transitions += 1
                            value = scores[(phase + meter - 1) % meter][before - 10][row - interval]
                            if value != -(1 << 62):
                                choices.append((value + emission - 2048 * (interval - before) ** 2, before))
                    if not choices:
                        continue
                    score, parent = min(choices, key=lambda item: (-item[0], item[1]))
                scores[phase][slot][row] = score
                previous[phase][slot][row] = parent
                if (row + interval) * 960 >= total:
                    terminal_candidates.append((-score, row, interval, phase))
    negative, row, interval, phase = min(terminal_candidates)
    terminal = {"row": row, "interval_rows": interval, "phase": phase}
    backward = []
    while True:
        backward.append((row, interval, phase))
        before = previous[phase][interval - 10][row]
        if before == 0:
            break
        if before == 255 or len(backward) > count:
            raise ValueError("invalid scalar traceback")
        row -= interval
        interval = before
        phase = (phase + meter - 1) % meter
    path = list(reversed(backward))
    # Re-sum the saved path without consulting the tables or parent indices.
    objective = 0
    for index, (row, interval, phase) in enumerate(path):
        if index == 0:
            if row >= interval:
                raise ValueError("non-seed first state")
        else:
            oldrow, oldinterval, oldphase = path[index - 1]
            if row - oldrow != interval or abs(interval - oldinterval) > 2 or phase != (oldphase + 1) % meter:
                raise ValueError("saved transition invalid")
            objective -= 2048 * (interval - oldinterval) ** 2
        objective += beat[row] + (downbeat[row] if phase == 0 else 0)
    if objective != -negative or (path[-1][0] + path[-1][1]) * 960 < total:
        raise ValueError("saved objective or terminal invalid")
    cells = count * 66 * meter
    return {"meter": meter, "terminal_score": objective, "terminal_state": terminal,
            "beat_rows": [s[0] for s in path], "incoming_intervals": [s[1] for s in path],
            "beat_phases": [s[2] for s in path], "state_cells": cells,
            "state_bytes": cells * 9, "transitions_examined": transitions}


def refined_frame(values, row):
    offset = Fraction(0)
    if row > 0 and row + 1 < len(values):
        a, b, c = values[row - 1], values[row], values[row + 1]
        if a < b and c < b:
            offset = Fraction(a - c, 2 * a - 4 * b + 2 * c)
            offset = sorted((Fraction(-1, 2), offset, Fraction(1, 2)))[1]
    position = 960 * (Fraction(row) + offset) + Fraction(1, 2)
    return position.numerator // position.denominator


def reconstruct(beat, downbeat, total, windows, gaps=()):
    """Rebuild every candidate field independently, including abstention metadata."""
    if not isinstance(beat, (list, tuple, array)) or not isinstance(downbeat, (list, tuple, array)):
        raise ValueError("paired arrays required")
    if type(total) is not int or not 0 < total <= 1440000 or not 0 < len(beat) == len(downbeat) <= 1501:
        raise ValueError("source capacity")
    samples = Fraction(total * 22050, 48000)
    floor = samples.numerator // samples.denominator
    ceil = -(-samples.numerator // samples.denominator)
    if len(beat) not in (floor // 441 + 1, ceil // 441 + 1):
        raise ValueError("row geometry")
    if not isinstance(windows, (list, tuple)) or not 1 <= len(windows) <= 8:
        raise ValueError("window count")
    end = 0
    for window in windows:
        if not isinstance(window, dict) or set(window) != {"first_frame", "end_frame"}:
            raise ValueError("plain geometry required")
        if (type(window["first_frame"]) is not int or type(window["end_frame"]) is not int
                or window["first_frame"] != end or not end < window["end_frame"] <= total):
            raise ValueError("window partition")
        end = window["end_frame"]
    if end != total or not isinstance(gaps, (tuple, list)):
        raise ValueError("tail or gaps")
    end = 0
    for gap in gaps:
        if (not isinstance(gap, (tuple, list)) or len(gap) != 2
                or any(type(p) is not int for p in gap) or not end <= gap[0] < gap[1] <= total):
            raise ValueError("invalid gap")
        end = gap[1]
    domain = min(len(beat), -(-total // 960))
    qb, qd = [quantized(v) for v in beat], [quantized(v) for v in downbeat]
    qb, qd = qb[:domain], qd[:domain]
    result = {"format": "libapta-dense-decoder-candidate-1", "decoder_name": "dense-joint-renewal-v1",
              "total_frames": total, "status": "known_pcm_gap", "selected_meter": None,
              "terminal_score": None, "confidence": 0, "confidence_calibrated": False,
              "per_meter_diagnostics": [], "score_difference_4_minus_3": None,
              "events": {"beat_frames": [], "downbeat_frames": [], "beat_rows": [], "beat_phases": []},
              "windows": [dict(w, status="known_pcm_gap", beat_ordinals=[]) for w in windows],
              "known_pcm_gaps": [list(g) for g in gaps], "boundary_rows_excluded": list(range(domain, len(beat))),
              "clipped_logits": {"beat": sum(abs(v) > 32 for v in beat), "downbeat": sum(abs(v) > 32 for v in downbeat)},
              "window_ownership_changes": [], "state_cells": 0, "maximum_live_state_bytes": 0,
              "transitions_examined": 0, "native_graph_constructed": False, "production_promotion": False}
    if gaps:
        return result
    for meter in (3, 4):
        result["per_meter_diagnostics"].append(scalar_meter(qb, qd, total, meter))
    a, b = result["per_meter_diagnostics"]
    winner = min((a, b), key=lambda item: (-item["terminal_score"], item["meter"]))
    status = "candidate_events"
    if winner["terminal_score"] <= 0:
        status = "nonpositive_path"
    elif len(winner["beat_rows"]) < 9:
        status = "insufficient_events"
    result.update(status=status, selected_meter=winner["meter"], terminal_score=winner["terminal_score"],
                  score_difference_4_minus_3=b["terminal_score"] - a["terminal_score"],
                  state_cells=sum(p["state_cells"] for p in (a, b)),
                  maximum_live_state_bytes=max(p["state_bytes"] for p in (a, b)),
                  transitions_examined=sum(p["transitions_examined"] for p in (a, b)))
    for window in result["windows"]:
        window["status"] = status
    if status != "candidate_events":
        return result
    rows, phases = winner["beat_rows"], winner["beat_phases"]
    frames = [refined_frame(qb, r) for r in rows]
    if (len(frames) > 3072 or any(p < 0 or p >= total for p in frames)
            or any(left >= right for left, right in zip(frames, frames[1:]))):
        raise ValueError("refined event geometry")
    result["events"] = {"beat_frames": frames, "beat_rows": rows, "beat_phases": phases,
                        "downbeat_frames": [p for p, k in zip(frames, phases) if k == 0]}
    for window in result["windows"]:
        window["status"] = "no_events"
    for index, (row, frame) in enumerate(zip(rows, frames)):
        owners = [i for i, w in enumerate(windows) if w["first_frame"] <= frame < w["end_frame"]]
        centers = [i for i, w in enumerate(windows) if w["first_frame"] <= row * 960 < w["end_frame"]]
        if len(owners) != 1 or len(centers) != 1:
            raise ValueError("event ownership")
        owner, center = owners[0], centers[0]
        result["windows"][owner]["beat_ordinals"].append(index)
        result["windows"][owner]["status"] = status
        if owner != center:
            result["window_ownership_changes"].append({"ordinal": index, "row_window": center, "refined_window": owner})
    if [i for w in result["windows"] for i in w["beat_ordinals"]] != list(range(len(frames))):
        raise ValueError("window event partition")
    return result


def verify(beat, downbeat, total, windows, candidate, gaps=()):
    expected = reconstruct(beat, downbeat, total, windows, gaps)
    # Canonical JSON comparison also rejects bool/int substitution and extra keys.
    if json.dumps(expected, sort_keys=True, allow_nan=False) != json.dumps(candidate, sort_keys=True, allow_nan=False):
        raise ValueError("independent candidate replay differs")
    return expected


def cohort(candidates, legacy, output):
    from pathlib import Path
    import time
    import apta_1_1_dense_decoder as runner
    import apta_dense_runner_support as io
    import apta_neural_dense as codec
    started = time.monotonic()
    config, bindings = runner.protocol(), {}
    io.external(output, runner.ROOT)
    io.disjoint_output(output, (candidates, legacy))
    revision = io.source_snapshot(runner.ROOT, bindings)
    complete, frozen, ids = runner.validate_frozen(candidates, legacy, config, bindings)
    if revision != complete["source_revision"]:
        raise ValueError("verification revision differs")
    sources, capture = runner.bind_inputs(Path(frozen["dense"]), legacy, config, bindings)
    if ids != [s["track"] for s in sources]:
        raise ValueError("verification trace order differs")
    io.runtime_snapshot(bindings, capture["bindings"])
    output.mkdir(parents=True)
    for source, row in zip(sources, complete["candidates"]):
        candidate_path = candidates / "candidates" / (row["track"] + ".json")
        candidate = io.read_bound(candidate_path, row["sha256"], bindings)
        worker_path = candidates / "jobs" / (row["track"] + ".result.json")
        worker = io.read_bound(worker_path, row["worker_report_sha256"], bindings)
        if candidate != worker["candidate"]:
            raise ValueError("worker candidate differs")
        beat, downbeat = codec.decode(Path(source["payload"]).read_bytes())
        verify(beat, downbeat, source["total_frames"], source["windows"], candidate)
        runner.rss_check(config["limits"]["max_host_rss_kib_per_process"])
        if time.monotonic() - started > config["limits"]["max_verify_process_seconds"]:
            raise ValueError("verification time exceeded")
    io.runtime_recheck(bindings, capture["bindings"])
    io.recheck(bindings)
    io.check_source(runner.ROOT, revision)
    result = {"format": "libapta-dense-independent-verification-1", "status": "PASS",
              "protocol_sha256": PROTOCOL_SHA256, "source_revision": revision,
              "candidate_freeze_sha256": io.sha(candidates / "candidates-frozen.private.json"),
              "verifier_module_sha256": io.sha(Path(__file__)), "sources_verified": len(ids),
              "reference_labels_loaded": False, "bindings": bindings,
              "resources": dict(runner.rss_check(config["limits"]["max_host_rss_kib_per_process"]),
                                wall_seconds=time.monotonic() - started)}
    roots = (output, Path(str(output) + "-supervision"), candidates, Path(str(candidates) + "-supervision"))
    if io.tree_bytes(roots) + io.json_bytes(result) > config["limits"]["max_attempt_output_bytes"]:
        raise ValueError("verification report output cap")
    io.write_new(output / "report.private.json", result)
    return result


def main():
    import argparse
    from pathlib import Path
    import resource
    import sys
    import apta_1_1_dense_decoder as runner
    import apta_dense_runner_support as io
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidates", required=True, type=Path)
    parser.add_argument("--legacy", required=True, type=Path)
    parser.add_argument("--_worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        config = runner.protocol()
        runner.require_preflight()
        limits = config["limits"]
        output = Path(str(args.candidates.resolve()) + "-verification")
        if args._worker:
            ceiling = limits["max_host_rss_kib_per_process"] * 1024
            resource.setrlimit(resource.RLIMIT_AS, (ceiling, ceiling))
            cohort(args.candidates, args.legacy, output)
            return 0
        supervision = Path(str(output) + "-supervision")
        io.external(output, runner.ROOT)
        io.external(supervision, runner.ROOT)
        io.disjoint_output(output, (args.candidates, args.legacy))
        io.disjoint_output(supervision, (output, args.candidates, args.legacy))
        supervision.mkdir(parents=True)
        roots = (output, supervision, args.candidates, Path(str(args.candidates) + "-supervision"))
        result = io.supervise([sys.executable, str(Path(__file__).resolve()),
                               "--candidates", str(args.candidates.resolve()),
                               "--legacy", str(args.legacy.resolve()), "--_worker"],
                              limits["max_verify_process_seconds"], limits["max_host_rss_kib_per_process"],
                              limits["max_attempt_output_bytes"], roots, supervision / "phase")
        report = output / "report.private.json"
        result.update(completion_sha256=io.sha(report), protocol_sha256=PROTOCOL_SHA256)
        if io.tree_bytes(roots) + io.json_bytes(result) > limits["max_attempt_output_bytes"]:
            raise ValueError("verification watchdog output cap")
        io.write_new(supervision / "report.json", result)
        print(json.dumps({"status": "PASS", "sources_verified": config["inputs"]["sources"]}))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(json.dumps({"status": "INVALID", "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    import sys
    sys.exit(main())
