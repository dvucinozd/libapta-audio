#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarize the fixed synthetic key-mode diagnostic, never corpus acceptance."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import re


def load_report(path, band, expected_format="apta-key-mode-diagnostic-1"):
    raw = path.read_bytes()
    data = json.loads(raw)
    if (data.get("format") != expected_format
            or data.get("acceptance_claim") is not False
            or data.get("checks_passed") is not True
            or data.get("semitone_band") is not band
            or data.get("mode_encoding") != "major=0,minor=1"
            or data.get("row_count") != 720 or len(data.get("rows", [])) != 720):
        raise ValueError("unexpected or incomplete diagnostic")
    expected = {(kind, condition, tonic, mode, window)
                for kind in ("profile", "triad", "pcm_window", "pcm_cumulative")
                for condition in range(3) for tonic in range(12) for mode in range(2)
                for window in (range(1, 5) if kind.startswith("pcm_") else (0,))}
    seen = set()
    for row in data["rows"]:
        key = tuple(row[k] for k in ("kind", "condition", "stimulus_tonic", "stimulus_mode", "window"))
        if key not in expected or key in seen:
            raise ValueError("duplicate or unexpected stimulus")
        seen.add(key)
        for field, limit in (("selected_tonic", 12), ("selected_mode", 2), ("confidence", 101)):
            if type(row[field]) is not int or not 0 <= row[field] < limit:
                raise ValueError("invalid verdict")
        for field, length in (("chroma", 12), ("reference_scores_major_then_minor", 24)):
            if len(row[field]) != length or any(type(v) not in (int, float) or not math.isfinite(v)
                                               or v < 0 for v in row[field]):
                raise ValueError("invalid evidence vector")
        scores = row["reference_scores_major_then_minor"]
        if abs(scores[row["selected_mode"] * 12 + row["selected_tonic"]] - max(scores)) > 2e-6:
            raise ValueError("native/reference mismatch")
        if sum(row["chroma"]) <= 0:
            raise ValueError("empty evidence")
    if seen != expected:
        raise ValueError("missing stimulus")
    return data["rows"], hashlib.sha256(raw).hexdigest()


def aggregate(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[(row["kind"], row["condition"], row["stimulus_mode"], row["window"])].append(row)
    result = []
    for (kind, condition, mode, window), group in sorted(groups.items()):
        # IV/V window labels describe the entire progression, not that chord's key.
        result.append({
            "kind": kind, "condition": condition, "stimulus_mode": mode, "window": window,
            "count": len(group),
            "matches_stimulus_tonic_and_mode": sum(r["selected_tonic"] == r["stimulus_tonic"]
                                                    and r["selected_mode"] == mode for r in group),
            "selected_major": sum(r["selected_mode"] == 0 for r in group),
            "parallel_mode_swaps": sum(r["selected_tonic"] == r["stimulus_tonic"]
                                       and r["selected_mode"] != mode for r in group),
            "mean_chroma_min_over_mean": sum(12 * min(r["chroma"]) / sum(r["chroma"])
                                             for r in group) / len(group),
            "mean_confidence": sum(r["confidence"] for r in group) / len(group),
        })
    return result


def summarize(default_path, band_path, native_revision):
    if not re.fullmatch(r"[0-9a-f]{40}", native_revision):
        raise ValueError("full native diagnostic revision required")
    default, dh = load_report(default_path, False)
    band, bh = load_report(band_path, True)
    return {"format": "apta-key-mode-diagnostic-summary-1", "acceptance_claim": False,
            "evidence_level": "synthetic-diagnostic", "native_source_revision": native_revision,
            "condition_encoding": {"vectors": ["floor0", "floor1", "floor4"],
                                   "pcm": ["clean", "plus_one_third_semitone", "uniform_noise_0.02"]},
            "mode_encoding": "major=0,minor=1",
            "report_sha256": {"default": dh, "semitone_band": bh},
            "default": aggregate(default), "semitone_band": aggregate(band)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--default-report", type=Path, required=True)
    parser.add_argument("--band-report", type=Path, required=True)
    parser.add_argument("--native-source-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = summarize(args.default_report, args.band_report, args.native_source_revision)
        with args.output.open("x", encoding="utf-8", newline="\n") as output:
            output.write(json.dumps(result, sort_keys=True, indent=2, allow_nan=False) + "\n")
    except (OSError, ValueError, TypeError, KeyError) as exc:
        parser.exit(2, f"error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
