#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pure registered dense decoder: generated tests only until full preflight."""
from array import array
from fractions import Fraction
import math

PROTOCOL_SHA256 = "5dc1241087a08663b06766f91faade5a5758f9791fd85e0077c62e78d5a186fd"
NAME = "dense-joint-renewal-v1"
FORMAT = "libapta-dense-decoder-candidate-1"
RATE = 48000
HOP = 960
MAXIMUM_SOURCE_FRAMES = 1440000
MAXIMUM_ROWS = 1501
MAXIMUM_WINDOWS = 8
MAXIMUM_EVENTS = 3072
MINIMUM_INTERVAL = 10
MAXIMUM_INTERVAL = 75
INTERVALS = 66
MAXIMUM_INTERVAL_CHANGE = 2
LOGIT_CLIP = 32
LOGIT_SCALE = 1024
TRANSITION_PENALTY = 2048
MINIMUM_EVENTS = 9
MAXIMUM_STATE_BYTES = 3566376
METERS = (3, 4)
UNREACHABLE = -(1 << 62)
PARENT_UNREACHABLE = 255
SEED = 0


def quantize(value):
    """Validate exact finite f32, then clip and round rationally, away at ties."""
    if type(value) not in (float, int):
        raise ValueError("logit must be an exact finite f32")
    try:
        if not math.isfinite(value):
            raise ValueError("nonfinite logit")
        single = array("f", [value])
        if single.itemsize != 4 or single[0] != value:
            raise ValueError("logit is not exact f32")
    except (OverflowError, TypeError) as exc:
        raise ValueError("logit is not exact finite f32") from exc
    clipped = min(LOGIT_CLIP, max(-LOGIT_CLIP, value))
    numerator, denominator = abs(clipped).as_integer_ratio()
    magnitude = (2 * numerator * LOGIT_SCALE + denominator) // (2 * denominator)
    return -magnitude if clipped < 0 else magnitude


def round_frame(value):
    """Nearest whole source frame, ties upward."""
    value = Fraction(value) + Fraction(1, 2)
    return value.numerator // value.denominator


