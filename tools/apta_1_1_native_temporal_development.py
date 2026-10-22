#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""One ordered, hash-bound stage C attempt: ASAP, then Ballroom only on pass.

Uses retained native traces and the unchanged scorer. No audio conversion,
holdout mode, DSP knobs, meter borrowing, fallback or automatic promotion.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import time

import apta_1_1_native_annotated_development as annotated
import apta_1_1_native_dense_development as dense
import apta_1_1_native_temporal_model as model

history = annotated.history
FORMAT = "libapta-temporal-ordered-development-1"
ROOT = Path(__file__).resolve().parents[1]
STAGE_B_SHA256 = "d0920f01fd0c52481482fe800f5ae891d975142eb6a838bcbe6e64cf9139ddfd"
CAPTURE_SHA256 = {
    "asap": "459fc15bb7ce1df5eb1725a7e8b9328580474011650cab83b21f45372d60f736",
    "ballroom": "7cb0d72b81706a095b09b1bf2349918a6c150ab4736fd9c128e29528f69907cb",
}
FROZEN_CODE = {
    "tools/apta_1_1_native_temporal_model.py": "cf7afb76976b8d63da9c538a9aa184bf80c52836485b0a4c99dd990ad61e7227",
    "tests/unit/apta_1_1_native_temporal_model.py": "2578ab4d3fba1bf274bfd37ed7df37752003527739856db5a35246fc5dc19b8e",
    "tools/apta_1_1_native_annotated_development.py": "47b2d70e0a222a07248d1860b02b53255557e0c9d3a660a0c1f3e1c8bcb1f1cc",
    "tools/apta_1_1_native_history_development.py": "a8bc5f39e1b03051ec9bf8c387a2af568d181a93311dbd9041a5f5e5111d14b1",
    "tools/apta_1_1_native_dense_development.py": "82557642fa4541475e3e57be3174832187d99ef756c55703fba8aa56cb4201ef",
    "docs/rust/S6-TEMPORAL-MODEL-PROTOCOL.json": model.PROTOCOL_SHA256,
}
CORRECT = ("confirmed_period_windows", "confirmed_phase_windows", "confirmed_joint_windows")


def bind(path: Path, expected: str, files: dict) -> None:
    if not history.HEX.fullmatch(expected) or history.sha256_file(path) != expected:
        raise ValueError("frozen evidence hash mismatch")
    files[path] = expected


def recheck(files: dict) -> None:
    for path, digest in files.items():
        bind(path, digest, {})


def source_bindings(inventory: Path, stage_b: Path, revision: str) -> dict:
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("full source revision required")
    files = {}
    for name, digest in FROZEN_CODE.items():
        bind(ROOT / name, digest, files)
    bind(stage_b, STAGE_B_SHA256, files)
    verification = json.loads(stage_b.read_text())
    if (verification.get("status") != "stage_b_implementation_verified_not_qualified"
        or verification.get("synthetic_unit_tests_passed") != 13
        or verification.get("real_corpus_selector_scored") is not False):
        raise ValueError("stage B synthetic prerequisite missing")
    source = json.loads(inventory.read_text())
    required = set(FROZEN_CODE) | {
        "tools/apta_1_1_native_temporal_development.py",
        "tests/unit/apta_1_1_native_temporal_development.py",
    }
    if not isinstance(source, dict) or not required <= source.keys():
        raise ValueError("source inventory missing candidate files")
    for name, digest in source.items():
        path = ROOT / name
        if Path(name).is_absolute() or not path.resolve().is_relative_to(ROOT.resolve()):
            raise ValueError("source inventory path escapes repository")
        bind(path, digest, files)
    bind(inventory, history.sha256_file(inventory), files)
    return files


