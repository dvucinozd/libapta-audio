#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""One fixed external KeyExtractor screen; no native algorithm or corpus tuning."""
import argparse
import hashlib
import json
import time
from pathlib import Path

import numpy as np
import essentia
import essentia.standard as es


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("refusing to overwrite an existing screen")
    extractor = es.KeyExtractor(sampleRate=48000)
    parameters = {name: extractor.paramValue(name) for name in extractor.parameterNames()}
    notes = {name: i for i, name in enumerate(("C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"))}
    notes.update({"Db": 1, "D#": 3, "Gb": 6, "G#": 8, "A#": 10})
    rows = []
    for mode in ("major", "minor"):
        for tonic in range(12):
            windows = []
            for window, degree in enumerate((0, 5, 7, 0)):
                root = 48 + tonic + degree
                third = 4 if mode == "major" or window == 2 else 3
                frames = np.arange(window * 48000, (window + 1) * 48000, dtype=np.float64)
                pcm = sum(0.15 * np.sin(2 * np.pi * (440 * 2 ** ((note - 69) / 12)) * frames / 48000)
                          for note in (root, root + third, root + 7))
                windows.append(pcm.astype(np.float32))
            audio = np.concatenate(windows)
            started = time.perf_counter()
            key, scale, strength = extractor(audio)
            elapsed = time.perf_counter() - started
            rows.append(dict(tonic=tonic, mode=mode, key=key, scale=scale, strength=float(strength),
                             match=notes[key] == tonic and scale == mode,
                             seconds=elapsed, pcm_sha256=hashlib.sha256(audio.tobytes()).hexdigest()))
    matches = {mode: sum(r["match"] for r in rows if r["mode"] == mode) for mode in ("major", "minor")}
    report = dict(format="apta-key-reference-smoke-1", acceptance_claim=False,
                  evidence_level="synthetic-reference-screen", essentia_version=essentia.__version__,
                  parameters=parameters, matches_per_12=matches, screen_pass=all(n >= 9 for n in matches.values()),
                  rows=rows)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, sort_keys=True, indent=2, allow_nan=False)
        output.write("\n")
    print(json.dumps({"matches_per_12": matches, "screen_pass": report["screen_pass"],
                      "essentia_version": essentia.__version__}))


if __name__ == "__main__":
    main()
