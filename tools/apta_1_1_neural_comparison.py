#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Freeze and score one two-model, host-only development comparison."""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import sys
import time

import apta_1_1_neural_reference as ref


def code_bindings() -> dict:
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=ref.ROOT, text=True)
    if dirty:
        raise ValueError("commit checkpoint before freezing a comparison")
    files = subprocess.check_output(["git", "ls-files", "-z"], cwd=ref.ROOT).decode().split("\0")
    return {str((ref.ROOT / f).resolve()): ref.history.sha256_file(ref.ROOT / f) for f in files if f}


def freeze(prepared: Path, assets: Path, analyzer: Path, output: Path) -> dict:
    ref.protocol()
    ref.external(output)
    if output.exists():
        raise ValueError("frozen attempt already exists")
    bindings = code_bindings()
    manifest_path = prepared / "manifest.private.json"
    manifest = json.loads(manifest_path.read_text())
    if (manifest.get("format") != ref.FORMAT or manifest.get("formal_acceptance") is not False
        or manifest.get("source_role") != "open-development" or manifest.get("protocol_sha256") != ref.PROTOCOL_SHA256
        or manifest.get("holdout_performances_opened") is not False or len(manifest["tracks"]) != 100):
        raise ValueError("registered prepared cohort required")
    ref.recheck(manifest["bindings"])
    bindings.update(manifest["bindings"])
    ref.bind(manifest_path, ref.history.sha256_file(manifest_path), bindings)
    for name, digest in (("selection.private.json", manifest["selection_sha256"]),
                         ("conversion.private.json", manifest["conversion_sha256"])):
        ref.bind(prepared / name, digest, bindings)
    ref.bind(analyzer, "b28672d90609b58b09fb10445483ad305628c9eb377910046ea9fae74f1d4d09", bindings)
    ref.bind(assets / "final0.ckpt", ref.FINAL0_SHA256, bindings)
    ref.bind(assets / "BeatNet/src/BeatNet/models/model_1_weights.pt",
             ref.protocol()["models"]["beatnet"]["weights_sha256"], bindings)
    for folder in (assets / "BeatNet", assets / "beat_this", assets / "madmom"):
        for path in folder.rglob("*.py"):
            if ".git" not in path.parts:
                ref.bind(path, ref.history.sha256_file(path), bindings)
    # Bind the installed runtime, not just the editable checkout and version strings.
    site = assets / "venv/lib/python3.12/site-packages"
    for pattern in ("*.py", "*.so"):
        for path in site.rglob(pattern):
            if path.is_file() and not path.is_symlink():
                ref.bind(path, ref.history.sha256_file(path), bindings)
    output.mkdir(parents=True)
    inputs = {"format": ref.FORMAT, "protocol_sha256": ref.PROTOCOL_SHA256,
        "prepared_manifest_sha256": ref.history.sha256_file(manifest_path), "audio_root": str((prepared / "audio").resolve()),
        "tracks": [{k: row[k] for k in ("track", "file", "sha256", "requested_frames")} for row in manifest["tracks"]]}
    ref.history.write_new(output / "inputs.private.json", inputs)
    declaration = {"format": ref.FORMAT, "formal_acceptance": False,
        "source_revision": ref.revision(ref.ROOT), "protocol_sha256": ref.PROTOCOL_SHA256,
        "prepared_manifest": str(manifest_path.resolve()), "analyzer": str(analyzer.resolve()),
        "assets": str(assets.resolve()), "bindings": bindings,
        "inputs_sha256": ref.history.sha256_file(output / "inputs.private.json"),
        "dependencies": subprocess.check_output([sys.executable, "-m", "pip", "freeze", "--all"], text=True),
        "cross_dataset_identity_verified": False}
    ref.history.write_new(output / "freeze.private.json", declaration)
    return {"tracks": 100, "bound_files": len(bindings), "freeze_sha256": ref.history.sha256_file(output / "freeze.private.json")}


def event_metrics(truth: dict, events: dict) -> dict:
    import mir_eval
    import numpy as np
    result = {}
    for name, field in (("beat", "beat_frames"), ("downbeat", "downbeat_frames")):
        actual = np.asarray(truth[field], dtype=float) / ref.RATE
        predicted = np.asarray(events[field], dtype=float) / ref.RATE
        actual = mir_eval.beat.trim_beats(actual, min_beat_time=5)
        predicted = mir_eval.beat.trim_beats(predicted, min_beat_time=5)
        if len(actual) == 0 or len(predicted) == 0:
            result[name + "_f1"] = result[name + "_cmlt"] = 0.0
        else:
            result[name + "_f1"] = float(mir_eval.beat.f_measure(actual, predicted, f_measure_threshold=.07))
            result[name + "_cmlt"] = float(mir_eval.beat.continuity(actual, predicted)[1])
    return result


