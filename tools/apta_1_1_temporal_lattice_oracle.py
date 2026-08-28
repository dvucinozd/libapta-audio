#!/usr/bin/env python3
"""Evaluate the frozen WP2-I1 temporal selector on development traces.

This is a privacy-safe development oracle. It accepts only the balanced
ASAP/Ballroom label topology and refuses trace coverage beyond the development
split. It does not implement or claim acceptance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

try:
    import numpy as np
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit(
        "error: apta_1_1_temporal_lattice_oracle.py requires numpy"
    ) from exc

from apta_1_1_onset_trace_oracle import (  # noqa: E402
    PERIOD_TOLERANCE,
    PHASE_TOLERANCE_BEATS,
    _candidate_lags,
    _load_trace,
    _phase_error_beats,
    _selected_phase,
    _sha256,
    _truth_period_bins,
)


FORMAT = "apta-1.1-temporal-lattice-oracle-1"
TRACE_BINS = 4096
WINDOW_BINS = 1024
HOP_BINS = 512
WINDOW_STARTS = tuple(range(0, TRACE_BINS - WINDOW_BINS + 1, HOP_BINS))
STATE_COUNT = 3


def _trace_set_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.name.encode("ascii"))
        digest.update(b"\0")
        digest.update(_sha256(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _state_rows(
    novelty: np.ndarray, start: int, sample_rate: int
) -> list[dict[str, float | int]]:
    candidates = _candidate_lags(novelty, sample_rate)
    if len(candidates) != STATE_COUNT:
        raise ValueError(
            f"window at {start} has {len(candidates)} candidates, expected 3"
        )
    best_score = candidates[0][1]
    if best_score <= 0.0:
        raise ValueError(f"window at {start} has no positive best score")
    return [
        {
            "lag": lag,
            "phase": _selected_phase(novelty, lag),
            "score": score,
            "emission": math.log(score / best_score),
            "start": start,
        }
        for lag, score in candidates
    ]


def _first_beat_at_or_after(state: dict[str, float | int], frame: int) -> float:
    lag = int(state["lag"])
    origin = int(state["start"]) + int(state["phase"])
    if origin >= frame:
        return float(origin)
    steps = math.ceil((frame - origin) / lag)
    return float(origin + steps * lag)


def _transition(
    previous: dict[str, float | int],
    current: dict[str, float | int],
    comparison_start: int,
) -> float:
    previous_lag = int(previous["lag"])
    current_lag = int(current["lag"])
    current_beat = _first_beat_at_or_after(current, comparison_start)
    previous_origin = int(previous["start"]) + int(previous["phase"])
    nearest_ordinal = math.floor(
        (current_beat - previous_origin) / previous_lag + 0.5
    )
    previous_beat = previous_origin + nearest_ordinal * previous_lag
    phase_distance = abs(current_beat - previous_beat) / (
        (previous_lag + current_lag) / 2.0
    )
    tempo_distance = abs(math.log2(current_lag / previous_lag))
    return -tempo_distance - phase_distance


def _select_temporal_rank(novelty: np.ndarray, sample_rate: int) -> tuple[
    int, list[dict[str, float | int]]
]:
    if len(novelty) != TRACE_BINS:
        raise ValueError(
            f"temporal oracle requires {TRACE_BINS} bins, got {len(novelty)}"
        )
    windows = [
        _state_rows(
            novelty[start:start + WINDOW_BINS], start, sample_rate
        )
        for start in WINDOW_STARTS
    ]
    path_scores = [float(state["emission"]) for state in windows[0]]
    for window_index in range(1, len(windows)):
        previous_states = windows[window_index - 1]
        current_states = windows[window_index]
        next_scores: list[float] = []
        for current in current_states:
            best = -math.inf
            for previous_rank, previous in enumerate(previous_states):
                score = path_scores[previous_rank] + _transition(
                    previous, current, int(current["start"])
                )
                if score > best:
                    best = score
            next_scores.append(best + float(current["emission"]))
        path_scores = next_scores

    full_states = _state_rows(novelty, 0, sample_rate)
    final_start = WINDOW_STARTS[-1]
    best_rank = 0
    best_score = -math.inf
    for full_rank, full_state in enumerate(full_states):
        terminal = dict(full_state)
        terminal["start"] = 0
        score = -math.inf
        for previous_rank, previous in enumerate(windows[-1]):
            candidate_score = path_scores[previous_rank] + _transition(
                previous, terminal, final_start
            )
            if candidate_score > score:
                score = candidate_score
        score += float(full_state["emission"])
        if score > best_score:
            best_score = score
            best_rank = full_rank
    return best_rank, full_states


def _metric_row(
    label: dict[str, object],
    first_bin: int,
    lag: int,
    phase: int,
    truth_period: float,
) -> dict[str, bool]:
    period_ok = abs(lag - truth_period) / truth_period <= PERIOD_TOLERANCE
    phase_ok = (
        _phase_error_beats(label, first_bin, lag, phase)
        <= PHASE_TOLERANCE_BEATS
    )
    return {
        "top1_period": period_ok,
        "top1_phase": phase_ok,
        "top1_period_phase": period_ok and phase_ok,
    }


def _summarize(
    baseline: dict[str, dict[str, bool]],
    temporal: dict[str, dict[str, bool]],
) -> dict[str, object]:
    summary: dict[str, object] = {}
    for metric in ("top1_period", "top1_phase", "top1_period_phase"):
        fixes = sorted(
            track
            for track in baseline
            if not baseline[track][metric] and temporal[track][metric]
        )
        breaks = sorted(
            track
            for track in baseline
            if baseline[track][metric] and not temporal[track][metric]
        )
        summary[metric] = {
            "baseline_correct": sum(row[metric] for row in baseline.values()),
            "temporal_correct": sum(row[metric] for row in temporal.values()),
            "fixes": len(fixes),
            "breaks": len(breaks),
            "net_fixes": len(fixes) - len(breaks),
            "fix_track_ids": fixes,
            "break_track_ids": breaks,
        }
    return summary


def evaluate(labels_path: Path, traces: Path, corpus: str) -> dict[str, object]:
    labels_raw = json.loads(labels_path.read_text(encoding="utf-8"))
    labels = {
        str(row["track"]): row
        for row in labels_raw
        if row.get("split") == "development"
    }
    if len(labels) != len(labels_raw) / 2:
        raise ValueError("labels must contain one balanced development split")
    paths = sorted(traces.glob("track-*.ndjson"))
    if {path.stem for path in paths} != set(labels):
        raise ValueError("trace coverage does not exactly match development IDs")

    baseline: dict[str, dict[str, bool]] = {}
    temporal: dict[str, dict[str, bool]] = {}
    selected_rank_counts = [0, 0, 0]
    captured_candidate_set_matches = 0
    selected_within_full_top3 = 0
    for path in paths:
        track = path.stem
        label = labels[track]
        trace, _energy = _load_trace(path)
        novelty = np.asarray(trace["onset_flux"], dtype=np.float64)
        sample_rate = int(trace["sample_rate"])
        selected_rank, full_states = _select_temporal_rank(
            novelty, sample_rate
        )
        traced = [
            int(row["lag_bins"])
            for row in trace.get("tempo_candidates", [])
            if int(row["lag_bins"]) > 0
        ]
        computed = [int(row["lag"]) for row in full_states]
        if traced == computed:
            captured_candidate_set_matches += 1
        if 0 <= selected_rank < len(full_states):
            selected_within_full_top3 += 1
            selected_rank_counts[selected_rank] += 1
        first_bin = int(trace["onset_evidence_first_bin"])
        truth_period = _truth_period_bins(
            label, first_bin, len(novelty)
        )
        baseline_state = full_states[0]
        temporal_state = full_states[selected_rank]
        baseline[track] = _metric_row(
            label,
            first_bin,
            int(baseline_state["lag"]),
            int(baseline_state["phase"]),
            truth_period,
        )
        temporal[track] = _metric_row(
            label,
            first_bin,
            int(temporal_state["lag"]),
            int(temporal_state["phase"]),
            truth_period,
        )

    return {
        "format": FORMAT,
        "acceptance_claim": False,
        "evidence_level": "development",
        "corpus": corpus,
        "track_count": len(labels),
        "inputs": {
            "labels_sha256": _sha256(labels_path),
            "trace_count": len(paths),
            "trace_set_sha256": _trace_set_sha256(paths),
        },
        "geometry": {
            "trace_bins": TRACE_BINS,
            "window_bins": WINDOW_BINS,
            "hop_bins": HOP_BINS,
            "window_starts": list(WINDOW_STARTS),
            "state_count": STATE_COUNT,
        },
        "validation": {
            "captured_candidate_order_matches": (
                captured_candidate_set_matches
            ),
            "selected_within_full_top3": selected_within_full_top3,
        },
        "selected_rank_counts": {
            str(index): count
            for index, count in enumerate(selected_rank_counts)
        },
        "metrics": _summarize(baseline, temporal),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = evaluate(args.labels, args.traces, args.corpus)
        encoded = json.dumps(
            report, sort_keys=True, separators=(",", ":")
        ) + "\n"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.exit(1, f"error: {exc}\n")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
