#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""One frozen external-reference comparison on spent FMAK development audio."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import wave

import numpy as np
import essentia
import essentia.standard as es
import apta_1_1_fmak_semitone_band_key_development as corpus

MANIFEST = "25e853990cc25dc97929f7767de825e89b9e650c7c1b8d075b6fd98c6d0ef09e"
BASELINE = "5053cff8bf3dd8116b936c6b4bc3c3acc2bab4d7e243adf03d7ceec407bffd80"
SMOKE = "99e733250f604a0eed57317fb743ad7f02e576c940f0c0255bffe14719381a61"


def sha(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prepared", "baseline", "smoke", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output exists")
    for path, expected in ((args.prepared / "manifest.json", MANIFEST),
                           (args.baseline, BASELINE), (args.smoke, SMOKE)):
        if sha(path) != expected:
            parser.error("frozen input hash mismatch")
    manifest, labels = corpus.load_prepared(args.prepared)
    mapping_path = args.prepared / "private-sources.json"
    if sha(mapping_path) != manifest["private_sources_sha256"]:
        parser.error("private mapping hash mismatch")
    hashes = {r["track"]: r["canonical_sha256"] for r in json.loads(mapping_path.read_text())}
    baseline = {r["track"]: r for r in json.loads(args.baseline.read_text())["tracks"]}
    if set(hashes) != set(manifest["track_ids"]) or set(baseline) != set(hashes):
        parser.error("incomplete identity coverage")
    for row in labels:
        track = row["track"]
        if (row["split"] != "development" or sha(args.prepared / "audio" / (track + ".wav")) != hashes[track]
                or (baseline[track]["expected_tonic"], baseline[track]["expected_mode"]) !=
                   (row["key_tonic"], row["key_mode"])):
            parser.error("audio/label identity mismatch")
    extractor = es.KeyExtractor(sampleRate=48000)
    params = {name: extractor.paramValue(name) for name in extractor.parameterNames()}
    smoke = json.loads(args.smoke.read_text())
    if params != smoke["parameters"] or essentia.__version__ != smoke["essentia_version"]:
        parser.error("reference configuration changed")
    names = {name: i for i, name in enumerate(("C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"))}
    names.update({"Db": 1, "D#": 3, "Gb": 6, "G#": 8, "A#": 10})
    rows = []
    started = time.perf_counter()
    for label in labels:
        track = label["track"]
        with wave.open(str(args.prepared / "audio" / (track + ".wav")), "rb") as wav:
            if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getcomptype()) != (48000, 2, 2, "NONE"):
                parser.error("unexpected WAV geometry")
            frames = wav.getnframes()
            samples = np.frombuffer(wav.readframes(frames), dtype="<i2")
        if len(samples) != frames * 2:
            parser.error("truncated WAV")
        mono = samples.astype(np.float32).reshape(-1, 2).mean(axis=1) / 32768.0
        key, mode, strength = extractor(mono)
        tonic = names[key]
        rows.append(dict(track=track, expected_tonic=label["key_tonic"], expected_mode=label["key_mode"],
                         key_tonic=tonic, key_mode=mode, strength=float(strength),
                         correct=tonic == label["key_tonic"] and mode == label["key_mode"],
                         baseline_correct=baseline[track]["key_correct"],
                         changed=(tonic, mode) != (baseline[track]["key_tonic"], baseline[track]["key_mode"])))
        if len(rows) % 12 == 0:
            print(f"completed {len(rows)}/72", flush=True)
    by_mode = {mode: sum(r["correct"] for r in rows if r["expected_mode"] == mode) for mode in ("major", "minor")}
    total = sum(r["correct"] for r in rows)
    fixes = sum(r["correct"] and not r["baseline_correct"] for r in rows)
    breaks = sum(not r["correct"] and r["baseline_correct"] for r in rows)
    summary = dict(track_count=72, reference_correct=total, baseline_correct=sum(r["baseline_correct"] for r in rows),
                   matches_per_36=by_mode, fixes=fixes, breaks=breaks, changed=sum(r["changed"] for r in rows),
                   port_investigation_justified=total / 72 >= .70 and all(n / 36 >= .60 for n in by_mode.values()) and fixes > breaks)
    report = dict(format="apta-external-key-development-1", acceptance_claim=False, holdout_eligible=False,
                  evidence_level="spent-development-external-reference", confidence_safety="not-assessed",
                  essentia_version=essentia.__version__, parameters=params, manifest_sha256=MANIFEST,
                  baseline_report_sha256=BASELINE, seconds=time.perf_counter()-started,
                  summary=summary, rows=rows)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2, sort_keys=True, allow_nan=False)
        output.write("\n")
    print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
