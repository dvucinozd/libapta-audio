#!/usr/bin/env python3
"""Compare fixed onset-evidence families on privacy-safe development traces.

This is a development oracle, not an analyzer, evaluator or acceptance tool.
It consumes only opaque track IDs, frozen beat labels and opt-in trace arrays.
Formal holdout labels must not be supplied.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from statistics import median
from typing import Callable

try:
    import numpy as np
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit(
        "error: apta_1_1_onset_trace_oracle.py requires numpy"
    ) from exc


FORMAT = "apta-1.1-onset-trace-oracle-1"
BIN_FRAMES = 256
MIN_BPM = 40
MAX_BPM = 300
PRIOR_CENTRE_MILLIBPM = 125_000
PRIOR_WIDTH = 0.55
QUANTIZATION_FLOOR = 1.0 / 255.0
PHASE_TOLERANCE_BEATS = 0.10
PERIOD_TOLERANCE = 0.01


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _positive_rise(values: np.ndarray) -> np.ndarray:
    previous = np.empty_like(values)
    previous[0] = 0.0
    previous[1:] = values[:-1]
    return np.maximum(values - previous, 0.0)


def _rolling_mean(values: np.ndarray, width: int) -> np.ndarray:
    result = np.zeros_like(values)
    prefix = np.concatenate((np.zeros(1), np.cumsum(values, dtype=np.float64)))
    indices = np.arange(len(values))
    first = np.maximum(indices - width, 0)
    counts = indices - first
    usable = counts > 0
    result[usable] = (
        prefix[indices[usable]] - prefix[first[usable]]
    ) / counts[usable]
    return result


def _baseline_band_flux(energy: np.ndarray) -> np.ndarray:
    bands = energy[:, :3]
    broad = energy[:, 3]
    current_total = bands.sum(axis=1)
    previous_total = np.concatenate((np.zeros(1), current_total[:-1]))
    novelty = _positive_rise(bands).sum(axis=1)
    return _positive_rise(broad) + np.where(
        current_total > previous_total, 0.25 * novelty, 0.0
    )


def _broadband_rise(energy: np.ndarray) -> np.ndarray:
    return _positive_rise(energy[:, 3])


def _log_band_flux(energy: np.ndarray) -> np.ndarray:
    compressed = np.log1p(255.0 * energy)
    return _baseline_band_flux(compressed)


def _local_contrast(energy: np.ndarray) -> np.ndarray:
    floors = np.column_stack(
        [_rolling_mean(energy[:, column], 16) for column in range(4)]
    )
    contrast = np.maximum(energy - floors, 0.0) / (
        energy + floors + QUANTIZATION_FLOOR
    )
    return 0.5 * contrast[:, 3] + 0.5 * contrast[:, :3].mean(axis=1)


def _unit_mean(values: np.ndarray) -> np.ndarray:
    mean = float(values.mean())
    if mean <= np.finfo(np.float64).eps:
        return np.zeros_like(values)
    return values / mean


def _equal_mean_fusion_local_contrast(energy: np.ndarray) -> np.ndarray:
    baseline = _unit_mean(_baseline_band_flux(energy))
    local_contrast = _unit_mean(_local_contrast(energy))
    return 0.5 * baseline + 0.5 * local_contrast


def _causal_mean_fusion_local_contrast(energy: np.ndarray) -> np.ndarray:
    history = np.zeros((16, 4), dtype=np.float32)
    history_sum = np.zeros(4, dtype=np.float32)
    previous = np.zeros(4, dtype=np.float32)
    baseline_sum = np.float32(0.0)
    contrast_sum = np.float32(0.0)
    floor_epsilon = np.float32(1.0) / np.float32(255.0)
    result = np.zeros(len(energy), dtype=np.float32)
    history_count = 0
    cursor = 0
    have_previous = False

    def contrast(current: np.float32, floor: np.float32) -> np.float32:
        excess = np.maximum(
            np.float32(current - floor), np.float32(0.0)
        )
        denominator = np.float32(
            np.float32(current + floor) + floor_epsilon
        )
        return np.float32(excess / denominator)

    for index, row in enumerate(energy):
        current = np.asarray(row, dtype=np.float32)
        current_total = np.float32(0.0)
        previous_total = np.float32(0.0)
        band_rise = np.float32(0.0)
        for band in range(3):
            prior = previous[band] if have_previous else np.float32(0.0)
            current_total = np.float32(current_total + current[band])
            previous_total = np.float32(previous_total + prior)
            band_rise = np.float32(
                band_rise +
                np.maximum(
                    np.float32(current[band] - prior), np.float32(0.0)
                )
            )
        broadband_previous = (
            previous[3] if have_previous else np.float32(0.0)
        )
        broadband_rise = np.maximum(
            np.float32(current[3] - broadband_previous), np.float32(0.0)
        )
        baseline = np.float32(
            broadband_rise +
            (
                np.float32(0.25) * band_rise
                if current_total > previous_total
                else np.float32(0.0)
            )
        )

        count = np.float32(history_count)
        broadband_floor = (
            np.float32(history_sum[3] / count)
            if history_count > 0
            else np.float32(0.0)
        )
        band_contrast = np.float32(0.0)
        for band in range(3):
            band_floor = (
                np.float32(history_sum[band] / count)
                if history_count > 0
                else np.float32(0.0)
            )
            band_contrast = np.float32(
                band_contrast + contrast(current[band], band_floor)
            )
        local_contrast = np.float32(
            np.float32(0.5) * contrast(current[3], broadband_floor) +
            np.float32(0.5) * np.float32(
                band_contrast / np.float32(3.0)
            )
        )

        if history_count == 16:
            for column in range(4):
                history_sum[column] = np.float32(
                    history_sum[column] - history[cursor, column]
                )
        for column in range(4):
            history_sum[column] = np.float32(
                history_sum[column] + current[column]
            )
        history[cursor] = current
        cursor = (cursor + 1) % 16
        history_count = min(history_count + 1, 16)

        baseline_sum = np.float32(baseline_sum + baseline)
        contrast_sum = np.float32(contrast_sum + local_contrast)
        causal_count = np.float32(index + 1)
        baseline_mean = np.float32(baseline_sum / causal_count)
        contrast_mean = np.float32(contrast_sum / causal_count)
        baseline_normalized = (
            np.float32(baseline / baseline_mean)
            if baseline_mean > np.float32(0.0)
            else np.float32(0.0)
        )
        contrast_normalized = (
            np.float32(local_contrast / contrast_mean)
            if contrast_mean > np.float32(0.0)
            else np.float32(0.0)
        )
        result[index] = np.float32(
            np.float32(0.5) * baseline_normalized +
            np.float32(0.5) * contrast_normalized
        )
        previous = current.copy()
        have_previous = True

    return result.astype(np.float64)


def _adaptive_whitened_flux(energy: np.ndarray) -> np.ndarray:
    floors = np.column_stack(
        [_rolling_mean(energy[:, column], 16) for column in range(4)]
    )
    whitened = energy / (floors + QUANTIZATION_FLOOR)
    return 0.5 * _positive_rise(whitened[:, 3]) + 0.5 * (
        _positive_rise(whitened[:, :3]).mean(axis=1)
    )


FORMULAS: dict[str, Callable[[np.ndarray], np.ndarray] | None] = {
    "captured_multiband": None,
    "reconstructed_multiband": _baseline_band_flux,
    "broadband_rise": _broadband_rise,
    "log_band_flux": _log_band_flux,
    "local_contrast_16": _local_contrast,
    "equal_mean_fusion_local_contrast_16":
        _equal_mean_fusion_local_contrast,
    "causal_mean_fusion_local_contrast_16":
        _causal_mean_fusion_local_contrast,
    "adaptive_whitened_flux_16": _adaptive_whitened_flux,
}


def _tempo_prior(tempo_millibpm: float) -> float:
    logarithm = math.log(tempo_millibpm / PRIOR_CENTRE_MILLIBPM) / PRIOR_WIDTH
    return math.exp(-0.5 * logarithm * logarithm)


def _candidate_lags(
    novelty: np.ndarray, sample_rate: int
) -> list[tuple[int, float]]:
    count = len(novelty)
    minimum = math.ceil(sample_rate * 60 / (MAX_BPM * BIN_FRAMES))
    maximum = math.floor(sample_rate * 60 / (MIN_BPM * BIN_FRAMES))
    maximum = min(maximum, count // 2)
    if maximum < minimum:
        return []

    fft_length = 1 << (2 * count - 1).bit_length()
    spectrum = np.fft.rfft(novelty, fft_length)
    autocorrelation = np.fft.irfft(
        spectrum * np.conjugate(spectrum), fft_length
    )[:count]
    squares = np.concatenate(
        (np.zeros(1), np.cumsum(novelty * novelty, dtype=np.float64))
    )
    scored: list[tuple[int, float]] = []
    for lag in range(minimum, maximum + 1):
        left_square = squares[count] - squares[lag]
        right_square = squares[count - lag]
        if left_square <= 1.0e-12 or right_square <= 1.0e-12:
            continue
        correlation = float(
            autocorrelation[lag] / math.sqrt(left_square * right_square)
        )
        tempo = sample_rate * 60_000.0 / (lag * BIN_FRAMES)
        score = max(correlation, 0.0) * _tempo_prior(tempo)
        if score > 0.0:
            scored.append((lag, score))
    scored.sort(key=lambda item: (-item[1], item[0]))
    return scored[:3]


def _selected_phase(novelty: np.ndarray, lag: int) -> int:
    phases = np.arange(len(novelty), dtype=np.int64) % lag
    scores = np.bincount(phases, weights=novelty, minlength=lag)
    return int(np.argmax(scores))


def _truth_period_bins(label: dict[str, object], first_bin: int, count: int) -> float:
    beats = [int(value) for value in label["beat_frames"]]
    first_frame = first_bin * BIN_FRAMES
    end_frame = (first_bin + count) * BIN_FRAMES
    gaps = [
        right - left
        for left, right in zip(beats, beats[1:])
        if right >= first_frame and left < end_frame
    ]
    if not gaps:
        gaps = [right - left for left, right in zip(beats, beats[1:])]
    if not gaps:
        raise ValueError(f"track {label['track']} has fewer than two beats")
    return float(median(gaps)) / BIN_FRAMES


def _phase_error_beats(
    label: dict[str, object], first_bin: int, lag: int, phase: int
) -> float:
    errors = []
    for beat_frame in label["beat_frames"]:
        relative = float(beat_frame) / BIN_FRAMES - first_bin - phase
        remainder = relative % lag
        distance = min(remainder, lag - remainder)
        errors.append(distance / lag)
    return float(median(errors))


def _load_trace(path: Path) -> tuple[dict[str, object], np.ndarray]:
    trace = json.loads(path.read_text(encoding="utf-8"))
    flux = np.asarray(trace["onset_flux"], dtype=np.float64)
    stride = int(trace.get("onset_band_stride", 0))
    energy_flat = np.asarray(trace.get("onset_band_energy", []), dtype=np.float64)
    if stride != 4 or len(energy_flat) != len(flux) * stride:
        raise ValueError(f"{path.name}: invalid onset-band trace geometry")
    energy = energy_flat.reshape((-1, stride))
    if not np.all(np.isfinite(flux)) or not np.all(np.isfinite(energy)):
        raise ValueError(f"{path.name}: non-finite trace value")
    return trace, energy


def evaluate(labels_path: Path, traces: Path, corpus: str) -> dict[str, object]:
    labels_raw = json.loads(labels_path.read_text(encoding="utf-8"))
    labels = {
        str(row["track"]): row
        for row in labels_raw
        if row.get("split") == "development"
    }
    if len(labels) != len(labels_raw) / 2:
        # Both currently frozen protocols are balanced development/holdout.
        # Refuse an unexpected label topology rather than risk consuming a
        # holdout through a caller-supplied trace directory.
        raise ValueError("labels must contain one balanced development split")
    paths = sorted(traces.glob("track-*.ndjson"))
    if {path.stem for path in paths} != set(labels):
        raise ValueError("trace coverage does not exactly match development IDs")

    per_formula: dict[str, list[dict[str, object]]] = {
        name: [] for name in FORMULAS
    }
    reconstructed_differences = 0
    captured_candidate_set_matches = 0
    for path in paths:
        track = path.stem
        label = labels[track]
        trace, energy = _load_trace(path)
        captured = np.asarray(trace["onset_flux"], dtype=np.float64)
        reconstructed = _baseline_band_flux(energy)
        if not np.allclose(captured, reconstructed, rtol=2.0e-5, atol=2.0e-7):
            reconstructed_differences += 1
        first_bin = int(trace["onset_evidence_first_bin"])
        truth_period = _truth_period_bins(label, first_bin, len(captured))

        for name, formula in FORMULAS.items():
            novelty = captured if formula is None else formula(energy)
            candidates = _candidate_lags(novelty, int(trace["sample_rate"]))
            rows = []
            for lag, score in candidates:
                phase = _selected_phase(novelty, lag)
                period_error = abs(lag - truth_period) / truth_period
                phase_error = _phase_error_beats(label, first_bin, lag, phase)
                rows.append(
                    {
                        "lag": lag,
                        "score": score,
                        "period_error": period_error,
                        "phase_error_beats": phase_error,
                    }
                )
            per_formula[name].append({"track": track, "candidates": rows})

            if name == "captured_multiband":
                traced = {
                    int(row["lag_bins"])
                    for row in trace.get("tempo_candidates", [])
                    if int(row["lag_bins"]) > 0
                }
                computed = {int(row["lag"]) for row in rows}
                if traced == computed:
                    captured_candidate_set_matches += 1

    verdicts: dict[str, dict[str, dict[str, bool]]] = {}
    summaries: dict[str, object] = {}
    for name, tracks in per_formula.items():
        top1_period = 0
        top1_phase = 0
        top1_joint = 0
        top3_period = 0
        top3_joint = 0
        formula_verdicts: dict[str, dict[str, bool]] = {}
        for track in tracks:
            candidates = track["candidates"]
            if not candidates:
                continue
            first = candidates[0]
            period_ok = float(first["period_error"]) <= PERIOD_TOLERANCE
            phase_ok = float(first["phase_error_beats"]) <= PHASE_TOLERANCE_BEATS
            joint_ok = period_ok and phase_ok
            top3_period_ok = any(
                float(row["period_error"]) <= PERIOD_TOLERANCE
                for row in candidates
            )
            top3_joint_ok = any(
                float(row["period_error"]) <= PERIOD_TOLERANCE
                and float(row["phase_error_beats"]) <= PHASE_TOLERANCE_BEATS
                for row in candidates
            )
            top1_period += period_ok
            top1_phase += phase_ok
            top1_joint += joint_ok
            top3_period += top3_period_ok
            top3_joint += top3_joint_ok
            formula_verdicts[str(track["track"])] = {
                "top1_period": period_ok,
                "top1_phase": phase_ok,
                "top1_period_phase": joint_ok,
                "top3_period": top3_period_ok,
                "top3_period_phase": top3_joint_ok,
            }
        verdicts[name] = formula_verdicts
        summaries[name] = {
            "top1_period_correct": top1_period,
            "top1_phase_correct": top1_phase,
            "top1_period_phase_correct": top1_joint,
            "top3_period_oracle": top3_period,
            "top3_period_phase_oracle": top3_joint,
            "track_count": len(tracks),
        }

    baseline_verdicts = verdicts["captured_multiband"]
    for name, formula_verdicts in verdicts.items():
        transitions: dict[str, object] = {}
        for metric in (
            "top1_period",
            "top1_phase",
            "top1_period_phase",
            "top3_period",
            "top3_period_phase",
        ):
            fixes = sorted(
                track
                for track in baseline_verdicts
                if not baseline_verdicts[track][metric]
                and formula_verdicts[track][metric]
            )
            breaks = sorted(
                track
                for track in baseline_verdicts
                if baseline_verdicts[track][metric]
                and not formula_verdicts[track][metric]
            )
            transitions[metric] = {
                "fixes": len(fixes),
                "breaks": len(breaks),
                "net_fixes": len(fixes) - len(breaks),
                "fix_track_ids": fixes,
                "break_track_ids": breaks,
            }
        summaries[name]["transitions_from_captured"] = transitions

    return {
        "format": FORMAT,
        "acceptance_claim": False,
        "evidence_level": "development",
        "corpus": corpus,
        "track_count": len(labels),
        "inputs": {
            "labels_sha256": _sha256(labels_path),
            "trace_count": len(paths),
        },
        "validation": {
            "reconstructed_baseline_difference_tracks": reconstructed_differences,
            "captured_top3_candidate_set_matches": captured_candidate_set_matches,
        },
        "formulas": summaries,
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
        encoded = json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.exit(1, f"error: {exc}\n")
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