def preflight(corpus: str, evidence: Path, files: dict) -> dict:
    """Validate the entire forty-source cohort before the first model result."""
    declared = model.PROTOCOL["inputs"][corpus]
    prepared = evidence / f"{corpus}-development-20261022/prepared"
    baseline_path = prepared.parent / "native-analysis/report.json"
    capture = evidence / f"temporal-trace-20261022/{corpus}-capture"
    # Hash-pinned development headers are checked before opening labels/audio.
    bind(prepared / "manifest.json", declared["prepared_manifest_sha256"], files)
    bind(prepared / "labels.json", declared["labels_sha256"], files)
    manifest, labels = annotated.load_development(prepared)
    bind(baseline_path, declared["dense_baseline_report_sha256"], files)
    baseline = json.loads(baseline_path.read_text())
    bind(capture / "report.json", CAPTURE_SHA256[corpus], files)
    captured = json.loads((capture / "report.json").read_text())
    if (baseline.get("format") != annotated.FORMAT or baseline.get("status") != "baseline_only"
        or baseline.get("formal_acceptance") is not False or baseline.get("tracks") != 40
        or captured.get("format") != dense.FORMAT or captured.get("status") != "dense_capture_only"
        or captured.get("formal_acceptance") is not False or captured.get("selector_scored") is not False
        or captured.get("holdout_performances_opened") is not False
        or captured.get("dense_windows") != 240 or captured.get("unchanged_ordinary_files") != 120
        or captured.get("prepared_manifest_sha256") != declared["prepared_manifest_sha256"]
        or captured.get("labels_sha256") != declared["labels_sha256"]
        or captured.get("baseline_dense_report_sha256") != declared["dense_baseline_report_sha256"]
        or baseline.get("prepared_manifest_sha256") != declared["prepared_manifest_sha256"]
        or baseline.get("labels_sha256") != declared["labels_sha256"]):
        raise ValueError("development capture/baseline prerequisite differs")
    native_path = capture / "native/report.json"
    old_path = baseline_path.parent / "native/report.json"
    bind(native_path, captured["native_report_sha256"], files)
    bind(old_path, baseline["coverage_report_sha256"], files)
    native = json.loads(native_path.read_text())
    old = json.loads(old_path.read_text())
    bind(evidence / "temporal-trace-20261022/target/release/apta-history-diagnostic", captured["analyzer_sha256"], files)
    if (native.get("format") != history.REPORT or native.get("formal_acceptance") is not False
        or native.get("dense_traces_requested") is not True
        or native.get("analyzer_sha256") != captured["analyzer_sha256"]
        or old.get("analyzer_sha256") != model.PROTOCOL["baseline_analyzer_sha256"]):
        raise ValueError("native capture declaration differs")
    bind(capture / "input.private.json", native["manifest_sha256"], files)
    inputs = history.load_manifest(capture / "input.private.json")
    rows = {row["track"]: row for row in inputs["tracks"]}
    outputs = {row["track"]: row["outputs_sha256"] for row in native["outputs"]}
    previous = {row["track"]: row["outputs_sha256"] for row in old["outputs"]}
    for collection, length in ((inputs["tracks"], len(rows)), (native["outputs"], len(outputs)), (old["outputs"], len(previous))):
        if len(collection) != 40 or length != 40:
            raise ValueError("duplicate or incomplete native cohort")
    if any(sorted(items) != manifest["track_ids"] for items in (rows, outputs, previous)):
        raise ValueError("native cohort differs from registered labels")
    sources = []
    for truth in labels:
        track = truth["track"]
        row = rows[track]
        if row["requested_frames"] != 1440000:
            raise ValueError("registered thirty-second prefix required")
        audio = prepared / "audio" / row["file"]
        bind(audio, row["sha256"], files)
        if audio.is_symlink() or history.geometry(audio) != {key: row[key] for key in ("sample_rate", "channel_count", "source_total_frames")}:
            raise ValueError("registered source geometry differs")
        values = {}
        for mode in ("coarse", "attack", "novelty", "novelty-dense"):
            path = capture / "native" / f"{track}-{mode}.json"
            bind(path, outputs[track][mode], files)
            if mode != "novelty-dense" and outputs[track][mode] != previous[track][mode]:
                raise ValueError("retained ordinary native output differs")
            value = json.loads(path.read_text())
            history.validate_output(value, row, "novelty" if mode == "novelty-dense" else mode)
            values[mode] = value
        traced = values["novelty-dense"]
        history.validate_dense_trace(traced)
        stripped = dict(traced, windows=[{k: v for k, v in w.items() if k != "dense_trace"} for w in traced["windows"]])
        if stripped != values["novelty"]:
            raise ValueError("dense capture changed ordinary novelty output")
        sources.append({"truth": truth, "coarse": values["coarse"], "dense": traced})
    return {"corpus": corpus, "sources": sources, "baseline": baseline,
            "capture_sha256": CAPTURE_SHA256[corpus], "manifest_sha256": declared["prepared_manifest_sha256"],
            "labels_sha256": declared["labels_sha256"]}


