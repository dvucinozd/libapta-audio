#!/usr/bin/env python3
"""Evaluate the frozen WP2-I3 beat-path selector on development traces.

This is a privacy-safe development oracle. It accepts only the balanced
ASAP/Ballroom development-label topology and does not implement or claim
acceptance.
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
        "error: apta_1_1_beat_path_oracle.py requires numpy"
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


FORMAT = "apta-1.1-beat-path-oracle-1"
TRACE_BINS = 4096
STATE_COUNT = 3
PATH_RADIUS_BEATS = 0.10


def _trace_set_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.name.encode("ascii"))
        digest.update(b"\0")
        digest.update(_sha256(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _offset_states(radius: int) -> list[int]:
    if radius < 1:
        raise ValueError("path radius must be positive")
    states = [0]
    for distance in range(1, radius + 1):
        states.extend((-distance, distance))
    return states


def _path_mean(novelty: np.ndarray, lag: int, phase: int) -> float | None:
    if lag <= 0 or phase < 0 or phase >= lag:
        raise ValueError("invalid beat-path candidate geometry")
    if len(novelty) != TRACE_BINS or not np.all(np.isfinite(novelty)):
        raise ValueError("beat-path oracle requires a finite 4,096-bin trace")
    if np.any(novelty < 0.0):
        raise ValueError("beat-path novelty must be non-negative")
    mean = float(np.mean(novelty, dtype=np.float64))
    if not math.isfinite(mean) or mean <= 0.0:
        return None
    radius = max(1, math.floor(PATH_RADIUS_BEATS * lag))
    offsets = _offset_states(radius)
    beats = [
        beat
        for beat in range(phase, len(novelty), lag)
        if beat - radius >= 0 and beat + radius < len(novelty)
    ]
    if len(beats) < 2:
        return None

    scores = [
        math.log1p(float(novelty[beats[0] + offset]) / mean)
        for offset in offsets
    ]
    for beat in beats[1:]:
        next_scores: list[float] = []
        for offset in offsets:
            emission = math.log1p(float(novelty[beat + offset]) / mean)
            best = -math.inf
            for prior_index, prior_offset in enumerate(offsets):
                candidate = scores[prior_index] - (
                    abs(offset - prior_offset) / radius
                )
                if candidate > best:
                    best = candidate
            next_scores.append(best + emission)
        scores = next_scores
    return max(scores) / len(beats)


def _select_beat_path_rank(
    novelty: np.ndarray, sample_rate: int
) -> tuple[int, list[dict[str, float | int]]]:
    if len(novelty) != TRACE_BINS:
        raise ValueError(
            f"beat-path oracle requires {TRACE_BINS} bins, got {len(novelty)}"
        )
    candidates = _candidate_lags(novelty, sample_rate)
    if len(candidates) != STATE_COUNT:
        raise ValueError(
            f"full trace has {len(candidates)} candidates, expected 3"
        )
    best_score = candidates[0][1]
    if best_score <= 0.0:
        raise ValueError("full trace has no positive best score")

    rows: list[dict[str, float | int]] = []
    selected_rank = 0
    selected_total = -math.inf
    for rank, (lag, score) in enumerate(candidates):
        phase = _selected_phase(novelty, lag)
        path_mean = _path_mean(novelty, lag, phase)
        total = (
            math.log(score / best_score) + path_mean
            if path_mean is not None
            else -math.inf
        )
        rows.append(
            {
                "lag": lag,
                "phase": phase,
                "score": score,
                "path_mean": path_mean if path_mean is not None else 0.0,
                "total": total,
            }
        )
        if total > selected_total:
            selected_total = total
            selected_rank = rank
    if not math.isfinite(selected_total):
        selected_rank = 0
    return selected_rank, rows


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
    candidate: dict[str, dict[str, bool]],
) -> dict[str, object]:
    summary: dict[str, object] = {}
    for metric in ("top1_period", "top1_phase", "top1_period_phase"):
        fixes = sorted(
            track
            for track in baseline
            if not baseline[track][metric] and candidate[track][metric]
        )
        breaks = sorted(
            track
            for track in baseline
            if baseline[track][metric] and not candidate[track][metric]
        )
        summary[metric] = {
            "baseline_correct": sum(row[metric] for row in baseline.values()),
            "candidate_correct": sum(row[metric] for row in candidate.values()),
            "fixes": len(fixes),
            "breaks": len(breaks),
            "net_fixes": len(fixes) - len(breaks),
            "fix_track_ids": fixes,
            "break_track_ids": breaks,
        }
    return summary


def evaluate(
    labels_path: Path, traces: Path, corpus: str
) -> dict[str, object]:
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
    candidate: dict[str, dict[str, bool]] = {}
    selected_rank_counts = [0, 0, 0]
    captured_candidate_order_matches = 0
    selected_within_full_top3 = 0
    for path in paths:
        track = path.stem
        label = labels[track]
        trace, _energy = _load_trace(path)
        novelty = np.asarray(trace["onset_flux"], dtype=np.float64)
        selected_rank, states = _select_beat_path_rank(
            novelty, int(trace["sample_rate"])
        )
        traced = [
            int(row["lag_bins"])
            for row in trace.get("tempo_candidates", [])
            if int(row["lag_bins"]) > 0
        ]
        computed = [int(row["lag"]) for row in states]
        if traced == computed:
            captured_candidate_order_matches += 1
        if 0 <= selected_rank < len(states):
            selected_within_full_top3 += 1
            selected_rank_counts[selected_rank] += 1
        first_bin = int(trace["onset_evidence_first_bin"])
        truth_period = _truth_period_bins(label, first_bin, len(novelty))
        baseline_state = states[0]
        candidate_state = states[selected_rank]
        baseline[track] = _metric_row(
            label,
            first_bin,
            int(baseline_state["lag"]),
            int(baseline_state["phase"]),
            truth_period,
        )
        candidate[track] = _metric_row(
            label,
            first_bin,
            int(candidate_state["lag"]),
            int(candidate_state["phase"]),
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
            "state_count": STATE_COUNT,
            "path_radius_beats": PATH_RADIUS_BEATS,
        },
        "validation": {
            "captured_candidate_order_matches": (
                captured_candidate_order_matches
            ),
            "selected_within_full_top3": selected_within_full_top3,
        },
        "selected_rank_counts": {
            str(index): count
            for index, count in enumerate(selected_rank_counts)
        },
        "metrics": _summarize(baseline, candidate),
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
