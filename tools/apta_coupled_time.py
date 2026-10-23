#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pure CT-00 timing contract; synthetic-only, without a real-data CLI."""
from array import array
import math
import sys

PROTOCOL_SHA256 = "5f23a96330e7852f9dcb21b6273acf258e7252177da9bef53ceb9e3bea1afced"
FORMAT = "libapta-coupled-time-candidate-1"
NAME = "coupled-neural-event-time-v1"
MAXIMUM_SOURCE_FRAMES = 1440000
MAXIMUM_EVENTS = 3072
MAXIMUM_WINDOWS = 8
MINIMUM_EVENTS = 9
MAXIMUM_RADIUS = 960
MAXIMUM_SWEEPS = 4096
RESIDUAL_LIMIT = 1e-6


def _runtime():
    if (sys.float_info.mant_dig != 53 or sys.float_info.max_exp != 1024
            or array("d").itemsize != 8):
        raise ValueError("unsupported_float64_runtime")


def _sequence(value):
    return isinstance(value, (list, tuple, array))


def _observations(values, maximum, minimum):
    if not _sequence(values) or not minimum <= len(values) <= MAXIMUM_EVENTS:
        raise ValueError("invalid_input")
    previous = -1
    for value in values:
        if type(value) is not int or not previous < value < maximum:
            raise ValueError("invalid_input")
        previous = value


def _geometry(total, windows, gaps):
    if type(total) is not int or not 1 <= total <= MAXIMUM_SOURCE_FRAMES:
        raise ValueError("invalid_input")
    if not isinstance(windows, (list, tuple)) or not 1 <= len(windows) <= MAXIMUM_WINDOWS:
        raise ValueError("invalid_input")
    previous = 0
    for window in windows:
        if not isinstance(window, dict) or set(window) != {"first_frame", "end_frame"}:
            raise ValueError("invalid_input")
        first, end = window["first_frame"], window["end_frame"]
        if (type(first) is not int or type(end) is not int
                or first != previous or not first < end <= total):
            raise ValueError("invalid_input")
        previous = end
    if previous != total or not isinstance(gaps, (list, tuple)):
        raise ValueError("invalid_input")
    previous = 0
    for gap in gaps:
        if not isinstance(gap, (list, tuple)) or len(gap) != 2:
            raise ValueError("invalid_input")
        first, end = gap
        if (type(first) is not int or type(end) is not int
                or not previous <= first < end <= total):
            raise ValueError("invalid_input")
        previous = end


def _root(i, a, x, lower, upper):
    """One clipped coordinate minimizer, in the registered binary64 order."""
    A = 1.0
    B = -float(a[i])
    for j in range(max(1, i - 1), min(len(x) - 2, i + 1) + 1):
        b = -2.0 if i == j else 1.0
        without = 0.0
        for k in range(j - 1, j + 2):
            if k == i:
                continue
            coefficient = -2.0 if k == j else 1.0
            term = coefficient * x[k]
            without = without + term
            if not math.isfinite(term) or not math.isfinite(without):
                raise ValueError("nonfinite_numeric")
        t = 16.0 * b
        aa = t * b
        A = A + aa
        bb = t * without
        B = B + bb
        if not all(math.isfinite(v) for v in (t, aa, A, bb, B)):
            raise ValueError("nonfinite_numeric")
    u = (-B) / A
    if not math.isfinite(u):
        raise ValueError("nonfinite_numeric")
    if u < lower[i]:
        return lower[i]
    if u > upper[i]:
        return upper[i]
    return u


def _residual(a, x, lower, upper):
    residual = 0.0
    for i in range(len(x)):
        root = _root(i, a, x, lower, upper)
        difference = abs(x[i] - root)
        if not math.isfinite(root) or not math.isfinite(difference):
            raise ValueError("nonfinite_numeric")
        if difference > residual:
            residual = difference
    return residual


def _objective(a, x):
    data = 0.0
    for i in range(len(x)):
        difference = x[i] - float(a[i])
        square = difference * difference
        data = data + square
        if not all(math.isfinite(v) for v in (difference, square, data)):
            raise ValueError("nonfinite_numeric")
    smooth = 0.0
    for j in range(1, len(x) - 1):
        t = 2.0 * x[j]
        c = x[j - 1] - t
        c = c + x[j + 1]
        square = c * c
        smooth = smooth + square
        if not all(math.isfinite(v) for v in (t, c, square, smooth)):
            raise ValueError("nonfinite_numeric")
    penalty = 16.0 * smooth
    objective = data + penalty
    if not math.isfinite(penalty) or not math.isfinite(objective):
        raise ValueError("nonfinite_numeric")
    if objective < 0.0:
        raise ValueError("invalid_output")
    return objective