def timing_input(native: dict) -> dict:
    fields = ("format", "mode", "sample_rate", "channel_count", "input_sha256", "finished",
              "capacity_exhausted", "provisional", "analyzed_first_frame", "source_total_frames",
              "requested_frames", "analyzed_frames")
    return {**{key: native[key] for key in fields}, "windows": [
        {key: w[key] for key in ("first_frame", "end_frame", "dense_trace")} for w in native["windows"]]}


def paired(before: dict, after: dict, old: dict, new: dict) -> dict:
    result = Counter()
    for key in CORRECT:
        result[key + "_fixes"] = int(not before[key] and bool(after[key]))
        result[key + "_breaks"] = int(bool(before[key]) and not after[key])
    result["changed_output"] = int(any(old.get(key) != new.get(key) for key in ("kind", "anchor_frame", "tempo_millibpm", "confidence")))
    result["changed_correctness"] = int(any(before[key] != after[key] for key in CORRECT))
    for key in ("high_confidence_grid_error", "high_confidence_grid_unknown_context"):
        result[key + "_regressions"] = int(after[key] > before[key])
    return dict(result)


def gates(baseline: dict, candidate: dict, changes: dict) -> dict:
    return {
        "strict_period_improvement": candidate[CORRECT[0]] > baseline[CORRECT[0]],
        "strict_joint_improvement": candidate[CORRECT[2]] > baseline[CORRECT[2]],
        **{"zero_" + key + "_breaks": changes.get(key + "_breaks", 0) == 0 for key in CORRECT},
        "no_high_confidence_safety_regression": all(changes.get(key + "_regressions", 0) == 0 for key in (
            "high_confidence_grid_error", "high_confidence_grid_unknown_context")),
    }


def run_corpus(context: dict, output: Path, bindings: dict) -> dict:
    output.mkdir(parents=True, exist_ok=False)
    totals = {key: Counter() for key in ("coarse", "temporal")}
    strata = {str(m): {key: Counter() for key in totals} for m in (3, 4)}
    changes, by_meter, reasons = Counter(), {str(m): Counter() for m in (3, 4)}, Counter()
    details, resources, file_hashes = [], [], {}
    started, cpu = time.monotonic(), time.process_time()
    retained = {row["track"]: row["windows"]["coarse"] for row in context["baseline"]["details"]}
    for number, source in enumerate(context["sources"], 1):
        truth, coarse = source["truth"], source["coarse"]
        native = timing_input(source["dense"])
        budget = model.SourceBudget()
        windows = []
        for index, window in enumerate(native["windows"]):
            proposal = model.select_window(native, index, budget=budget)
            if (proposal.get("kind") not in {"accepted", "no_valid_path", "insufficient_support", "ambiguous"}
                or proposal["kind"] == "accepted" and proposal.get("confidence") != 49):
                raise ValueError("unexpected frozen model verdict")
            # Timing-only: never borrow coarse meter/downbeat onto the new grid.
            candidate = {"first_frame": window["first_frame"], "end_frame": window["end_frame"], "selected": proposal, "meter": {}}
            before = annotated.score_window(coarse["windows"][index], truth, model.RATE)
            after = annotated.score_window(candidate, truth, model.RATE)
            if before != retained[truth["track"]][index]:
                raise ValueError("unchanged scorer no longer reproduces retained baseline")
            delta = paired(before, after, coarse["windows"][index]["selected"], proposal)
            for name, score in (("coarse", before), ("temporal", after)):
                totals[name].update(score)
                strata[str(truth["meter_numerator"])][name].update(score)
            changes.update(delta); by_meter[str(truth["meter_numerator"])].update(delta)
            reasons.update([proposal["kind"]])
            windows.append({"first_frame": window["first_frame"], "end_frame": window["end_frame"],
                            "coarse": before, "temporal": after, "proposal": proposal, "paired": delta})
        budget.check()
        resource = {"source_wall_seconds": time.monotonic() - budget.started,
                    "numeric_peak_bytes": budget.numeric_peak, "rss_peak_kib": budget.rss_peak}
        resources.append(resource)
        record = {"track": truth["track"], "meter_numerator": truth["meter_numerator"], "windows": windows, "resources": resource}
        target = output / f"{truth['track']}.json"
        history.write_new(target, record)
        file_hashes[truth["track"]] = history.sha256_file(target)
        details.append({"track": truth["track"], "report_sha256": file_hashes[truth["track"]]})
        print(json.dumps({"corpus": context["corpus"], "sources_done": number, "windows_done": totals["temporal"]["windows"],
                          "source_seconds": round(resource["source_wall_seconds"], 2)}), flush=True)
    recheck(bindings)
    for name in totals:
        if (totals[name]["windows"] != 240 or totals[name]["reference_complete_windows"] != model.PROTOCOL["inputs"][context["corpus"]]["complete_reference_windows"]):
            raise ValueError("missing source windows or reference scope changed")
    if dict(totals["coarse"]) != context["baseline"]["totals"]["coarse"]:
        raise ValueError("retained aggregate baseline differs")
    for track, digest in file_hashes.items():
        bind(output / f"{track}.json", digest, {})
    verdict = gates(totals["coarse"], totals["temporal"], changes)
    report = {"format": FORMAT, "corpus": context["corpus"], "status": "passed" if all(verdict.values()) else "rejected",
        "formal_acceptance": False, "holdout_performances_opened": False, "native_selector_implemented": False,
        "tracks": len(details), "totals": {k: dict(v) for k, v in totals.items()},
        "by_meter": {m: {k: dict(v) for k, v in modes.items()} for m, modes in strata.items()},
        "paired": dict(changes), "paired_by_meter": {m: dict(v) for m, v in by_meter.items()}, "reasons": dict(reasons),
        "gates": dict(verdict, complete_hash_bound_evidence=True, resource_caps=True),
        "input_bindings": {k: context[k] for k in ("capture_sha256", "manifest_sha256", "labels_sha256")},
        "details": details, "resources": {"wall_seconds": time.monotonic() - started, "cpu_seconds": time.process_time() - cpu,
            "max_source_seconds": max(r["source_wall_seconds"] for r in resources),
            "numeric_peak_bytes": max(r["numeric_peak_bytes"] for r in resources), "rss_peak_kib": max(r["rss_peak_kib"] for r in resources)},
        "meter_downbeat": "not implemented on new grid; coarse records never borrowed", "physical_p4": "not run"}
    history.write_new(output / "report.json", report)
    return report


