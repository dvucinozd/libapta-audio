#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent CT-00 binary64 replay; memory-only API, no real-input CLI."""
from array import array
import hashlib
import json
import math
import sys

PROTOCOL_SHA256 = "5f23a96330e7852f9dcb21b6273acf258e7252177da9bef53ceb9e3bea1afced"


def _finite(*numbers):
    if any(not math.isfinite(number) for number in numbers):
        raise ValueError("nonfinite_numeric")


def _inputs(beats, downbeats, total, windows, gaps):
    if type(total) is not int or total < 1 or total > 1440000:
        raise ValueError("invalid_input")
    for values, end in ((beats, total), (downbeats, len(beats) if isinstance(beats, (list, tuple, array)) else 0)):
        if not isinstance(values, (list, tuple, array)) or len(values) > 3072:
            raise ValueError("invalid_input")
        previous = -1
        for value in values:
            if type(value) is not int or value <= previous or value >= end:
                raise ValueError("invalid_input")
            previous = value
    if not isinstance(windows, (list, tuple)) or len(windows) not in range(1, 9):
        raise ValueError("invalid_input")
    cursor = 0
    for window in windows:
        if not isinstance(window, dict) or set(window) != {"first_frame", "end_frame"}:
            raise ValueError("invalid_input")
        start, stop = window["first_frame"], window["end_frame"]
        if type(start) is not int or type(stop) is not int or start != cursor or not start < stop <= total:
            raise ValueError("invalid_input")
        cursor = stop
    if cursor != total or not isinstance(gaps, (list, tuple)):
        raise ValueError("invalid_input")
    cursor = 0
    for gap in gaps:
        if not isinstance(gap, (list, tuple)) or len(gap) != 2:
            raise ValueError("invalid_input")
        start, stop = gap
        if (type(start) is not int or type(stop) is not int
                or start < cursor or not 0 <= start < stop <= total):
            raise ValueError("invalid_input")
        cursor = stop
    if sys.float_info.mant_dig != 53 or sys.float_info.max_exp != 1024 or array("d").itemsize != 8:
        raise ValueError("unsupported_float64_runtime")


def coordinate(index, observed, points, lows, highs):
    """Own scalar derivative assembly; no production helper or matrix cache."""
    diagonal, linear = 1.0, -float(observed[index])
    center = max(1, index - 1)
    last = min(len(points) - 2, index + 1)
    while center <= last:
        own_coefficient = -2.0 if center == index else 1.0
        other_terms = 0.0
        for position in (center - 1, center, center + 1):
            if position != index:
                coefficient = -2.0 if position == center else 1.0
                product = coefficient * points[position]
                other_terms = other_terms + product
                _finite(product, other_terms)
        weighted = 16.0 * own_coefficient
        diagonal_term = weighted * own_coefficient
        diagonal = diagonal + diagonal_term
        linear_term = weighted * other_terms
        linear = linear + linear_term
        _finite(weighted, diagonal_term, diagonal, linear_term, linear)
        center += 1
    answer = (-linear) / diagonal
    _finite(answer)
    if answer < float(lows[index]):
        answer = float(lows[index])
    elif answer > float(highs[index]):
        answer = float(highs[index])
    return answer


def residual(observed, points, lows, highs):
    maximum = 0.0
    for index in range(len(points)):
        root = coordinate(index, observed, points, lows, highs)
        difference = abs(points[index] - root)
        _finite(root, difference)
        if difference > maximum:
            maximum = difference
    return maximum


def objective(observed, points):
    data = 0.0
    for index, value in enumerate(points):
        offset = value - float(observed[index])
        square = offset * offset
        data = data + square
        _finite(offset, square, data)
    curvature = 0.0
    for center in range(1, len(points) - 1):
        doubled = 2.0 * points[center]
        difference = points[center - 1] - doubled
        _finite(doubled, difference)
        difference = difference + points[center + 1]
        square = difference * difference
        curvature = curvature + square
        _finite(difference, square, curvature)
    penalty = 16.0 * curvature
    value = data + penalty
    _finite(penalty, value)
    if value < 0.0:
        raise ValueError("invalid_output")
    return value


def rounded(value):
    _finite(value)
    shifted = value + 0.5
    return math.floor(shifted)


def replay_solution(observations, lows, highs):
    """Independent fixed-order solver primitive, also usable by tiny tests."""
    if not isinstance(observations, (list, tuple, array)) or not 1 <= len(observations) <= 3072:
        raise ValueError("invalid_input")
    if (not isinstance(lows, (list, tuple, array)) or not isinstance(highs, (list, tuple, array))
            or len(lows) != len(observations) or len(highs) != len(observations)):
        raise ValueError("invalid_input")
    previous = -1
    for index, value in enumerate(observations):
        if (type(value) is not int or not previous < value < 1440000
                or type(lows[index]) is not int or type(highs[index]) is not int
                or not 0 <= lows[index] <= value <= highs[index] < 1440000):
            raise ValueError("invalid_input")
        previous = value
    if sys.float_info.mant_dig != 53 or sys.float_info.max_exp != 1024 or array("d").itemsize != 8:
        raise ValueError("unsupported_float64_runtime")
    points = array("d", observations)
    sweep = 0
    error = residual(observations, points, lows, highs)
    while error > 1e-6:
        if sweep >= 4096:
            raise ValueError("nonconvergence")
        for index in range(len(points)):
            points[index] = coordinate(index, observations, points, lows, highs)
            _finite(points[index])
        for index in range(len(points) - 1, -1, -1):
            points[index] = coordinate(index, observations, points, lows, highs)
            _finite(points[index])
        sweep += 1
        error = residual(observations, points, lows, highs)
    count = len(points)
    return {"coordinates_hex": [value.hex() for value in points],
            "lower_bounds": list(lows), "upper_bounds": list(highs),
            "sweeps": sweep, "root_evaluations": count * (3 * sweep + 1),
            "coordinate_updates": 2 * count * sweep, "residual_frames": error,
            "objective_frames_squared": objective(observations, points),
            "active_lower_ordinals": [i for i in range(count) if points[i] == float(lows[i])],
            "active_upper_ordinals": [i for i in range(count) if points[i] == float(highs[i])]}


