#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage B: frozen label-blind host recurrence over complete native dense bins.

No corpus CLI, audio decoder, reference labels, baseline fallback or meter.
All real selector scoring belongs to the separately gated ordered stage C.
"""
from __future__ import annotations

from array import array
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import time
try:
    import resource
except ImportError:
    resource = None

import apta_1_1_native_history_development as history

PROTOCOL_SHA256 = "bf8dc41fcce167b3ed9fb23d08d8106121e1c0e74c2d012d253025d893c9bbf2"
_protocol_bytes = (Path(__file__).resolve().parents[1] / "docs/rust/S6-TEMPORAL-MODEL-PROTOCOL.json").read_bytes()
if hashlib.sha256(_protocol_bytes).hexdigest() != PROTOCOL_SHA256:
    raise ValueError("temporal model requires the exact registered protocol")
PROTOCOL = json.loads(_protocol_bytes)
MODEL = PROTOCOL["model"]
LIMITS = PROTOCOL["resources"]
RATE = PROTOCOL["evidence"]["sample_rate"]
HOP = PROTOCOL["evidence"]["bin_frames"]
WINDOW = history.WINDOW
MAX_BINS = PROTOCOL["evidence"]["max_context_bins"]
TEMPOS = MODEL["tempo_states"]
PERIODS = tuple(RATE * 60 / (HOP * (40 * (300 / 40) ** (j / 127))) for j in range(TEMPOS))


class ResourceLimit(RuntimeError):
    """Operational veto, never a musical abstention or fallback."""


def _rss_kib() -> int:
    if resource is None:
        raise ResourceLimit("RSS enforcement requires a supported host; run this probe in WSL/Linux")
    value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return int(value // 1024 if sys.platform == "darwin" else value)


class SourceBudget:
    """Share one budget across all target windows of a thirty-second source."""
    def __init__(self):
        self.started = time.monotonic()
        self.numeric_peak = 0
        self.rss_peak = 0

    def check(self, numeric_bytes: int = 0) -> None:
        self.numeric_peak = max(self.numeric_peak, numeric_bytes)
        self.rss_peak = max(self.rss_peak, _rss_kib())
        if self.numeric_peak > LIMITS["host_numeric_workspace_bytes_max"]:
            raise ResourceLimit("registered numeric workspace cap exceeded")
        if self.rss_peak > LIMITS["host_rss_kib_max"]:
            raise ResourceLimit("registered host RSS cap exceeded")
        if time.monotonic() - self.started > LIMITS["host_seconds_per_thirty_second_source_max"]:
            raise ResourceLimit("registered source wall-time cap exceeded")


class _Paths:
    def __init__(self, count: int, states: int, budget: SourceBudget):
        slots = count * states * 2
        if array("d").itemsize != 8 or array("I").itemsize != 4:
            raise ResourceLimit("unsupported numeric array layout")
        budget.check(slots * 12)
        self.scores = array("d", [-math.inf]) * slots
        self.previous = array("I", [0]) * slots
        self.states = states
        self.stride = states * 2
        self.transitions = 0
        budget.check(self.bytes())

    def bytes(self) -> int:
        return sys.getsizeof(self.scores) + sys.getsizeof(self.previous)

    def path(self, node: int) -> tuple[tuple[int, int], ...]:
        result = []
        while node:
            cell = node - 1
            result.append((cell // self.stride, (cell // 2) % self.states))
            node = self.previous[cell]
        result.reverse()
        return tuple(result)

    def earlier_extension(self, left: int, right: int, i: int, j: int) -> bool:
        # The current node must participate: one predecessor may prefix another.
        return self.path(left) + ((i, j),) < self.path(right) + ((i, j),)


def _build(values, periods, budget: SourceBudget) -> _Paths:
    """Exact two-best DAG recurrence. Alternate periods are for micro-oracle tests only."""
    paths = _Paths(len(values), len(periods), budget)
    edges = []
    for j, period in enumerate(periods):
        edges.append(tuple((d, k, abs(d - period), abs(period - periods[k]))
            for d in range(max(1, math.ceil(period - 1)), math.floor(period + 1) + 1)
            for k in range(max(0, j - 1), min(len(periods), j + 2))))
    for i, value in enumerate(values):
        budget.check(paths.bytes())
        for j, period in enumerate(periods):
            best = (i + 1) * value if i < period else -math.inf
            second = -math.inf
            best_prev = second_prev = 0
            for d, k, interval_error, tempo_error in edges[j]:
                h = i - d
                if h < 0:
                    continue
                increment = d * value - interval_error - tempo_error
                predecessor = h * paths.stride + 2 * k
                for rank in (0, 1):
                    cell = predecessor + rank
                    old = paths.scores[cell]
                    if old == -math.inf:
                        continue
                    paths.transitions += 1
                    score = old + increment
                    node = cell + 1
                    if score > best or (score == best and paths.earlier_extension(node, best_prev, i, j)):
                        second, second_prev = best, best_prev
                        best, best_prev = score, node
                    elif score > second or (score == second and paths.earlier_extension(node, second_prev, i, j)):
                        second, second_prev = score, node
            cell = i * paths.stride + 2 * j
            paths.scores[cell] = best
            paths.previous[cell] = best_prev
            paths.scores[cell + 1] = second
            paths.previous[cell + 1] = second_prev
    return paths


def _fit(path, context_first: int, target_first: int, target_end: int, rate: int = RATE):
    if len(path) < MODEL["minimum_context_beats"]:
        return None
    # All target nodes are consecutive path ordinals, even with zero strength.
    points = [i * HOP for i, _ in path if target_first <= context_first + i * HOP < target_end]
    n = len(points)
    if n < MODEL["minimum_target_beats"]:
        return None
    sx = n * (n - 1) // 2
    sxx = n * (n - 1) * (2 * n - 1) // 6
    sy = sum(points)
    sxy = sum(k * y for k, y in enumerate(points))
    denominator = n * sxx - sx * sx
    period_numerator = n * sxy - sx * sy
    if period_numerator <= 0 or not period_numerator * 40 <= rate * 60 * denominator <= period_numerator * 300:
        return None
    intercept_numerator = sy * denominator - period_numerator * sx
    common = n * denominator
    period_common = n * period_numerator
    if any(abs(y * common - intercept_numerator - k * period_common) * 10 > period_common
           for k, y in enumerate(points)):
        return None
    # Integer-rational least squares avoids loss of source identity at large offsets.
    relative_start = target_first - context_first
    ordinal = -((intercept_numerator - relative_start * common) // period_common)
    position = intercept_numerator + ordinal * period_common
    if not relative_start * common <= position < (target_end - context_first) * common:
        return None
    anchor = context_first + (2 * position + common) // (2 * common)
    tempo = (2 * rate * 60000 * denominator + period_numerator) // (2 * period_numerator)
    if not target_first <= anchor < target_end or not 40000 <= tempo <= 300000:
        return None
    return anchor, tempo


def _distinct(anchor: int, tempo: int, winner_anchor: int, winner_tempo: int) -> bool:
    period = (RATE * 60000 << 32) // winner_tempo
    other = (RATE * 60000 << 32) // tempo
    phase = (abs(anchor - winner_anchor) << 32) % period
    return 100 * abs(other - period) > period or 10 * min(phase, period - phase) > period


class _Terminals:
    def __init__(self):
        self.scores = array("d")
        self.nodes = array("I")
        self.anchors = array("Q")
        self.tempos = array("I")

    def append(self, score, node, anchor, tempo):
        self.scores.append(score); self.nodes.append(node)
        self.anchors.append(anchor); self.tempos.append(tempo)

    def bytes(self):
        return sum(sys.getsizeof(a) for a in (self.scores, self.nodes, self.anchors, self.tempos))


def _rank(terminals: _Terminals, paths: _Paths, budget: SourceBudget):
    def better(a, b):
        return (b is None or terminals.scores[a] > terminals.scores[b]
                or (terminals.scores[a] == terminals.scores[b] and paths.path(terminals.nodes[a]) < paths.path(terminals.nodes[b])))
    winner = None
    for i in range(len(terminals.scores)):
        if i % 256 == 0: budget.check(paths.bytes() + terminals.bytes())
        if better(i, winner): winner = i
    if winner is None:
        return {"kind": "no_valid_path"}
    runner = None
    for i in range(len(terminals.scores)):
        if i % 256 == 0: budget.check(paths.bytes() + terminals.bytes())
        if _distinct(terminals.anchors[i], terminals.tempos[i], terminals.anchors[winner], terminals.tempos[winner]) and better(i, runner):
            runner = i
    score = terminals.scores[winner]
    other = terminals.scores[runner] if runner is not None else 0.0
    detail = {"score": score, "runner_score": other, "margin": score - other,
              "terminal_proposals": len(terminals.scores), "path": paths.path(terminals.nodes[winner])}
    if score <= 0:
        return dict(detail, kind="insufficient_support")
    if score - other <= 0:
        return dict(detail, kind="ambiguous")
    return dict(detail, kind="accepted", anchor_frame=terminals.anchors[winner],
                tempo_millibpm=terminals.tempos[winner], confidence=MODEL["confidence"])


def select(values, context_first: int, target_first: int, target_end: int, *, budget: SourceBudget | None = None) -> dict:
    """No reference labels, corpus identity, old estimates or tunable model knobs."""
    if (any(type(v) is not int for v in (context_first, target_first, target_end))
        or not 0 <= context_first <= target_first < target_end <= (1 << 64) - 1
        or context_first % WINDOW or target_first % WINDOW
        or target_end > target_first + WINDOW or target_first - context_first > 2 * WINDOW
        or not 0 < len(values) <= MAX_BINS or len(values) != (target_end - context_first + HOP - 1) // HOP):
        raise ValueError("invalid bounded temporal source geometry")
    if any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1
           or struct.unpack("<f", struct.pack("<f", v))[0] != v for v in values):
        raise ValueError("temporal observations must be exact finite native f32 in [0,1]")
    if context_first == 0 and any(values[:min(len(values), 15)]):
        raise ValueError("native initial background bins must be declared zero")
    budget = budget if budget is not None else SourceBudget()
    started = time.monotonic()
    paths = _build(values, PERIODS, budget)
    terminals = _Terminals()
    for i in range(max(0, len(values) - math.ceil(max(PERIODS))), len(values)):
        budget.check(paths.bytes() + terminals.bytes())
        for j, period in enumerate(PERIODS):
            if len(values) - 1 - i >= period:
                continue
            for rank in (0, 1):
                cell = i * paths.stride + j * 2 + rank
                if paths.scores[cell] == -math.inf:
                    continue
                node = cell + 1
                fitted = _fit(paths.path(node), context_first, target_first, target_end)
                if fitted is not None:
                    score = (paths.scores[cell] + (len(values) - 1 - i) * values[i]) / len(values)
                    terminals.append(score, node, *fitted)
    result = _rank(terminals, paths, budget)
    budget.check(paths.bytes() + terminals.bytes())
    result["resources"] = {"elapsed_seconds": time.monotonic() - started, "source_elapsed_seconds": time.monotonic() - budget.started,
        "numeric_workspace_peak_bytes": budget.numeric_peak, "rss_peak_kib": budget.rss_peak,
        "cell_slots": len(paths.scores), "transitions": paths.transitions}
    return result


def select_window(native: dict, index: int, *, budget: SourceBudget | None = None) -> dict:
    """Use only the current and preceding two native windows; never future values."""
    if type(index) is not int or not 0 <= index < len(native["windows"]):
        raise ValueError("invalid target window index")
    if (native.get("format") != "libapta-history-diagnostic-1" or native.get("mode") != "novelty"
        or type(native.get("sample_rate")) is not int or native.get("sample_rate") != RATE
        or type(native.get("channel_count")) is not int or native.get("channel_count") != 2
        or not isinstance(native.get("input_sha256"), str) or not history.HEX.fullmatch(native["input_sha256"])
        or native.get("finished") is not True or native.get("capacity_exhausted") is not False
        or native.get("provisional") is not True or native.get("analyzed_first_frame") != 0):
        raise ValueError("invalid temporal native source declaration")
    total, requested, analyzed = (native.get(key) for key in ("source_total_frames", "requested_frames", "analyzed_frames"))
    if (any(type(v) is not int for v in (total, requested, analyzed)) or total <= 0
        or not 0 < requested <= RATE * 30 or analyzed != min(total, requested)):
        raise ValueError("invalid temporal native prefix scope")
    context = native["windows"][max(0, index - 2):index + 1]
    history.validate_dense_trace(dict(sample_rate=RATE, windows=context))
    if context[-1]["end_frame"] > analyzed:
        raise ValueError("target lies outside analyzed source prefix")
    if any(left["end_frame"] != right["first_frame"] for left, right in zip(context, context[1:])):
        raise ValueError("temporal context cannot bridge missing source ranges")
    return select([v for w in context for v in w["dense_trace"]["values"]], context[0]["first_frame"],
                  context[-1]["first_frame"], context[-1]["end_frame"], budget=budget)
