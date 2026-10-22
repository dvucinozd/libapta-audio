#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Stage A: complete native dense capture on declared development inputs only.

No selector or label scoring. Bind unchanged ordinary outputs to the exact
retained annotated baseline; raw traces and input manifests stay private.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path

import apta_1_1_native_annotated_development as annotated
import apta_1_1_native_history_development as history

FORMAT = "libapta-native-dense-capture-development-1"


def capture(prepared: Path, analyzer: Path, baseline: Path, expected_baseline: str,
            output: Path, revision: str, inventory: str) -> dict:
    if not history.HEX.fullmatch(expected_baseline) or history.sha256_file(baseline) != expected_baseline:
        raise ValueError("retained baseline hash mismatch")
    manifest, _labels = annotated.load_development(prepared)
    retained = json.loads(baseline.read_text())
    manifest_hash = history.sha256_file(prepared / "manifest.json")
    if (retained.get("format") != annotated.FORMAT or retained.get("status") != "baseline_only"
        or retained.get("formal_acceptance") is not False or retained.get("tracks") != 40
        or retained.get("prepared_manifest_sha256") != manifest_hash
        or retained.get("labels_sha256") != manifest["labels_sha256"]):
        raise ValueError("retained development baseline identities differ")
    coverage_path = baseline.parent / "native/report.json"
    coverage_hash = history.sha256_file(coverage_path)
    if coverage_hash != retained["coverage_report_sha256"]:
        raise ValueError("retained native coverage hash mismatch")
    old = json.loads(coverage_path.read_text())
    if old["analyzer_sha256"] != retained["analyzer_sha256"]:
        raise ValueError("retained analyzer identity mismatch")
    expected = {row["track"]: row["outputs_sha256"] for row in old["outputs"]}
    if len(expected) != 40 or sorted(expected) != manifest["track_ids"]:
        raise ValueError("retained native cohort differs")
    # Preflight every retained output before invoking any native process.
    for track, hashes in expected.items():
        for mode in ("coarse", "attack", "novelty"):
            if history.sha256_file(coverage_path.parent / f"{track}-{mode}.json") != hashes[mode]:
                raise ValueError("retained native output changed")
    output.mkdir(parents=True, exist_ok=False)
    input_manifest = output / "input.private.json"
    frozen = history.freeze(prepared / "audio", input_manifest, 40, inventory, 30)
    if [row["track"] for row in frozen["tracks"]] != manifest["track_ids"]:
        raise ValueError("dense source identity differs from labels")
    native = history.run(input_manifest, prepared / "audio", analyzer, output / "native", revision, "novelty", dense=True)
    unchanged = 0
    bins = windows = weak = startup = 0
    for row in native["outputs"]:
        track = row["track"]
        for mode in ("coarse", "attack", "novelty"):
            if row["outputs_sha256"][mode] != expected[track][mode]:
                raise ValueError("dense build altered a retained ordinary output")
            unchanged += 1
        value = json.loads((output / "native" / f"{track}-novelty-dense.json").read_text())
        for window in value["windows"]:
            trace = window["dense_trace"]
            bins += trace["count"]
            startup += trace["startup_bins"]
            windows += 1
            weak += sum(0.0 < v <= 0.20 for v in trace["values"])
    if (history.sha256_file(baseline) != expected_baseline or history.sha256_file(coverage_path) != coverage_hash
        or history.sha256_file(prepared / "manifest.json") != manifest_hash
        or history.sha256_file(prepared / "labels.json") != manifest["labels_sha256"]):
        raise ValueError("retained evidence or references changed during capture")
    report = {"format": FORMAT, "status": "dense_capture_only", "formal_acceptance": False,
              "selector_implemented": False, "selector_scored": False, "accuracy": None,
              "source_revision": revision, "candidate_inventory_sha256": inventory,
              "analyzer_sha256": native["analyzer_sha256"], "baseline_analyzer_sha256": retained["analyzer_sha256"],
              "baseline_dense_report_sha256": expected_baseline, "prepared_manifest_sha256": manifest_hash,
              "labels_sha256": manifest["labels_sha256"], "tracks": 40,
              "native_report_sha256": history.sha256_file(output / "native/report.json"),
              "unchanged_ordinary_files": unchanged, "dense_windows": windows, "dense_bins": bins,
              "positive_subthreshold_bins": weak, "startup_bins": startup,
              "holdout_performances_opened": False, "physical_p4": "not run"}
    history.write_new(output / "report.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prepared", "analyzer", "baseline", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("expected-baseline-sha256", "source-revision", "candidate-inventory-sha256"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    try:
        result = capture(args.prepared, args.analyzer, args.baseline, args.expected_baseline_sha256,
                         args.output, args.source_revision, args.candidate_inventory_sha256)
    except (OSError, ValueError, KeyError, annotated.canonical.ValidationError) as exc:
        parser.exit(1, f"dense development capture failed: {exc}\n")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