def reconstruct(beats, downbeats, total, windows, known_gaps=()):
    _inputs(beats, downbeats, total, windows, known_gaps)
    count = len(beats)
    status = "candidate_events"
    if known_gaps:
        status = "known_pcm_gap"
    elif count < 9:
        status = "insufficient_events"
    solution = {"coordinates_hex": [], "lower_bounds": [], "upper_bounds": [],
                "sweeps": 0, "root_evaluations": 0, "coordinate_updates": 0,
                "residual_frames": None, "objective_frames_squared": None,
                "active_lower_ordinals": [], "active_upper_ordinals": []}
    frames, changes = [], []
    if status == "candidate_events":
        lows, highs = [], []
        for index, value in enumerate(beats):
            neighbors = []
            if index > 0:
                neighbors.append(value - beats[index - 1])
            if index < count - 1:
                neighbors.append(beats[index + 1] - value)
            radius = min(960, min(neighbors) // 4)
            lows.append(max(0, value - radius))
            highs.append(min(total - 1, value + radius))
        if any(highs[i] >= lows[i + 1] for i in range(count - 1)):
            raise ValueError("invalid_output")
        solution = replay_solution(beats, lows, highs)
        frames = [rounded(float.fromhex(value)) for value in solution["coordinates_hex"]]
        if any(not lows[i] <= frames[i] <= highs[i] for i in range(count)):
            raise ValueError("invalid_output")
        if any(left >= right for left, right in zip(frames, frames[1:])):
            raise ValueError("invalid_output")
        for index, value in enumerate(beats):
            old = next(i for i, window in enumerate(windows) if window["first_frame"] <= value < window["end_frame"])
            new = next(i for i, window in enumerate(windows) if window["first_frame"] <= frames[index] < window["end_frame"])
            if old != new:
                changes.append({"ordinal": index, "observed_window": old, "final_window": new})
    mapped_windows = []
    for window in windows:
        members = [i for i, value in enumerate(frames) if window["first_frame"] <= value < window["end_frame"]]
        mapped_windows.append({"first_frame": window["first_frame"], "end_frame": window["end_frame"],
                               "status": ("candidate_events" if members else "no_events")
                               if status == "candidate_events" else status, "beat_ordinals": members})
    return {"format": "libapta-coupled-time-candidate-1", "hypothesis_name": "coupled-neural-event-time-v1",
            "protocol_sha256": PROTOCOL_SHA256, "total_frames": total, "input_event_count": count,
            "status": status, "confidence": 0, "confidence_calibrated": False,
            "events": {"beat_frames": frames, "beat_ordinals": list(range(len(frames))),
                       "downbeat_frames": [frames[i] for i in downbeats] if frames else [],
                       "downbeat_ordinals": list(downbeats) if frames else []},
            "solution": solution, "windows": mapped_windows,
            "known_pcm_gaps": [list(gap) for gap in known_gaps], "window_ownership_changes": changes,
            "native_graph_constructed": False, "physical_p4": False,
            "formal_acceptance": False, "production_promotion": False}


def _identical(saved, expected):
    if type(saved) is not type(expected):
        raise ValueError("candidate type differs")
    if type(expected) is dict:
        if saved.keys() != expected.keys():
            raise ValueError("candidate fields differ")
        for key in expected:
            _identical(saved[key], expected[key])
    elif type(expected) is list:
        if len(saved) != len(expected):
            raise ValueError("candidate length differs")
        for left, right in zip(saved, expected):
            _identical(left, right)
    elif type(expected) is float:
        if not math.isfinite(saved) or saved.hex() != expected.hex():
            raise ValueError("candidate float differs")
    elif saved != expected:
        raise ValueError("candidate value differs")


def verify(beats, downbeats, total, windows, candidate, known_gaps=()):
    expected = reconstruct(beats, downbeats, total, windows, known_gaps)
    _identical(candidate, expected)
    return expected


def canonical_bytes(value):
    """CT-01 compact sorted JSON convention; exact bytes, without a newline."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def _pairs(items):
    value = {}
    for key, item in items:
        if key in value:
            raise ValueError("duplicate JSON field")
        value[key] = item
    return value


def _constant(_name):
    raise ValueError("nonfinite JSON number")


def verify_bytes(beats, downbeats, total, windows, payload, expected_sha256, known_gaps=()):
    if type(payload) is not bytes or len(payload) > 67108864:
        raise ValueError("candidate byte capacity differs")
    if type(expected_sha256) is not str or hashlib.sha256(payload).hexdigest() != expected_sha256:
        raise ValueError("candidate hash differs")
    try:
        saved = json.loads(payload.decode("utf-8"), object_pairs_hook=_pairs, parse_constant=_constant)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError("invalid JSON encoding") from exc
    expected = verify(beats, downbeats, total, windows, saved, known_gaps)
    if payload != canonical_bytes(expected):
        raise ValueError("candidate bytes differ")
    return expected
