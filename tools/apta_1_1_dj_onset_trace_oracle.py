#!/usr/bin/env python3
"""Compare fixed onset formulas on the frozen, already-spent DJ corpus.

This tool is deliberately separate from the ASAP/Ballroom development oracle.
It accepts only the privacy-safe DJ manifest and CSV schema, verifies their
binding, and always emits non-acceptance, spent-development evidence.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path
from statistics import median

try:
    import numpy as np
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit(
        "error: apta_1_1_dj_onset_trace_oracle.py requires numpy"
    ) from exc

from apta_1_1_onset_trace_oracle import (  # noqa: E402
    BIN_FRAMES,
    FORMULAS,
    PHASE_ONLY_FORMULAS,
    PERIOD_TOLERANCE,
    PHASE_TOLERANCE_BEATS,
    _baseline_band_flux,
    _candidate_lags,
    _load_trace,
    _selected_phase,
    _sha256,
)


FORMAT = "apta-1.1-dj-onset-trace-oracle-1"
MANIFEST_FORMAT = "apta-1.1-dj-validation-1"
TRACK_PATTERN = re.compile(r"track-[0-9a-f]{24}")
LABEL_FIELDS = {
    "track",
    "key_tonic",
    "key_mode",
    "meter_numerator",
    "meter_denominator",
    "downbeat_frame",
    "beat_period_frames",
}


def _trace_set_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.name.encode("ascii"))
        digest.update(b"\0")
        digest.update(_sha256(path).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _load_inputs(
    manifest_path: Path, labels_path: Path, traces: Path
) -> tuple[dict[str, object], dict[str, dict[str, str]], list[Path]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != MANIFEST_FORMAT:
        raise ValueError("unexpected spent DJ manifest format")
    manifest_ids_raw = manifest.get("track_ids")
    if not isinstance(manifest_ids_raw, list):
        raise ValueError("spent DJ manifest has no track_ids array")
    manifest_ids = [str(value) for value in manifest_ids_raw]
    if any(TRACK_PATTERN.fullmatch(value) is None for value in manifest_ids):
        raise ValueError("spent DJ manifest contains a non-opaque track ID")
    if len(manifest_ids) != len(set(manifest_ids)):
        raise ValueError("spent DJ manifest contains duplicate track IDs")
    if int(manifest.get("track_count", -1)) != len(manifest_ids):
        raise ValueError("spent DJ manifest track_count does not match IDs")
    if manifest.get("reference_source") is None:
        raise ValueError("spent DJ manifest has no reference_source")

    if manifest.get("labels_sha256") != _sha256(labels_path):
        raise ValueError("labels CSV hash does not match spent DJ manifest")
    with labels_path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if set(reader.fieldnames or []) != LABEL_FIELDS:
            raise ValueError("unexpected spent DJ labels CSV schema")
        rows = list(reader)
    labels: dict[str, dict[str, str]] = {}
    for row in rows:
        track = str(row["track"])
        if TRACK_PATTERN.fullmatch(track) is None:
            raise ValueError("spent DJ labels contain a non-opaque track ID")
        if track in labels:
            raise ValueError("spent DJ labels contain duplicate track IDs")
        labels[track] = row
    if set(labels) != set(manifest_ids):
        raise ValueError("spent DJ labels do not exactly match manifest IDs")

    paths = sorted(traces.glob("track-*.ndjson"))
    if {path.stem for path in paths} != set(manifest_ids):
        raise ValueError("trace coverage does not exactly match spent DJ IDs")
    if len(paths) != len(manifest_ids):
        raise ValueError("trace directory contains duplicate or missing IDs")
    return manifest, labels, paths


def _truth_period_bins(label: dict[str, str]) -> float:
    period = float(label["beat_period_frames"])
    if not math.isfinite(period) or period <= 0.0:
        raise ValueError(f"track {label['track']} has invalid beat period")
    return period / BIN_FRAMES


def _truth_beats(
    label: dict[str, str], first_bin: int, count: int
) -> list[float]:
    period = float(label["beat_period_frames"])
    downbeat = float(label["downbeat_frame"])
    if not math.isfinite(downbeat) or downbeat < 0.0:
        raise ValueError(f"track {label['track']} has invalid downbeat")
    first_frame = first_bin * BIN_FRAMES
    end_frame = (first_bin + count) * BIN_FRAMES
    first_ordinal = math.floor((first_frame - downbeat) / period) - 1
    last_ordinal = math.ceil((end_frame - downbeat) / period) + 1
    beats = [
        downbeat + ordinal * period
        for ordinal in range(first_ordinal, last_ordinal + 1)
        if first_frame <= downbeat + ordinal * period < end_frame
    ]
    if not beats:
        raise ValueError(f"track {label['track']} has no beat in trace window")
    return beats


def _phase_error_beats(
    beats: list[float], first_bin: int, lag: int, phase: int
) -> float:
    errors = []
    for beat_frame in beats:
        relative = beat_frame / BIN_FRAMES - first_bin - phase
        remainder = relative % lag
        errors.append(min(remainder, lag - remainder) / lag)
    return float(median(errors))


def _verdicts(
    per_formula: dict[str, list[dict[str, object]]]
) -> tuple[
    dict[str, dict[str, dict[str, bool]]], dict[str, dict[str, object]]
]:
    verdicts: dict[str, dict[str, dict[str, bool]]] = {}
    summaries: dict[str, dict[str, object]] = {}
    for name, tracks in per_formula.items():
        counts = {
            "top1_period": 0,
            "top1_phase": 0,
            "top1_period_phase": 0,
            "top3_period": 0,
            "top3_period_phase": 0,
        }
        formula_verdicts: dict[str, dict[str, bool]] = {}
        for track in tracks:
            candidates = track["candidates"]
            if not isinstance(candidates, list) or not candidates:
                continue
            first = candidates[0]
            period_ok = float(first["period_error"]) <= PERIOD_TOLERANCE
            phase_ok = (
                float(first["phase_error_beats"]) <= PHASE_TOLERANCE_BEATS
            )
            top3_period_ok = any(
                float(row["period_error"]) <= PERIOD_TOLERANCE
                for row in candidates
            )
            top3_joint_ok = any(
                float(row["period_error"]) <= PERIOD_TOLERANCE
                and float(row["phase_error_beats"])
                <= PHASE_TOLERANCE_BEATS
                for row in candidates
            )
            row_verdicts = {
                "top1_period": period_ok,
                "top1_phase": phase_ok,
                "top1_period_phase": period_ok and phase_ok,
                "top3_period": top3_period_ok,
                "top3_period_phase": top3_joint_ok,
            }
            formula_verdicts[str(track["track"])] = row_verdicts
            for metric, result in row_verdicts.items():
                counts[metric] += result
        verdicts[name] = formula_verdicts
        summaries[name] = {
            "top1_period_correct": counts["top1_period"],
            "top1_phase_correct": counts["top1_phase"],
            "top1_period_phase_correct": counts["top1_period_phase"],
            "top3_period_oracle": counts["top3_period"],
            "top3_period_phase_oracle": counts["top3_period_phase"],
            "track_count": len(tracks),
        }
    return verdicts, summaries


def evaluate(
    manifest_path: Path, labels_path: Path, traces: Path
) -> dict[str, object]:
    _manifest, labels, paths = _load_inputs(
        manifest_path, labels_path, traces
    )
    per_formula: dict[str, list[dict[str, object]]] = {
        name: [] for name in (*FORMULAS, *PHASE_ONLY_FORMULAS)
    }
    reconstructed_differences = 0
    captured_candidate_set_matches = 0
    hybrid_candidate_order_matches = 0
    for path in paths:
        track = path.stem
        label = labels[track]
        trace, energy = _load_trace(path)
        trace_track = Path(str(trace.get("track", ""))).stem
        if trace_track != track or TRACK_PATTERN.fullmatch(trace_track) is None:
            raise ValueError(f"{path.name}: trace source is not its opaque ID")
        captured = np.asarray(trace["onset_flux"], dtype=np.float64)
        reconstructed = _baseline_band_flux(energy)
        if not np.allclose(
            captured, reconstructed, rtol=2.0e-5, atol=2.0e-7
        ):
            reconstructed_differences += 1
        first_bin = int(trace["onset_evidence_first_bin"])
        truth_period = _truth_period_bins(label)
        beats = _truth_beats(label, first_bin, len(captured))

        formulas = {
            **FORMULAS,
            **PHASE_ONLY_FORMULAS,
        }
        for name, formula in formulas.items():
            phase_only = name in PHASE_ONLY_FORMULAS
            phase_novelty = captured if formula is None else formula(energy)
            candidate_novelty = captured if phase_only else phase_novelty
            candidates = _candidate_lags(
                candidate_novelty, int(trace["sample_rate"])
            )
            rows = []
            for lag, score in candidates:
                phase = _selected_phase(phase_novelty, lag)
                rows.append(
                    {
                        "lag": lag,
                        "score": score,
                        "period_error": abs(lag - truth_period) / truth_period,
                        "phase_error_beats": _phase_error_beats(
                            beats, first_bin, lag, phase
                        ),
                    }
                )
            per_formula[name].append({"track": track, "candidates": rows})
            if name == "captured_multiband":
                traced = {
                    int(row["lag_bins"])
                    for row in trace.get("tempo_candidates", [])
                    if int(row["lag_bins"]) > 0
                }
                if traced == {int(row["lag"]) for row in rows}:
                    captured_candidate_set_matches += 1
            elif phase_only:
                baseline_candidates = _candidate_lags(
                    captured, int(trace["sample_rate"])
                )
                if candidates == baseline_candidates:
                    hybrid_candidate_order_matches += 1

    verdicts, summaries = _verdicts(per_formula)
    baseline = verdicts["captured_multiband"]
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
                for track in baseline
                if not baseline[track][metric]
                and formula_verdicts[track][metric]
            )
            breaks = sorted(
                track
                for track in baseline
                if baseline[track][metric]
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
        "corpus": "frozen-dj-60",
        "corpus_status": "spent",
        "track_count": len(labels),
        "inputs": {
            "manifest_sha256": _sha256(manifest_path),
            "labels_sha256": _sha256(labels_path),
            "trace_count": len(paths),
            "trace_set_sha256": _trace_set_sha256(paths),
        },
        "validation": {
            "exact_manifest_coverage": True,
            "privacy_safe_opaque_ids": True,
            "reconstructed_baseline_difference_tracks": (
                reconstructed_differences
            ),
            "captured_top3_candidate_set_matches": (
                captured_candidate_set_matches
            ),
            "hybrid_candidate_order_matches": (
                hybrid_candidate_order_matches
            ),
        },
        "formulas": summaries,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = evaluate(
            args.manifest, args.labels, args.traces
        )
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
