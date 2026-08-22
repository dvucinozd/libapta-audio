#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Evaluate a frozen APTA 1.1 tempo/grid ensemble corpus.

This tool intentionally contains no tuning knobs.  It compares a retained
baseline and one candidate on the same opaque-ID manifest and enforces the
pre-registered Task-5 acceptance rules from
``docs/status/APTA-1.1-TEMPO-ENSEMBLE-EVALUATION.md``.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

FORMAT = "apta-tempo-ensemble-evaluation-1"
METRICAL_RELATIONS = {
    "half",
    "half-time",
    "double",
    "double-time",
    "third",
    "triple",
    "quarter",
    "quadruple",
    "two-thirds",
    "three-half",
    "three-halves",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _as_bool(value: str) -> bool:
    return value.strip().casefold() in {"1", "true", "yes", "y"}


def read_results(path: Path) -> dict[str, dict[str, object]]:
    rows: dict[str, dict[str, object]] = {}
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"track", "truth_millibpm", "reported_millibpm", "relation", "confidence"}
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            missing = sorted(required - set(reader.fieldnames or []))
            raise ValueError(f"{path}: missing required columns: {', '.join(missing)}")
        for raw in reader:
            track = raw["track"].strip()
            if not track or track in rows:
                raise ValueError(f"{path}: empty or duplicate track id: {track!r}")
            truth = int(raw["truth_millibpm"])
            reported = int(raw["reported_millibpm"])
            confidence = int(raw["confidence"])
            relation = raw["relation"].strip().casefold()
            if truth <= 0 or reported <= 0:
                raise ValueError(f"{path}: non-positive tempo for {track}")
            if not 0 <= confidence <= 100:
                raise ValueError(f"{path}: invalid confidence for {track}")
            exact = abs(reported - truth) * 100 <= truth
            metrical = relation in METRICAL_RELATIONS or _as_bool(raw.get("octave_error", ""))
            rows[track] = {
                "truth": truth,
                "reported": reported,
                "confidence": confidence,
                "relation": relation,
                "exact": exact,
                "metrical_error": metrical and not exact,
            }
    return rows


def read_manifest(path: Path) -> tuple[list[str], dict[str, object]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("manifest root must be an object")
    tracks = data.get("tracks")
    if not isinstance(tracks, list) or not tracks:
        raise ValueError("manifest tracks must be a non-empty list")
    ids: list[str] = []
    for item in tracks:
        if isinstance(item, str):
            track_id = item
        elif isinstance(item, dict) and isinstance(item.get("id"), str):
            track_id = item["id"]
        else:
            raise ValueError("manifest tracks must contain opaque ids")
        if not track_id or track_id in ids:
            raise ValueError(f"manifest contains empty or duplicate id: {track_id!r}")
        ids.append(track_id)
    return ids, data


def summarize(rows: dict[str, dict[str, object]], ids: list[str]) -> dict[str, object]:
    exact = sum(bool(rows[track]["exact"]) for track in ids)
    high_errors = sum(
        not bool(rows[track]["exact"]) and int(rows[track]["confidence"]) >= 75
        for track in ids
    )
    high_metrical = sum(
        bool(rows[track]["metrical_error"]) and int(rows[track]["confidence"]) >= 75
        for track in ids
    )
    relations = Counter(str(rows[track]["relation"]) for track in ids)
    return {
        "tracks": len(ids),
        "within_1_percent": exact,
        "high_confidence_errors": high_errors,
        "high_confidence_metrical_errors": high_metrical,
        "relations": dict(sorted(relations.items())),
    }


def evaluate(
    ids: list[str],
    baseline: dict[str, dict[str, object]],
    candidate: dict[str, dict[str, object]],
) -> dict[str, object]:
    expected = set(ids)
    if set(baseline) != expected:
        raise ValueError("baseline ids do not exactly match frozen manifest")
    if set(candidate) != expected:
        raise ValueError("candidate ids do not exactly match frozen manifest")

    base_summary = summarize(baseline, ids)
    cand_summary = summarize(candidate, ids)
    fixed: list[str] = []
    broken: list[str] = []
    changed: list[str] = []
    for track in ids:
        b = baseline[track]
        c = candidate[track]
        if b["reported"] != c["reported"]:
            changed.append(track)
        if not b["exact"] and c["exact"]:
            fixed.append(track)
        if b["exact"] and not c["exact"]:
            broken.append(track)

    gates = {
        "no_exact_accuracy_regression": cand_summary["within_1_percent"] >= base_summary["within_1_percent"],
        "no_promotion_regression": len(broken) == 0,
        "no_high_confidence_safety_regression": cand_summary["high_confidence_errors"] <= base_summary["high_confidence_errors"],
        "no_metrical_safety_regression": cand_summary["high_confidence_metrical_errors"] <= base_summary["high_confidence_metrical_errors"],
    }
    benefit = (
        cand_summary["within_1_percent"] > base_summary["within_1_percent"]
        or len(broken) < 0  # kept explicit: broken promotions cannot be negative
        or cand_summary["high_confidence_errors"] < base_summary["high_confidence_errors"]
        or cand_summary["high_confidence_metrical_errors"] < base_summary["high_confidence_metrical_errors"]
        or len(fixed) > 0
    )
    gates["demonstrated_benefit"] = benefit
    accepted = all(gates.values())
    return {
        "format": FORMAT,
        "accepted": accepted,
        "baseline": base_summary,
        "candidate": cand_summary,
        "comparison": {
            "changed_selection_count": len(changed),
            "fixed_count": len(fixed),
            "broken_count": len(broken),
            "fixed_track_ids": fixed,
            "broken_track_ids": broken,
        },
        "gates": gates,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--minimum-tracks", type=int, default=48)
    args = parser.parse_args(argv)

    ids, manifest = read_manifest(args.manifest)
    baseline = read_results(args.baseline)
    candidate = read_results(args.candidate)
    report = evaluate(ids, baseline, candidate)
    report["evidence"] = {
        "manifest_sha256": sha256_file(args.manifest),
        "baseline_sha256": sha256_file(args.baseline),
        "candidate_sha256": sha256_file(args.candidate),
        "manifest_declared_hashes": manifest.get("hashes", {}),
        "minimum_tracks": args.minimum_tracks,
        "diagnostic_only": len(ids) < args.minimum_tracks,
    }
    if len(ids) < args.minimum_tracks:
        report["accepted"] = False
        report["gates"]["minimum_fresh_set_size"] = False
    else:
        report["gates"]["minimum_fresh_set_size"] = True
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["accepted"] else 2


if __name__ == "__main__":
    sys.exit(main())
