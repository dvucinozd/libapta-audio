#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Hash-bound provisional history coverage on an explicitly spent development set.

No acceptance labels, FINAL export, formal holdout mode or automatic promotion.
Private input names/manifests stay local; reports contain only content-derived IDs.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import struct
import subprocess
import sys
import wave
from collections import Counter
from pathlib import Path

from apta_1_1_asap_meter_validation import sha256_file

FORMAT = "libapta-history-development-input-1"
REPORT = "libapta-history-development-report-1"
WINDOW = 262144
HEX = re.compile(r"^[0-9a-f]{64}$")


def geometry(path: Path) -> dict:
    with wave.open(str(path), "rb") as source:
        if (source.getframerate(), source.getnchannels(), source.getsampwidth(), source.getcomptype()) != (48000, 2, 2, "NONE"):
            raise ValueError("development input must be canonical 48 kHz stereo PCM16")
        if source.getnframes() == 0:
            raise ValueError("development input is empty")
        return {"sample_rate": 48000, "channel_count": 2, "source_total_frames": source.getnframes()}


def write_new(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write("\n")


def freeze(audio_root: Path, manifest_path: Path, expected_count: int, inventory: str, seconds: int) -> dict:
    if not HEX.fullmatch(inventory) or expected_count <= 0 or not 1 <= seconds <= 30:
        raise ValueError("positive count, valid source inventory hash and 1-30 seconds required")
    paths = sorted(audio_root.glob("*.wav"))
    if len(paths) != expected_count:
        raise ValueError("audio count differs from the predeclared development count")
    rows = []
    for path in paths:
        if not path.is_file() or path.is_symlink():
            raise ValueError("development audio must be regular local files")
        info = geometry(path)
        digest = sha256_file(path)
        rows.append({"track": "track-" + digest[:24], "file": path.name, "sha256": digest,
                     **info, "requested_frames": seconds * 48000})
    rows.sort(key=lambda row: row["track"])
    if len({row["track"] for row in rows}) != len(rows):
        raise ValueError("duplicate audio identity in development set")
    value = {"format": FORMAT, "source_role": "spent-development", "candidate_inventory_sha256": inventory,
             "labels": "not supplied; accuracy and false-lattice rates unmeasured", "tracks": rows}
    write_new(manifest_path, value)
    return value


def load_manifest(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("format") != FORMAT or value.get("source_role") != "spent-development" or not HEX.fullmatch(value.get("candidate_inventory_sha256", "")):
        raise ValueError("only explicitly spent development manifests are supported")
    rows = value.get("tracks")
    if not isinstance(rows, list) or not rows:
        raise ValueError("nonempty development rows required")
    seen = set()
    for row in rows:
        name = row.get("file", "")
        digest = row.get("sha256", "")
        if not name or Path(name).name != name or "/" in name or "\\" in name or not name.endswith(".wav"):
            raise ValueError("input file must be a local WAV leaf name")
        if not HEX.fullmatch(digest) or row.get("track") != "track-" + digest[:24] or row["track"] in seen:
            raise ValueError("invalid or duplicated opaque audio identity")
        seen.add(row["track"])
        if row.get("sample_rate") != 48000 or row.get("channel_count") != 2 or not isinstance(row.get("source_total_frames"), int) or row["source_total_frames"] <= 0:
            raise ValueError("invalid canonical source geometry")
        if not isinstance(row.get("requested_frames"), int) or not 0 < row["requested_frames"] <= 30 * 48000:
            raise ValueError("analysis prefix exceeds the declared 30-second bound")
    return value


def validate_output(value: dict, row: dict, mode: str) -> None:
    if mode not in {"coarse", "attack", "novelty"}:
        raise ValueError("unknown candidate mode")
    if value.get("format") != "libapta-history-diagnostic-1" or value.get("mode") != mode:
        raise ValueError("unexpected native diagnostic format/mode")
    for field in ("sample_rate", "channel_count", "source_total_frames", "requested_frames"):
        if value.get(field) != row[field]:
            raise ValueError("native diagnostic source geometry mismatch")
    total = min(row["source_total_frames"], row["requested_frames"])
    if value.get("input_sha256") != row["sha256"] or value.get("analyzed_first_frame") != 0 or value.get("analyzed_frames") != total:
        raise ValueError("native diagnostic source identity/scope mismatch")
    if value.get("finished") is not True or value.get("capacity_exhausted") is not False or value.get("provisional") is not True:
        raise ValueError("incomplete, exhausted or promoted native diagnostic")
    windows = value.get("windows", [])
    if len(windows) != (total + WINDOW - 1) // WINDOW:
        raise ValueError("native diagnostic window count mismatch")
    for index, window in enumerate(windows):
        if (window.get("first_frame"), window.get("end_frame")) != (index * WINDOW, min((index + 1) * WINDOW, total)):
            raise ValueError("native diagnostic window partition mismatch")
        for field in ("baseline", "selected"):
            selected = window.get(field, {})
            if selected.get("kind") not in {"accepted", "rejected", "unavailable", "too_short"}:
                raise ValueError("unknown history verdict")
            if selected["kind"] == "unavailable":
                raise ValueError("lost evidence invalidates this prompt-capture run")
            if selected["kind"] == "accepted" and not (
                window["first_frame"] <= selected.get("anchor_frame", -1) < window["end_frame"]
                and 40000 <= selected.get("tempo_millibpm", 0) <= 300000
                and 0 <= selected.get("confidence", -1) <= (49 if field == "selected" and mode != "coarse" else 100)
            ):
                raise ValueError("invalid accepted history estimate")
        meter = window.get("meter", {})
        if meter.get("kind") not in {"estimated", "grid_unsupported", "insufficient_beats", "beat_limit", "ambiguous", "unavailable"}:
            raise ValueError("unknown meter verdict")
        if meter["kind"] == "unavailable":
            raise ValueError("lost meter evidence invalidates this run")
        if meter["kind"] == "estimated" and not (
            window["selected"]["kind"] == "accepted" and meter.get("numerator") in (3, 4)
            and meter.get("denominator") == 4 and 0 <= meter.get("confidence", -1) < 50
            and window["first_frame"] <= meter.get("downbeat_frame", -1) < window["end_frame"]
        ):
            raise ValueError("invalid provisional meter")


def validate_onset_trace(value: dict) -> None:
    for window in value["windows"]:
        trace = window.get("onset_trace", {})
        events = trace.get("events")
        if trace.get("format") != "libapta-onset-events-1" or trace.get("complete") is not True or not isinstance(events, list) or len(events) > 128:
            raise ValueError("missing, incomplete or overflowing onset trace")
        previous = -1
        for event in events:
            if not isinstance(event, list) or len(event) != 2:
                raise ValueError("invalid onset event")
            frame, strength = event
            if (not isinstance(frame, int) or frame <= previous or frame % 256
                or not window["first_frame"] <= frame < window["end_frame"]
                or not isinstance(strength, (int, float)) or not math.isfinite(strength) or not 0.20 < strength <= 1.0):
                raise ValueError("invalid onset source coordinate/strength")
            previous = frame


def validate_dense_trace(value: dict) -> None:
    rate = value.get("sample_rate")
    if type(rate) is not int or not 0 < rate <= 768000:
        raise ValueError("invalid dense sample rate")
    background = (rate * 80 + 1000 * 256 - 1) // (1000 * 256)
    for window in value["windows"]:
        first, end = window["first_frame"], window["end_frame"]
        trace = window.get("dense_trace", {})
        if type(first) is not int or type(end) is not int or first < 0 or first % WINDOW or not first < end <= first + WINDOW:
            raise ValueError("invalid dense source window")
        count = (end + 255) // 256 - first // 256
        startup = max(0, min((end + 255) // 256, background) - first // 256)
        expected = {"count": count, "first_bin": first // 256, "bin_frames": 256,
                    "last_bin_frames": (end - 1) % 256 + 1, "startup_bins": startup}
        if (trace.get("format") != "libapta-dense-novelty-1" or trace.get("complete") is not True
            or any(type(trace.get(key)) is not int or trace[key] != val for key, val in expected.items())):
            raise ValueError("incomplete or mismatched dense source coordinates")
        values = trace.get("values")
        if not isinstance(values, list) or len(values) != count or not 0 < count <= 1024:
            raise ValueError("invalid dense count")
        if any(type(v) not in (int, float) or not math.isfinite(v) or not 0.0 <= v <= 1.0 for v in values) or any(values[:startup]):
            raise ValueError("invalid dense novelty/startup values")
        if any(struct.unpack("<f", struct.pack("<f", v))[0] != v for v in values):
            raise ValueError("dense novelty values must preserve exact native f32")


def run(manifest_path: Path, audio_root: Path, analyzer: Path, output: Path, revision: str, candidate: str = "attack", trace: bool = False, dense: bool = False) -> dict:
    if candidate not in {"attack", "novelty"}:
        raise ValueError("explicit supported development candidate required")
    if trace and candidate != "novelty":
        raise ValueError("onset traces require explicit novelty candidate")
    if dense and candidate != "novelty":
        raise ValueError("dense traces require explicit novelty candidate")
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("full source revision required, with dirty inventory recorded separately")
    manifest = load_manifest(manifest_path)
    manifest_hash = sha256_file(manifest_path)
    binary_hash = sha256_file(analyzer)
    output.mkdir(parents=True, exist_ok=False)
    totals = {mode: Counter() for mode in dict.fromkeys(("coarse", "attack", candidate))}
    if trace:
        totals["novelty-trace"] = Counter()
    if dense:
        totals["novelty-dense"] = Counter()
    reasons = {mode: Counter() for mode in totals}
    details = []
    for index, row in enumerate(manifest["tracks"], start=1):
        audio = audio_root / row["file"]
        if audio.is_symlink() or geometry(audio) != {key: row[key] for key in ("sample_rate", "channel_count", "source_total_frames")} or sha256_file(audio) != row["sha256"]:
            raise ValueError("frozen development source changed")
        print(f"analyze {index}/{len(manifest['tracks'])} {row['track']}", flush=True)
        pair = {}
        files = {}
        for mode in totals:
            target = output / f"{row['track']}-{mode}.json"
            completed = subprocess.run([str(analyzer), str(audio), str(target), f"--mode={mode}", f"--frames={row['requested_frames']}"], capture_output=True, text=True)
            if completed.returncode != 0:
                raise ValueError(f"native diagnostic failed for {row['track']} ({mode})")
            value = json.loads(target.read_text(encoding="utf-8"))
            validate_output(value, row, "novelty" if mode in {"novelty-trace", "novelty-dense"} else mode)
            if mode == "novelty-trace":
                validate_onset_trace(value)
            if mode == "novelty-dense":
                validate_dense_trace(value)
            pair[mode] = value
            files[mode] = sha256_file(target)
            count = totals[mode]
            count["tracks"] += 1
            count["analyzed_frames"] += value["analyzed_frames"]
            count["windows"] += len(value["windows"])
            supported = [w for w in value["windows"] if w["selected"]["kind"] == "accepted"]
            count["tracks_with_grid"] += bool(supported)
            count["supported_windows"] += len(supported)
            count["supported_frames"] += sum(w["end_frame"] - w["first_frame"] for w in supported)
            count["meter_windows"] += sum(w["meter"]["kind"] == "estimated" for w in value["windows"])
            for window in value["windows"]:
                reasons[mode][window["selection_reason"] if mode != "coarse" else window["selected"]["kind"]] += 1
        for mode in totals:
            if [w["baseline"] for w in pair[mode]["windows"]] != [w["selected"] for w in pair["coarse"]["windows"]]:
                raise ValueError("candidate altered retained baseline verdicts")
        if trace:
            stripped = dict(pair["novelty-trace"], windows=[{key: val for key, val in window.items() if key != "onset_trace"} for window in pair["novelty-trace"]["windows"]])
            if stripped != pair["novelty"]:
                raise ValueError("onset capture changed existing diagnostic evidence")
        if dense:
            stripped = dict(pair["novelty-dense"], windows=[{key: val for key, val in window.items() if key != "dense_trace"} for window in pair["novelty-dense"]["windows"]])
            if stripped != pair["novelty"]:
                raise ValueError("dense capture changed existing diagnostic evidence")
        details.append({"track": row["track"], "outputs_sha256": files})
    if sha256_file(analyzer) != binary_hash or sha256_file(manifest_path) != manifest_hash:
        raise ValueError("analyzer or frozen manifest changed during run")
    rejected = totals[candidate]["supported_windows"] == 0 and totals["coarse"]["supported_windows"] > 0
    report = {"format": REPORT, "source_revision": revision, "candidate_inventory_sha256": manifest["candidate_inventory_sha256"],
              "manifest_sha256": manifest_hash, "analyzer_sha256": binary_hash, "candidate": candidate, "traces_requested": trace, "evidence_level": "spent-development coverage only",
              "status": "rejected_zero_coverage" if rejected else "development_only",
              "formal_acceptance": False, "accuracy": None, "false_lattice_rate": None,
              "unmeasured_reason": "verified frame labels not supplied; abstentions are not accuracy successes",
              "totals": {mode: dict(count) for mode, count in totals.items()},
              "reasons": {mode: dict(count) for mode, count in reasons.items()}, "outputs": details}
    if dense:
        report["dense_traces_requested"] = True
    write_new(output / "report.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    freeze_parser = commands.add_parser("freeze-spent-development")
    freeze_parser.add_argument("--audio-root", type=Path, required=True)
    freeze_parser.add_argument("--manifest", type=Path, required=True)
    freeze_parser.add_argument("--expected-count", type=int, required=True)
    freeze_parser.add_argument("--candidate-inventory-sha256", required=True)
    freeze_parser.add_argument("--seconds", type=int, default=30)
    run_parser = commands.add_parser("run")
    for name in ("manifest", "audio-root", "analyzer", "output"):
        run_parser.add_argument(f"--{name}", type=Path, required=True)
    run_parser.add_argument("--source-revision", required=True)
    run_parser.add_argument("--candidate", choices=("attack", "novelty"), default="attack")
    run_parser.add_argument("--trace", action="store_true", help="capture native novelty events in an additional paired mode")
    run_parser.add_argument("--dense", action="store_true", help="capture complete native novelty bins in an additional paired mode")
    args = parser.parse_args()
    try:
        if args.command == "freeze-spent-development":
            value = freeze(args.audio_root, args.manifest, args.expected_count, args.candidate_inventory_sha256, args.seconds)
            print(json.dumps({"track_count": len(value["tracks"]), "manifest_sha256": sha256_file(args.manifest)}))
        else:
            value = run(args.manifest, args.audio_root, args.analyzer, args.output, args.source_revision, args.candidate, args.trace, args.dense)
            print(json.dumps({"status": value["status"], "totals": value["totals"]}))
    except (OSError, ValueError, wave.Error) as exc:
        print(f"development probe failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
