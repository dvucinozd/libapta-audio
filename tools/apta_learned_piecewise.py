#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pure label-blind constructor. No files, scoring, corpus or reference imports."""
from fractions import Fraction

Q = 1 << 32
RATE = 48000
MAXIMUM_SOURCE_FRAMES = 1440000
MAXIMUM_EVENTS = 3072
MAXIMUM_WINDOWS = 8
MAXIMUM_PIECES = 8
MINIMUM_PIECE_EVENTS = 4
MAXIMUM_RESIDUAL_FRAMES = 960
MAXIMUM_SEAM_ERROR_FRAMES = 1920


def rounded(value):
    value = Fraction(value) + Fraction(1, 2)
    return value.numerator // value.denominator


def events_valid(events, total):
    if (not isinstance(events, list) or len(events) > MAXIMUM_EVENTS
            or any(type(p) is not int or not 0 <= p < total for p in events)
            or any(a >= b for a, b in zip(events, events[1:]))):
        raise ValueError("invalid bounded event sequence")


def expand(pieces):
    return [(piece["anchor_frame"] * Q + j * piece["period_q32"], piece["start_ordinal"] + j,
             piece["period_q32"]) for piece in pieces for j in range(piece["beat_count"])]


def compatible(left, right):
    a, ordinal, period = left
    b, next_ordinal, _ = right
    return b > a and next_ordinal == ordinal + 1 and abs(b - a - period) <= MAXIMUM_SEAM_ERROR_FRAMES * Q


def fit(beats, start, end, boundaries, base):
    n = end - start
    if n < MINIMUM_PIECE_EVENTS:
        return None
    values = beats[start:end]
    sx, sxx = n * (n - 1) // 2, n * (n - 1) * (2 * n - 1) // 6
    period = Fraction(n * sum(i * p for i, p in enumerate(values)) - sx * sum(values), n * sxx - sx * sx)
    tempo = rounded(Fraction(RATE * 60000) / period)
    if not 40000 <= tempo <= 300000:
        return None
    anchor = rounded(Fraction(sum(values), n) - period * Fraction(sx, n))
    published = (RATE * 60000 * Q) // tempo
    first, stop = boundaries[start - base], boundaries[end - base]
    if anchor * Q - published >= first * Q or anchor * Q + n * published < stop * Q:
        return None
    residuals = []
    for i, event in enumerate(values):
        point = anchor * Q + i * published
        index = start - base + i
        residual = point - event * Q
        if (not boundaries[index] * Q <= point < boundaries[index + 1] * Q
                or abs(residual) > MAXIMUM_RESIDUAL_FRAMES * Q):
            return None
        residuals.append(residual)
    return {"start_ordinal": start, "end_ordinal": end, "beat_count": n,
            "first_frame": first, "end_frame": stop, "anchor_frame": anchor,
            "tempo_millibpm": tempo, "period_q32": published,
            "squared_residual_q64": sum(r * r for r in residuals), "confidence": 0}


def path_key(path):
    return (len(path), sum(p["squared_residual_q64"] for p in path), tuple(-p["end_ordinal"] for p in path))