def geometry(total, rows, windows, gaps):
    if (type(total) is not int or not 0 < total <= MAXIMUM_SOURCE_FRAMES
            or type(rows) is not int or not 0 < rows <= MAXIMUM_ROWS):
        raise ValueError("source/row capacity invalid")
    low = total * 22050 // RATE
    high = (total * 22050 + RATE - 1) // RATE
    if rows not in (low // 441 + 1, high // 441 + 1):
        raise ValueError("source/resampled row geometry differs")
    if not isinstance(windows, (list, tuple)) or not 0 < len(windows) <= MAXIMUM_WINDOWS:
        raise ValueError("invalid window count")
    previous = 0
    for window in windows:
        if not isinstance(window, dict) or set(window) != {"first_frame", "end_frame"}:
            raise ValueError("plain source geometry only")
        first, end = window["first_frame"], window["end_frame"]
        if (type(first) is not int or type(end) is not int
                or first != previous or not first < end <= total):
            raise ValueError("window partition differs")
        previous = end
    if previous != total:
        raise ValueError("source tail missing")
    if not isinstance(gaps, (list, tuple)):
        raise ValueError("invalid known gaps")
    previous = 0
    for gap in gaps:
        if not isinstance(gap, (list, tuple)) or len(gap) != 2:
            raise ValueError("invalid known gap")
        first, end = gap
        if (type(first) is not int or type(end) is not int
                or not previous <= first < end <= total):
            raise ValueError("known gaps unordered or outside source")
        previous = end
    return min(rows, (total + HOP - 1) // HOP)


def path_score(qb, qd, total, meter, rows, intervals, phases):
    """Check the reconstructed path independently of its table/parent indices."""
    if meter not in METERS or not rows or not len(rows) == len(intervals) == len(phases):
        raise ValueError("invalid path shape")
    if len(rows) > MAXIMUM_ROWS:
        raise ValueError("traceback cycle or capacity")
    score = 0
    for i, (t, d, k) in enumerate(zip(rows, intervals, phases)):
        if (type(t) is not int or type(d) is not int or type(k) is not int
                or not 0 <= t < len(qb) or t * HOP >= total
                or not MINIMUM_INTERVAL <= d <= MAXIMUM_INTERVAL or not 0 <= k < meter):
            raise ValueError("invalid reconstructed state")
        if i == 0:
            if not t < d:
                raise ValueError("path does not start at seed")
        else:
            if (t - rows[i - 1] != d or (phases[i - 1] + 1) % meter != k
                    or abs(d - intervals[i - 1]) > MAXIMUM_INTERVAL_CHANGE):
                raise ValueError("invalid path transition")
            score -= TRANSITION_PENALTY * (d - intervals[i - 1]) ** 2
        score += qb[t] + (qd[t] if k == 0 else 0)
    if (rows[-1] + intervals[-1]) * HOP < total:
        raise ValueError("path ends before source tail")
    return score


def solve_meter(qb, qd, total, meter):
    """Pre-abstention optimum for one meter; inputs are bounded quantized logits."""
    if (meter not in METERS or type(meter) is not int or type(total) is not int
            or not 0 < total <= MAXIMUM_SOURCE_FRAMES
            or not 0 < len(qb) == len(qd) <= MAXIMUM_ROWS
            or len(qb) != (total + HOP - 1) // HOP
            or any(type(x) is not int or abs(x) > LOGIT_CLIP * LOGIT_SCALE for x in (*qb, *qd))):
        raise ValueError("invalid quantized DP domain")
    stride = INTERVALS * meter
    cells = len(qb) * stride
    scores = array("q", [UNREACHABLE]) * cells
    if scores.itemsize != 8 or cells * 9 > MAXIMUM_STATE_BYTES:
        raise ValueError("state layout differs from registration")
    parents = bytearray([PARENT_UNREACHABLE]) * cells
    best, terminal, transitions = UNREACHABLE, None, 0
    for t in range(len(qb)):
        for d in range(MINIMUM_INTERVAL, MAXIMUM_INTERVAL + 1):
            base = t * stride + (d - MINIMUM_INTERVAL) * meter
            terminal_allowed = (t + d) * HOP >= total
            for k in range(meter):
                emission = qb[t] + (qd[t] if k == 0 else 0)
                value, parent = UNREACHABLE, PARENT_UNREACHABLE
                if t < d:
                    value, parent = emission, SEED
                else:
                    old_phase = (k - 1) % meter
                    old_base = (t - d) * stride + old_phase
                    for p in range(max(MINIMUM_INTERVAL, d - MAXIMUM_INTERVAL_CHANGE),
                                   min(MAXIMUM_INTERVAL, d + MAXIMUM_INTERVAL_CHANGE) + 1):
                        transitions += 1
                        old = scores[old_base + (p - MINIMUM_INTERVAL) * meter]
                        if old == UNREACHABLE:
                            continue
                        candidate = old + emission - TRANSITION_PENALTY * (d - p) ** 2
                        if candidate > value:
                            value, parent = candidate, p
                scores[base + k], parents[base + k] = value, parent
                # Traversal is lexicographic (t,d,k); never replace an equal terminal.
                if terminal_allowed and value > best:
                    best, terminal = value, (t, d, k)
    if terminal is None:
        raise ValueError("no terminal in valid domain")
    rows, intervals, phases = [], [], []
    t, d, k = terminal
    for _ in range(MAXIMUM_ROWS):
        index = t * stride + (d - MINIMUM_INTERVAL) * meter + k
        p = parents[index]
        if p == PARENT_UNREACHABLE:
            raise ValueError("traceback entered unreachable state")
        rows.append(t)
        intervals.append(d)
        phases.append(k)
        if p == SEED:
            break
        if not MINIMUM_INTERVAL <= p <= MAXIMUM_INTERVAL or t - d < 0:
            raise ValueError("invalid predecessor")
        t, d, k = t - d, p, (k - 1) % meter
    else:
        raise ValueError("traceback exceeds row capacity")
    rows.reverse()
    intervals.reverse()
    phases.reverse()
    if path_score(qb, qd, total, meter, rows, intervals, phases) != best:
        raise ValueError("path score differs from terminal")
    return {"meter": meter, "terminal_score": best,
            "terminal_state": {"row": terminal[0], "interval_rows": terminal[1], "phase": terminal[2]},
            "beat_rows": rows, "incoming_intervals": intervals, "beat_phases": phases,
            "state_cells": cells, "state_bytes": cells * 9, "transitions_examined": transitions}


def localize(qb, row):
    if type(row) is not int or not 0 <= row < len(qb):
        raise ValueError("invalid localization row")
    delta = Fraction(0)
    if 0 < row < len(qb) - 1:
        left, center, right = qb[row - 1:row + 2]
        if center > left and center > right:
            delta = Fraction(left - right, 2 * (left - 2 * center + right))
            delta = min(Fraction(1, 2), max(-Fraction(1, 2), delta))
    return round_frame(HOP * (row + delta))


def window_index(frame, windows):
    for i, window in enumerate(windows):
        if window["first_frame"] <= frame < window["end_frame"]:
            return i
    raise ValueError("event outside window partition")


def construct(beat_logits, downbeat_logits, total_frames, windows, known_gaps=()):
    """Decode once using model evidence and geometry only. No I/O or labels."""
    if (not isinstance(beat_logits, (list, tuple, array))
            or not isinstance(downbeat_logits, (list, tuple, array))
            or len(beat_logits) != len(downbeat_logits)):
        raise ValueError("invalid paired logit arrays")
    domain = geometry(total_frames, len(beat_logits), windows, known_gaps)
    qb_all = [quantize(x) for x in beat_logits]
    qd_all = [quantize(x) for x in downbeat_logits]
    qb, qd = qb_all[:domain], qd_all[:domain]
    result = {"format": FORMAT, "decoder_name": NAME, "total_frames": total_frames,
              "status": "known_pcm_gap", "selected_meter": None, "terminal_score": None,
              "confidence": 0, "confidence_calibrated": False,
              "per_meter_diagnostics": [], "score_difference_4_minus_3": None,
              "events": {"beat_frames": [], "downbeat_frames": [], "beat_rows": [], "beat_phases": []},
              "windows": [dict(w, status="known_pcm_gap", beat_ordinals=[]) for w in windows],
              "known_pcm_gaps": [list(g) for g in known_gaps],
              "boundary_rows_excluded": list(range(domain, len(beat_logits))),
              "clipped_logits": {"beat": sum(abs(x) > LOGIT_CLIP for x in beat_logits),
                                 "downbeat": sum(abs(x) > LOGIT_CLIP for x in downbeat_logits)},
              "window_ownership_changes": [], "state_cells": 0,
              "maximum_live_state_bytes": 0, "transitions_examined": 0,
              "native_graph_constructed": False, "production_promotion": False}
    if known_gaps:
        return result
    for meter in METERS:
        # solve_meter owns/releases its own typed arrays. Retain only bounded paths.
        result["per_meter_diagnostics"].append(solve_meter(qb, qd, total_frames, meter))
    a, b = result["per_meter_diagnostics"]
    winner = b if b["terminal_score"] > a["terminal_score"] else a
    result.update(selected_meter=winner["meter"], terminal_score=winner["terminal_score"],
                  score_difference_4_minus_3=b["terminal_score"] - a["terminal_score"],
                  state_cells=a["state_cells"] + b["state_cells"],
                  maximum_live_state_bytes=max(a["state_bytes"], b["state_bytes"]),
                  transitions_examined=a["transitions_examined"] + b["transitions_examined"])
    status = ("nonpositive_path" if winner["terminal_score"] <= 0 else
              "insufficient_events" if len(winner["beat_rows"]) < MINIMUM_EVENTS else "candidate_events")
    result["status"] = status
    for window in result["windows"]:
        window["status"] = status
    if status != "candidate_events":
        return result
    rows, phases = winner["beat_rows"], winner["beat_phases"]
    frames = [localize(qb, t) for t in rows]
    if (len(frames) > MAXIMUM_EVENTS or any(not 0 <= p < total_frames for p in frames)
            or any(a >= b for a, b in zip(frames, frames[1:]))):
        raise ValueError("invalid localized events")
    result["events"] = {"beat_frames": frames, "beat_rows": list(rows), "beat_phases": list(phases),
                        "downbeat_frames": [p for p, k in zip(frames, phases) if k == 0]}
    for window in result["windows"]:
        window["status"] = "no_events"
    for ordinal, (row, frame) in enumerate(zip(rows, frames)):
        old, new = window_index(row * HOP, windows), window_index(frame, windows)
        result["windows"][new]["beat_ordinals"].append(ordinal)
        result["windows"][new]["status"] = "candidate_events"
        if old != new:
            result["window_ownership_changes"].append({"ordinal": ordinal, "row_window": old,
                                                       "refined_window": new})
    if [i for w in result["windows"] for i in w["beat_ordinals"]] != list(range(len(frames))):
        raise ValueError("event window partition differs")
    return result