def score(attempt: Path) -> dict:
    ref.external(attempt)
    target = attempt / "scoring"
    if target.exists():
        raise ValueError("scoring attempt already exists; refusing overwrite")
    frozen = json.loads((attempt / "freeze.private.json").read_text())
    bindings = frozen["bindings"]
    ref.recheck(bindings)
    ref.bind(attempt / "inputs.private.json", frozen["inputs_sha256"], bindings)
    manifest = json.loads(Path(frozen["prepared_manifest"]).read_text())
    models = ("beatnet", "beat_this")
    for model in models:
        report = json.loads((attempt / model / "report.json").read_text())
        if (report.get("inputs_sha256") != frozen["inputs_sha256"] or report.get("model") != model
            or len(report["outputs"]) != 100 or [row["track"] for row in report["outputs"]] != [r["track"] for r in manifest["tracks"]]):
            raise ValueError("incomplete or mismatched fixed model outputs")
        ref.recheck(report["source_bindings"])
        for row in report["outputs"]:
            ref.bind(attempt / model / (row["track"] + ".json"), row["events_sha256"], bindings)
    target.mkdir()
    totals = {name: Counter() for name in ("coarse", *models)}
    fixes = {name: Counter() for name in models}
    macro = {name: Counter() for name in models}
    by_genre, details = {}, []
    for row in manifest["tracks"]:
        audio = Path(frozen["prepared_manifest"]).parent / "audio" / row["file"]
        native_path = target / (row["track"] + "-coarse.json")
        completed = subprocess.run([frozen["analyzer"], str(audio), str(native_path), "--mode=coarse",
            f"--frames={row['requested_frames']}"], capture_output=True, text=True, timeout=120)
        if completed.returncode:
            raise ValueError("unchanged native coarse execution failed")
        native = json.loads(native_path.read_text())
        ref.history.validate_output(native, row, "coarse")
        baseline = [ref.annotated.score_window(w, row, ref.RATE) for w in native["windows"]]
        for value in baseline:
            totals["coarse"].update(value)
        records = {}
        for name in models:
            events = json.loads((attempt / name / (row["track"] + ".json")).read_text())["events"]
            ref.valid_events(events["beat_frames"], row["requested_frames"])
            ref.valid_events(events["downbeat_frames"], row["requested_frames"])
            metrics = event_metrics(row, events)
            macro[name].update(metrics)
            genre = by_genre.setdefault(row["genre"], {model: {"tracks": 0, "metrics": Counter()} for model in models})[name]
            genre["tracks"] += 1
            genre["metrics"].update(metrics)
            windows = []
            for old, before in zip(native["windows"], baseline):
                candidate = ref.project(events["beat_frames"], events["downbeat_frames"], old["first_frame"], old["end_frame"])
                after = ref.annotated.score_window(candidate, row, ref.RATE)
                totals[name].update(after)
                for field in ("confirmed_period_windows", "confirmed_phase_windows", "confirmed_joint_windows"):
                    fixes[name][field + "_fixes"] += int(not before[field] and bool(after[field]))
                    fixes[name][field + "_breaks"] += int(bool(before[field]) and not after[field])
                # Independent JSON round trip is rescored; candidate contains no reference fields.
                if ref.annotated.score_window(json.loads(json.dumps(candidate)), row, ref.RATE) != after:
                    raise ValueError("serialized candidate score changed")
                windows.append({"candidate": candidate, "score": after})
            records[name] = {"event_metrics": metrics, "windows": windows}
        detail = {"track": row["track"], "coarse": baseline, "models": records}
        ref.history.write_new(target / (row["track"] + ".json"), detail)
        details.append({"track": row["track"], "sha256": ref.history.sha256_file(target / (row["track"] + ".json"))})
    ref.recheck(bindings)
    # Reload saved events and score records, then independently reproduce all
    # event metrics, projected tuples and unchanged-scorer window verdicts.
    for row, detail in zip(manifest["tracks"], details):
        record_path = target / (row["track"] + ".json")
        ref.bind(record_path, detail["sha256"], {})
        record = json.loads(record_path.read_text())
        for name in models:
            events = json.loads((attempt / name / (row["track"] + ".json")).read_text())["events"]
            saved = record["models"][name]
            if event_metrics(row, events) != saved["event_metrics"]:
                raise ValueError("saved raw event metrics do not reproduce")
            for window in saved["windows"]:
                geometry = window["candidate"]
                projected = ref.project(events["beat_frames"], events["downbeat_frames"], geometry["first_frame"], geometry["end_frame"])
                if projected != geometry or ref.annotated.score_window(projected, row, ref.RATE) != window["score"]:
                    raise ValueError("saved projection/window metrics do not reproduce")
    means = {name: {key: value / 100 for key, value in macro[name].items()} for name in models}
    gates = {name: {key: means[name][key] >= ref.protocol()["gates"][key + "_min"] for key in means[name]} for name in models}
    value = {"format": ref.FORMAT, "formal_acceptance": False, "status": "reference_comparison_completed",
        "source_revision": frozen["source_revision"], "protocol_sha256": ref.PROTOCOL_SHA256,
        "tracks": 100, "event_means": means, "event_gates": gates,
        "totals": {name: dict(v) for name, v in totals.items()}, "paired": {name: dict(v) for name, v in fixes.items()},
        "by_genre": {g: {name: {"tracks": data["tracks"], "event_means": {k: v / data["tracks"] for k, v in data["metrics"].items()}}
                              for name, data in values.items()} for g, values in by_genre.items()},
        "cross_dataset_identity_verified": False, "confidence_calibrated": False, "p4_measured": False,
        "promotion_allowed": False, "details": details}
    ref.history.write_new(target / "report.json", value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("freeze")
    for field in ("prepared", "assets", "analyzer", "output"):
        p.add_argument("--" + field, type=Path, required=True)
    p = commands.add_parser("score")
    p.add_argument("--attempt", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "freeze":
        result = freeze(args.prepared, args.assets, args.analyzer, args.output)
    else:
        result = score(args.attempt)
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