def solve_times(observations, lower_bounds, upper_bounds):
    """Solve explicit boxes, including tiny oracle cases; emit no beat candidate."""
    _observations(observations, MAXIMUM_SOURCE_FRAMES, 1)
    n = len(observations)
    if (not _sequence(lower_bounds) or not _sequence(upper_bounds)
            or len(lower_bounds) != n or len(upper_bounds) != n):
        raise ValueError("invalid_input")
    for i, observed in enumerate(observations):
        low, high = lower_bounds[i], upper_bounds[i]
        if (type(low) is not int or type(high) is not int
                or not 0 <= low <= observed <= high < MAXIMUM_SOURCE_FRAMES):
            raise ValueError("invalid_input")
    _runtime()
    # Four linear double vectors; dense systems belong only in the tiny test oracle.
    a = array("d", observations)
    x = array("d", a)
    lower = array("d", lower_bounds)
    upper = array("d", upper_bounds)
    sweeps = 0
    residual = _residual(a, x, lower, upper)
    while residual > RESIDUAL_LIMIT:
        if sweeps == MAXIMUM_SWEEPS:
            raise ValueError("nonconvergence")
        for indices in (range(n), range(n - 1, -1, -1)):
            for i in indices:
                value = _root(i, a, x, lower, upper)
                if not math.isfinite(value):
                    raise ValueError("nonfinite_numeric")
                x[i] = value
        sweeps += 1
        residual = _residual(a, x, lower, upper)
    return {
        "coordinates_hex": [v.hex() for v in x],
        "lower_bounds": list(lower_bounds), "upper_bounds": list(upper_bounds),
        "sweeps": sweeps, "root_evaluations": n * (3 * sweeps + 1),
        "coordinate_updates": 2 * n * sweeps, "residual_frames": residual,
        "objective_frames_squared": _objective(a, x),
        "active_lower_ordinals": [i for i in range(n) if x[i] == lower[i]],
        "active_upper_ordinals": [i for i in range(n) if x[i] == upper[i]],
    }


def _round_frame(value):
    if not math.isfinite(value):
        raise ValueError("nonfinite_numeric")
    shifted = value + 0.5
    return math.floor(shifted)


def _empty_solution():
    return {
        "coordinates_hex": [], "lower_bounds": [], "upper_bounds": [],
        "sweeps": 0, "root_evaluations": 0, "coordinate_updates": 0,
        "residual_frames": None, "objective_frames_squared": None,
        "active_lower_ordinals": [], "active_upper_ordinals": [],
    }


def construct(beat_frames, downbeat_ordinals, total_frames, windows, known_gaps=()):
    """Construct one source-wide candidate, preserving neural event topology."""
    _geometry(total_frames, windows, known_gaps)
    _observations(beat_frames, total_frames, 0)
    _observations(downbeat_ordinals, len(beat_frames), 0)
    _runtime()
    n = len(beat_frames)
    status = ("known_pcm_gap" if known_gaps else
              "insufficient_events" if n < MINIMUM_EVENTS else "candidate_events")
    result = {
        "format": FORMAT, "hypothesis_name": NAME, "protocol_sha256": PROTOCOL_SHA256,
        "total_frames": total_frames, "input_event_count": n, "status": status,
        "confidence": 0, "confidence_calibrated": False,
        "events": {"beat_frames": [], "beat_ordinals": [],
                   "downbeat_frames": [], "downbeat_ordinals": []},
        "solution": _empty_solution(),
        "windows": [{"first_frame": w["first_frame"], "end_frame": w["end_frame"],
                     "status": status, "beat_ordinals": []} for w in windows],
        "known_pcm_gaps": [list(g) for g in known_gaps], "window_ownership_changes": [],
        "native_graph_constructed": False, "physical_p4": False,
        "formal_acceptance": False, "production_promotion": False,
    }
    if status != "candidate_events":
        return result
    lower, upper = [], []
    for i, value in enumerate(beat_frames):
        gap = (beat_frames[1] - value if i == 0 else
               value - beat_frames[i - 1] if i == n - 1 else
               min(value - beat_frames[i - 1], beat_frames[i + 1] - value))
        radius = min(MAXIMUM_RADIUS, gap // 4)
        lower.append(max(0, value - radius))
        upper.append(min(total_frames - 1, value + radius))
    if any(upper[i] >= lower[i + 1] for i in range(n - 1)):
        raise ValueError("invalid_output")
    solution = solve_times(beat_frames, lower, upper)
    frames = [_round_frame(float.fromhex(v)) for v in solution["coordinates_hex"]]
    if (len(frames) != n or any(type(v) is not int or not lower[i] <= v <= upper[i]
                               for i, v in enumerate(frames))
            or any(frames[i] >= frames[i + 1] for i in range(n - 1))):
        raise ValueError("invalid_output")
    result["solution"] = solution
    result["events"] = {"beat_frames": frames, "beat_ordinals": list(range(n)),
                        "downbeat_frames": [frames[i] for i in downbeat_ordinals],
                        "downbeat_ordinals": list(downbeat_ordinals)}
    observed_window = final_window = 0
    for i, observed in enumerate(beat_frames):
        while observed >= windows[observed_window]["end_frame"]:
            observed_window += 1
        while frames[i] >= windows[final_window]["end_frame"]:
            final_window += 1
        result["windows"][final_window]["beat_ordinals"].append(i)
        if observed_window != final_window:
            result["window_ownership_changes"].append({
                "ordinal": i, "observed_window": observed_window, "final_window": final_window})
    for window in result["windows"]:
        if not window["beat_ordinals"]:
            window["status"] = "no_events"
    return result
