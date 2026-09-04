#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Audit frozen development lag coverage; never select a production candidate."""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
from pathlib import Path

import numpy as np

from apta_1_1_beat_path_oracle import _trace_set_sha256
from apta_1_1_onset_trace_oracle import (
    BIN_FRAMES, MAX_BPM, MIN_BPM, PERIOD_TOLERANCE,
    _candidate_lags, _sha256, _tempo_prior, _truth_period_bins,
)

FORMAT = "apta-1.1-lattice-coverage-audit-1"
OPAQUE_ID = re.compile(r"track-[0-9a-f]{24}")
PINS = {
    "ASAP": (
        "7f6d5e40c8771df6052fd31f46a0fb0d0e95ab990c8d89f00c4ef1fe41d882c0",
        "72089f349db6fef80cd50c30d5a74df17ca75cc9ec4149e576c35f884d048963",
    ),
    "Ballroom": (
        "5aa77e0b23233a38480d43a77b67f63f2407c91ac444073efce1bf8f0214d323",
        "2a1fd33d10a2fb93faf3d82c79a56c12c23d88152bb6728b148ef686f43a47b8",
    ),
}


def full_scan(novelty: np.ndarray, sample_rate: int) -> list[dict]:
    if (novelty.shape != (4096,) or not np.all(np.isfinite(novelty))
            or np.any(novelty < 0)):
        raise ValueError("expected finite non-negative 4096-bin vector")
    if type(sample_rate) is not int or not 8000 <= sample_rate <= 192000:
        raise ValueError("invalid sample rate")
    count = len(novelty)
    minimum = math.ceil(sample_rate * 60 / (MAX_BPM * BIN_FRAMES))
    maximum = min(math.floor(sample_rate * 60 / (MIN_BPM * BIN_FRAMES)), count // 2)
    nfft = 1 << (2 * count - 1).bit_length()
    spectrum = np.fft.rfft(novelty, nfft)
    correlation = np.fft.irfft(spectrum * np.conjugate(spectrum), nfft)[:count]
    squares = np.concatenate((np.zeros(1), np.cumsum(novelty * novelty)))
    rows = []
    for lag in range(minimum, maximum + 1):
        left, right = squares[count] - squares[lag], squares[count - lag]
        raw = (max(float(correlation[lag] / math.sqrt(left * right)), 0.0)
               if left > 1e-12 and right > 1e-12 else 0.0)
        prior = _tempo_prior(sample_rate * 60000.0 / (lag * BIN_FRAMES))
        rows.append({"lag": lag, "correlation": raw, "score": raw * prior})
    return rows


def ranked(rows: list[dict], field: str = "score") -> list[dict]:
    return sorted((r for r in rows if r[field] > 0), key=lambda r: (-r[field], r["lag"]))


def local_maxima(rows: list[dict]) -> list[dict]:
    """One lowest-lag representative per exact plateau, including endpoints."""
    peaks = []
    i = 0
    while i < len(rows):
        end = i
        score = rows[i]["score"]
        while end + 1 < len(rows) and rows[end + 1]["score"] == score:
            end += 1
        if (score > 0 and (i == 0 or score > rows[i - 1]["score"])
                and (end + 1 == len(rows) or score > rows[end + 1]["score"])):
            peaks.append(rows[i])
        i = end + 1
    return ranked(peaks)


def first_correct_rank(rows: list[dict], truth: float) -> int | None:
    if not math.isfinite(truth) or truth <= 0:
        raise ValueError("invalid truth period")
    return next((i for i, row in enumerate(rows, 1)
                 if abs(row["lag"] - truth) / truth <= PERIOD_TOLERANCE), None)


def development_labels(raw: object) -> dict[str, dict]:
    if not isinstance(raw, list) or len(raw) != 80:
        raise ValueError("expected 40 development and 40 holdout rows")
    seen: set[str] = set()
    counts = {(s, m): 0 for s in ("development", "holdout") for m in (3, 4)}
    development = {}
    for row in raw:
        if not isinstance(row, dict):
            raise ValueError("invalid label row")
        track = row.get("track")
        if not isinstance(track, str) or not OPAQUE_ID.fullmatch(track) or track in seen:
            raise ValueError("duplicate or non-opaque track ID")
        seen.add(track)
        key = row.get("split"), row.get("meter_numerator")
        if key not in counts or row.get("meter_denominator") != 4:
            raise ValueError("invalid split or meter")
        counts[key] += 1
        if key[0] == "development":
            beats = row.get("beat_frames")
            if (not isinstance(beats, list) or len(beats) < 2
                    or any(type(b) is not int or b < 0 for b in beats)
                    or any(a >= b for a, b in zip(beats, beats[1:]))):
                raise ValueError("invalid development beat sequence")
            development[track] = row
    if set(counts.values()) != {20}:
        raise ValueError("split is not balanced by meter")
    return development


def evaluate(labels_path: Path, traces: Path, corpus: str, revision: str) -> dict:
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("full tool revision required")
    labels_hash, trace_hash = PINS[corpus]
    if _sha256(labels_path) != labels_hash:
        raise ValueError("frozen labels hash mismatch")
    labels = development_labels(json.loads(labels_path.read_text(encoding="utf-8")))
    paths = sorted(traces.glob("*.ndjson"))
    if {p.stem for p in paths} != set(labels) or len(paths) != 40:
        raise ValueError("trace coverage must match development IDs only")
    if _trace_set_sha256(paths) != trace_hash:
        raise ValueError("frozen trace-set hash mismatch")
    result = []
    for path in paths:
        trace = json.loads(path.read_text(encoding="utf-8"))
        rate, first = trace.get("sample_rate"), trace.get("onset_evidence_first_bin")
        if type(first) is not int or first < 0:
            raise ValueError("invalid evidence origin")
        novelty = np.asarray(trace.get("onset_flux"), dtype=np.float64)
        scan = full_scan(novelty, rate)
        weighted, raw, peaks = ranked(scan), ranked(scan, "correlation"), local_maxima(scan)
        calculated = [r["lag"] for r in weighted[:3]]
        captured = [r["lag_bins"] for r in trace.get("tempo_candidates", []) if r["lag_bins"] > 0]
        if calculated != captured or calculated != [lag for lag, _ in _candidate_lags(novelty, rate)]:
            raise ValueError("captured/frozen/recomputed top-three mismatch")
        label = labels[path.stem]
        truth = _truth_period_bins(label, first, len(novelty))
        beats = label["beat_frames"]
        gaps = [(b - a) / BIN_FRAMES for a, b in zip(beats, beats[1:])
                if b >= first * BIN_FRAMES and a < (first + len(novelty)) * BIN_FRAMES]
        if not gaps:
            raise ValueError("no annotated interval intersects evidence")
        rank, peak_rank = first_correct_rank(weighted, truth), first_correct_rank(peaks, truth)
        result.append({
            "track": path.stem, "meter": label["meter_numerator"],
            "truth_period_bins": truth,
            "within_scan_range": scan[0]["lag"] <= truth <= scan[-1]["lag"],
            "integer_lag_representable": first_correct_rank(scan, truth) is not None,
            "weighted_correct_rank": rank,
            "raw_correct_rank": first_correct_rank(raw, truth),
            "local_peak_correct_rank": peak_rank,
            "top3_period": rank is not None and rank <= 3,
            "local_peak_top3_period": peak_rank is not None and peak_rank <= 3,
            "top3_adjacent_pairs": sum(abs(a - b) == 1 for i, a in enumerate(calculated)
                                        for b in calculated[i + 1:]),
            "intervals_within_one_percent": sum(abs(g - truth) / truth <= PERIOD_TOLERANCE for g in gaps),
            "interval_count": len(gaps),
        })
    summary = {field: sum(bool(row[field]) for row in result) for field in (
        "within_scan_range", "integer_lag_representable", "top3_period", "local_peak_top3_period")}
    summary.update({
        "tracks_with_adjacent_top3_bins": sum(r["top3_adjacent_pairs"] > 0 for r in result),
        "peak_coverage_fixes": sum(not r["top3_period"] and r["local_peak_top3_period"] for r in result),
        "peak_coverage_breaks": sum(r["top3_period"] and not r["local_peak_top3_period"] for r in result),
    })
    return {"format": FORMAT, "acceptance_claim": False, "candidate_promoted": False,
            "evidence_level": "development-diagnostic", "corpus": corpus,
            "tool_revision": revision, "track_count": 40,
            "inputs": {"labels_sha256": labels_hash, "trace_set_sha256": trace_hash},
            "summary": summary, "tracks": result}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--corpus", choices=tuple(PINS), required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--git-executable", default="git",
                        help="Git executable owning this checkout (git.exe for a Windows worktree under WSL)")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        root = Path(__file__).resolve().parents[1]
        head = subprocess.check_output([args.git_executable, "rev-parse", "HEAD"], cwd=root, text=True).strip()
        dirty = subprocess.check_output(
            [args.git_executable, "--no-optional-locks", "status", "--porcelain"], cwd=root, text=True)
        if head != args.source_revision or dirty.strip():
            raise ValueError("diagnostic requires its exact clean source revision")
        if args.output.exists():
            raise ValueError("refusing to overwrite existing diagnostic")
        report = evaluate(args.labels, args.traces, args.corpus, args.source_revision)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, sort_keys=True, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        parser.exit(2, f"error: {exc}\n")
    print(json.dumps(report["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
