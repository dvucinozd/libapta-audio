#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Unchanged native history against development-only dense ASAP/Ballroom references.

No new selector, holdout, FINAL exporter or acceptance claim. Rendered piano
stress diagnostics use actual source-local annotated beat/downbeat frames.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path

import apta_1_1_asap_meter_validation as canonical
import apta_1_1_ballroom_meter_validation as ballroom
import apta_1_1_native_history_development as history

FORMAT = "libapta-native-annotated-development-1"


def load_development(prepared: Path) -> tuple[dict, list]:
    declaration = json.loads((prepared / "manifest.json").read_text())
    corpus_format = declaration.get("format")
    if corpus_format not in (canonical.FORMAT, ballroom.FORMAT) or declaration.get("prepared_splits") != ["development"] or declaration.get("holdout_performances_opened") is not False:
        raise ValueError("explicit development-only preparation required before reading labels/audio")
    if corpus_format == canonical.FORMAT:
        if declaration.get("source_revision") != canonical.ASAP_REVISION:
            raise ValueError("pinned ASAP source revision required")
    elif (declaration.get("audio_archive_md5") != ballroom.AUDIO_ARCHIVE_MD5
          or declaration.get("annotation_archive_md5") != ballroom.ANNOTATION_ARCHIVE_MD5
          or declaration.get("selection_seed") != ballroom.SELECTION_SEED
          or declaration.get("compressed_distribution_contains_holdout") is not True):
        raise ValueError("pinned Ballroom archives/selection declaration required")
    manifest, labels = canonical.load_prepared(prepared, corpus_format)
    if len(labels) != 40 or any(row["split"] != "development" for row in labels):
        raise ValueError("exact pinned forty-row development cohort required")
    if {meter: sum(row["meter_numerator"] == meter for row in labels) for meter in (3, 4)} != {3: 20, 4: 20}:
        raise ValueError("development cohort must contain twenty rows per meter")
    return manifest, labels


def score_window(window: dict, truth: dict, rate: int) -> dict:
    beats = truth["beat_frames"]
    context = beats[0] <= window["first_frame"] and window["end_frame"] - 1 <= beats[-1]
    result = {key: 0 for key in ("emitted", "predicted_beats", "scored_beats", "phase_correct_beats", "period_correct_beats",
              "confirmed_period_windows", "confirmed_phase_windows", "confirmed_joint_windows", "meter_emitted", "meter_correct",
              "downbeat_scored", "downbeat_correct", "high_confidence_grid_error", "high_confidence_grid_unknown_context")}
    result.update(windows=1, reference_complete_windows=int(context))
    selected = window["selected"]
    if selected["kind"] != "accepted":
        return result
    result["emitted"] = 1
    period_q32 = (rate * 60000 << 32) // selected["tempo_millibpm"]
    period = period_q32 / (1 << 32)
    position = selected["anchor_frame"] << 32
    phase_good, period_good = [], []
    while position < window["end_frame"] << 32:
        result["predicted_beats"] += 1
        frame = position / (1 << 32)
        if beats[0] <= frame <= beats[-1]:
            nearest = min(beats, key=lambda value: abs(value - frame))
            local = canonical._local_period(beats, nearest)
            phase_good.append(abs(frame - nearest) <= local * canonical.DOWNBEAT_TOLERANCE_BEATS)
            period_good.append(abs(period - local) <= local * 0.01)
        position += period_q32
    result["scored_beats"] = len(phase_good)
    result["phase_correct_beats"] = sum(phase_good)
    result["period_correct_beats"] = sum(period_good)
    fully_scored = context and bool(phase_good) and len(phase_good) == result["predicted_beats"]
    result["confirmed_period_windows"] = int(fully_scored and all(period_good))
    result["confirmed_phase_windows"] = int(fully_scored and all(phase_good))
    result["confirmed_joint_windows"] = result["confirmed_period_windows"] * result["confirmed_phase_windows"]
    if selected["confidence"] >= 75:
        result["high_confidence_grid_error"] = int(context and not result["confirmed_joint_windows"])
        result["high_confidence_grid_unknown_context"] = int(not context)
    meter = window.get("meter", {})
    if meter.get("kind") == "estimated":
        result["meter_emitted"] = 1
        result["meter_correct"] = int(meter["numerator"] == truth["meter_numerator"] and meter["denominator"] == truth["meter_denominator"])
        frame = meter["downbeat_frame"] + meter["downbeat_fraction_q32"] / (1 << 32)
        if beats[0] <= frame <= beats[-1]:
            result["downbeat_scored"] = 1
            nearest = min(truth["downbeat_frames"], key=lambda value: abs(value - frame))
            local = canonical._local_period(beats, nearest)
            result["downbeat_correct"] = int(abs(frame - nearest) <= local * canonical.DOWNBEAT_TOLERANCE_BEATS)
    return result