def window_plan(beats, downbeats, first, end):
    indices = [i for i, p in enumerate(beats) if first <= p < end]
    result = {"first_frame": first, "end_frame": end, "status": "insufficient_events", "pieces": [], "meter": {}}
    if len(indices) < MINIMUM_PIECE_EVENTS:
        return result
    a, b = indices[0], indices[-1] + 1
    boundaries = [first] + [(beats[i - 1] + beats[i]) // 2 + 1 for i in range(a + 1, b)] + [end]
    by_end, states = {}, {}
    for stop in range(a + MINIMUM_PIECE_EVENTS, b + 1):
        for start in range(a, stop - MINIMUM_PIECE_EVENTS + 1):
            piece = fit(beats, start, stop, boundaries, a)
            if piece is None:
                continue
            first_point, last_point = expand([piece])[0], expand([piece])[-1]
            possibilities = [[piece]] if start == a else []
            for previous in by_end.get(start, []):
                if compatible(states[previous][1], first_point):
                    possibilities.append(states[previous][0] + [piece])
            if possibilities:
                path = min(possibilities, key=path_key)
                key = (start, stop)
                states[key] = (path, last_point)
                by_end.setdefault(stop, []).append(key)
    paths = [states[key][0] for key in by_end.get(b, [])]
    if not paths:
        result["status"] = "no_piecewise_path"
        return result
    result.update(status="accepted", pieces=min(paths, key=path_key), anchor_ordinal=a, event_count=b - a)
    points = expand(result["pieces"])
    index = {p: i for i, p in enumerate(beats)}
    db = [p for p in downbeats if first <= p < end]
    for left, middle, right in zip(db, db[1:], db[2:]):
        numerator = index[middle] - index[left]
        if numerator in (3, 4) and index[right] - index[middle] == numerator:
            ordinal = index[left]
            position = points[ordinal - a][0]
            result["meter"] = {"kind": "estimated", "numerator": numerator, "denominator": 4,
                "downbeat_frame": position // Q, "downbeat_fraction_q32": position % Q, "downbeat_ordinal": ordinal}
            break
    return result


def construct(beats, downbeats, windows, total, gaps=()):
    if type(total) is not int or not 0 < total <= MAXIMUM_SOURCE_FRAMES:
        raise ValueError("invalid bounded source")
    events_valid(beats, total)
    events_valid(downbeats, total)
    if not set(downbeats) <= set(beats) or not 0 < len(windows) <= MAXIMUM_WINDOWS:
        raise ValueError("invalid downbeats/windows")
    previous = 0
    for window in windows:
        if set(window) != {"first_frame", "end_frame"}:
            raise ValueError("constructor requires plain geometry only")
        first, end = window["first_frame"], window["end_frame"]
        if type(first) is not int or type(end) is not int or first != previous or not first < end <= total:
            raise ValueError("incomplete source partition")
        previous = end
    if previous != total:
        raise ValueError("source tail omitted")
    previous = 0
    for first, end in gaps:
        if type(first) is not int or type(end) is not int or not previous <= first < end <= total:
            raise ValueError("invalid known PCM gaps")
        previous = end
    result = []
    for window in windows:
        first, end = window["first_frame"], window["end_frame"]
        if any(a < end and first < b for a, b in gaps):
            value = dict(window, status="known_pcm_gap", pieces=[], meter={})
        else:
            value = window_plan(beats, downbeats, first, end)
        result.append(value)
    failed = set()
    for i, (left, right) in enumerate(zip(result, result[1:])):
        if left["status"] == right["status"] == "accepted" and not compatible(expand(left["pieces"])[-1], expand(right["pieces"])[0]):
            failed.update((i, i + 1))
    for i in failed:
        result[i].update(status="source_seam_failure", diagnostic_pieces=result[i]["pieces"], pieces=[], meter={})
    piece_count = sum(len(w["pieces"]) for w in result)
    if piece_count > MAXIMUM_PIECES:
        for window in result:
            if window["status"] == "accepted":
                window.update(status="source_capacity_exceeded", diagnostic_pieces=window["pieces"], pieces=[], meter={})
    output = {"windows": result, "known_pcm_gaps": [list(g) for g in gaps], "pieces_before_capacity": piece_count,
              "capacity_exceeded": piece_count > MAXIMUM_PIECES, "failed_source_seams": sorted(failed),
              "full_source_support": all(w["status"] == "accepted" for w in result), "native_graph_constructed": False}
    validate(beats, output)
    return output


def validate(beats, result):
    if sum(len(w["pieces"]) for w in result["windows"]) > MAXIMUM_PIECES:
        raise ValueError("source capacity exceeded")
    for window in result["windows"]:
        points = expand(window["pieces"])
        expected = [(i, p) for i, p in enumerate(beats) if window["first_frame"] <= p < window["end_frame"]]
        if window["status"] != "accepted":
            if points or window["meter"]:
                raise ValueError("unsupported grid carries successful points/meter")
            continue
        if [ordinal for _, ordinal, _ in points] != [i for i, _ in expected]:
            raise ValueError("event identity changed")
        boundaries = [window["first_frame"]] + [(a[1] + b[1]) // 2 + 1 for a, b in zip(expected, expected[1:])] + [window["end_frame"]]
        next_ordinal = expected[0][0]
        for piece in window["pieces"]:
            if (piece["start_ordinal"] != next_ordinal or piece["beat_count"] < MINIMUM_PIECE_EVENTS
                    or piece["end_ordinal"] != next_ordinal + piece["beat_count"]
                    or not 40000 <= piece["tempo_millibpm"] <= 300000
                    or piece["period_q32"] != RATE * 60000 * Q // piece["tempo_millibpm"]):
                raise ValueError("piece ordinal/period corruption")
            anchor, period = piece["anchor_frame"] * Q, piece["period_q32"]
            a, b = piece["start_ordinal"] - expected[0][0], piece["end_ordinal"] - expected[0][0]
            if (piece["first_frame"] != boundaries[a] or piece["end_frame"] != boundaries[b]
                    or anchor - period >= piece["first_frame"] * Q
                    or anchor + piece["beat_count"] * period < piece["end_frame"] * Q):
                raise ValueError("piece hides natural points")
            next_ordinal = piece["end_ordinal"]
        for i, ((point, _, _), (_, event)) in enumerate(zip(points, expected)):
            if not boundaries[i] * Q <= point < boundaries[i + 1] * Q or abs(point - event * Q) > MAXIMUM_RESIDUAL_FRAMES * Q:
                raise ValueError("event/window/residual identity failed")
        if any(not compatible(a, b) for a, b in zip(points, points[1:])):
            raise ValueError("actual seam failed")
        meter = window["meter"]
        if meter:
            matches = [p for p in points if p[1] == meter["downbeat_ordinal"]]
            if len(matches) != 1 or matches[0][0] != meter["downbeat_frame"] * Q + meter["downbeat_fraction_q32"]:
                raise ValueError("meter detached from own grid")