def run(evidence: Path, stage_b: Path, inventory: Path, output: Path, revision: str) -> dict:
    if output.exists():
        raise ValueError("attempt output already exists; refusing overwrite/retry")
    bindings = source_bindings(inventory, stage_b, revision)
    output.mkdir(parents=True, exist_ok=False)
    completed = []
    try:
        for corpus in model.PROTOCOL["gates"]["stage_c_corpus_order"]:
            context = preflight(corpus, evidence, bindings)
            recheck(bindings)
            freeze = {"source_revision": revision, "candidate_inventory_sha256": history.sha256_file(inventory),
                      "protocol_sha256": model.PROTOCOL_SHA256, "corpus": corpus,
                      "model_sha256": FROZEN_CODE["tools/apta_1_1_native_temporal_model.py"],
                      "frozen_files": {str(p): h for p, h in bindings.items()}, "before_any_corpus_candidate_result": True}
            history.write_new(output / f"{corpus}-freeze.private.json", freeze)
            result = run_corpus(context, output / corpus, bindings)
            digest = history.sha256_file(output / corpus / "report.json")
            bind(output / corpus / "report.json", digest, bindings)
            completed.append({"corpus": corpus, "status": result["status"], "report_sha256": digest})
            if result["status"] != "passed":
                break
    except (OSError, ValueError, KeyError, model.ResourceLimit, annotated.canonical.ValidationError) as exc:
        history.write_new(output / "operational-failure.json", {"status": "operational_veto", "completed": completed, "reason": str(exc),
                                                               "formal_acceptance": False, "retry_or_rescue_allowed": False})
        raise
    report = {"format": FORMAT, "source_revision": revision, "candidate_inventory_sha256": history.sha256_file(inventory),
              "protocol_sha256": model.PROTOCOL_SHA256, "stage_b_report_sha256": STAGE_B_SHA256,
              "completed": completed, "status": "passed_both_development_gates" if len(completed) == 2 and all(r["status"] == "passed" for r in completed) else "rejected",
              "ballroom_run": len(completed) == 2, "formal_acceptance": False, "physical_p4": "not run",
              "native_selector_implemented": False, "holdout_performances_opened": False}
    history.write_new(output / "report.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("evidence-root", "stage-b-report", "source-inventory", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    args = parser.parse_args()
    try:
        result = run(args.evidence_root, args.stage_b_report, args.source_inventory, args.output, args.source_revision)
    except (OSError, ValueError, KeyError, model.ResourceLimit, annotated.canonical.ValidationError) as exc:
        parser.exit(1, f"ordered temporal development failed: {exc}\n")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