def run(prepared: Path, analyzer: Path, output: Path, revision: str, inventory: str) -> dict:
    manifest, labels = load_development(prepared)
    manifest_hash = canonical.sha256_file(prepared / "manifest.json")
    output.mkdir(parents=True, exist_ok=False)
    input_manifest = output / "input.private.json"
    frozen = history.freeze(prepared / "audio", input_manifest, 40, inventory, 30)
    by_id = {row["track"]: row for row in frozen["tracks"]}
    if sorted(by_id) != manifest["track_ids"]:
        raise ValueError("rendered source identities differ from dense labels")
    if manifest["format"] == canonical.FORMAT and any(row["source_total_frames"] != 1440000 for row in by_id.values()):
        raise ValueError("exact thirty-second canonical excerpts required")
    coverage = history.run(input_manifest, prepared / "audio", analyzer, output / "native", revision, "novelty")
    totals = {mode: Counter() for mode in ("coarse", "attack", "novelty")}
    classes = {str(meter): {mode: Counter() for mode in totals} for meter in (3, 4)}
    sources = {row["track"]: row for row in coverage["outputs"]}
    details = []
    for truth in labels:
        track = truth["track"]
        windows_by_mode = {}
        for mode in totals:
            path = output / "native" / f"{track}-{mode}.json"
            if canonical.sha256_file(path) != sources[track]["outputs_sha256"][mode]:
                raise ValueError("native output changed before dense scoring")
            value = json.loads(path.read_text())
            history.validate_output(value, by_id[track], mode)
            scores = [score_window(window, truth, value["sample_rate"]) for window in value["windows"]]
            for score in scores:
                totals[mode].update(score)
                classes[str(truth["meter_numerator"])][mode].update(score)
            windows_by_mode[mode] = scores
        details.append({"track": track, "meter_numerator": truth["meter_numerator"], "windows": windows_by_mode})
    if canonical.sha256_file(prepared / "manifest.json") != manifest_hash or canonical.sha256_file(prepared / "labels.json") != manifest["labels_sha256"]:
        raise ValueError("dense reference metadata changed during run")
    report = {"format": FORMAT, "status": "baseline_only", "formal_acceptance": False,
              "evidence_level": ("external ASAP development, rendered piano stress; no new selector" if manifest["format"] == canonical.FORMAT
                                 else "external Ballroom development, recorded music; no new selector"),
              "source_revision": revision, "candidate_inventory_sha256": inventory, "analyzer_sha256": coverage["analyzer_sha256"],
              "prepared_manifest_sha256": manifest_hash, "labels_sha256": manifest["labels_sha256"],
              "coverage_report_sha256": canonical.sha256_file(output / "native/report.json"),
              "tracks": 40, "meter_counts": {"3": 20, "4": 20}, "holdout_performances_opened": False,
              "totals": {mode: dict(count) for mode, count in totals.items()},
              "by_meter": {meter: {mode: dict(count) for mode, count in modes.items()} for meter, modes in classes.items()},
              "details": details}
    history.write_new(output / "report.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("prepared", "analyzer", "output"):
        parser.add_argument("--" + field, type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--candidate-inventory-sha256", required=True)
    args = parser.parse_args()
    try:
        result = run(args.prepared, args.analyzer, args.output, args.source_revision, args.candidate_inventory_sha256)
    except (ValueError, OSError, KeyError) as exc:
        parser.exit(1, f"annotated development failed: {exc}\n")
    print(json.dumps({"status": result["status"], "totals": result["totals"], "by_meter": result["by_meter"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
