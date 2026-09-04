#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare browser key services on the fixed, spent twelve-track review."""
import argparse
import hashlib
import json
import re
from pathlib import Path

TONICS = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3,
          "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7, "G#": 8,
          "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11, "H": 11}
LISTENER_TONICS = {"C": 0, "C♯ / D♭": 1, "D": 2, "D♯ / E♭": 3,
                   "E": 4, "F": 5, "F♯ / G♭": 6, "G": 7,
                   "G♯ / A♭": 8, "A": 9, "A♯ / B♭": 10,
                   "H (B u engleskoj oznaci)": 11}
LISTENER_MODES = {"Dur (major)": "major", "Mol (minor)": "minor"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def parse_key(value):
    tonic, mode = value.split()
    require(tonic in TONICS and mode in {"major", "minor"}, "unsupported service key")
    return TONICS[tonic], mode


def parse_listener(path, prefix, count):
    pattern = re.compile(r"(?m)^([AB]\d{2})\r?\nTonika — osnovni ton: (.+)\r?\n"
                         r"Tonalitet: (.+)\r?\nKoliko si siguran\?: (.+)\r?\nBilješke: (.*)")
    result = {}
    for match in pattern.finditer(path.read_text(encoding="utf-8-sig")):
        sample, tonic, mode, certainty, notes = (item.strip() for item in match.groups())
        require(sample.startswith(prefix) and sample not in result, "invalid listener sample")
        require(tonic in LISTENER_TONICS and mode in LISTENER_MODES, "non-key listener answer")
        result[sample] = dict(key=(LISTENER_TONICS[tonic], LISTENER_MODES[mode]),
                              certainty=certainty, notes=notes)
    require(len(result) == count, "listener answer count mismatch")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("rotation", "zalturi", "coordinator", "listener1", "listener2-map", "listener2", "output"):
        parser.add_argument("--" + name, dest=name.replace("-", "_"), type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--observed-utc", required=True)
    args = parser.parse_args()
    require(not args.output.exists(), "output exists; never overwrite service evidence")
    require(len(args.source_revision) == 40, "full source revision required")
    rotation_rows = json.loads(args.rotation.read_text(encoding="utf-8"))
    zalturi_data = json.loads(args.zalturi.read_text(encoding="utf-8"))
    zalturi_rows = zalturi_data["rows"]
    require(len(rotation_rows) == len(zalturi_rows) == 12, "service coverage mismatch")
    rotation = {r["file"].removesuffix(".wav"): dict(key=parse_key(r["key"]), bpm=float(r["bpm"]), confidence=r["key confidence"]) for r in rotation_rows}
    zalturi = {r["sample"]: dict(key=parse_key(r["key"]), bpm=r["bpm"], confidence=r["key_confidence"], tuning_cents=r["tuning_cents"]) for r in zalturi_rows}
    expected_samples = {f"A{i:02d}" for i in range(1, 13)}
    require(set(rotation) == set(zalturi) == expected_samples, "service sample IDs mismatch")
    first = parse_listener(args.listener1, "A", 12)
    second = parse_listener(args.listener2, "B", 9)
    coordinator = json.loads(args.coordinator.read_text(encoding="utf-8"))
    original = {r["sample"]: r for r in coordinator["private_rows"]}
    mapping = json.loads(args.listener2_map.read_text(encoding="utf-8"))
    require(mapping["first_answers_sha256"] == sha(args.listener1), "first answers changed")
    require(mapping["first_coordinator_sha256"] == sha(args.coordinator), "coordinator changed")
    second_to_first = {r["sample"]: r["first_sample"] for r in mapping["rows"]}
    require(set(second_to_first) == set(second), "second-listener mapping mismatch")
    consensus = {}
    for second_id, first_id in second_to_first.items():
        require(second[second_id]["key"] == first[first_id]["key"], "listeners disagree")
        consensus[first_id] = first[first_id]["key"]
    rows = []
    for sample in sorted(expected_samples):
        ground = first[sample]["key"]
        source = original[sample]
        rows.append(dict(sample=sample, listener1=list(ground),
                         listener2=list(consensus[sample]) if sample in consensus else None,
                         original=[source["key_tonic"], source["key_mode"]],
                         apta=list(source["baseline"]), essentia=list(source["reference"]),
                         rotation=dict(rotation[sample], key=list(rotation[sample]["key"])),
                         zalturi=dict(zalturi[sample], key=list(zalturi[sample]["key"]))))
    def agreements(name, samples):
        return sum(tuple(next(r for r in rows if r["sample"] == sample)[name]["key"] if name in {"rotation", "zalturi"} else next(r for r in rows if r["sample"] == sample)[name]) == first[sample]["key"] for sample in samples)
    all_samples = sorted(expected_samples)
    consensus_samples = sorted(consensus)
    summary = {
        "listener1_12": {name: agreements(name, all_samples) for name in ("rotation", "zalturi")},
        "two_listener_consensus_9": {name: agreements(name, consensus_samples) for name in ("original", "apta", "essentia", "rotation", "zalturi")},
        "screen_decision": "do-not-scale-either-service-as-label-authority",
        "acceptance_claim": False,
    }
    report = dict(format="apta-key-web-service-screen-1", source_revision=args.source_revision,
                  source_worktree_clean=False, observed_utc=args.observed_utc,
                  evidence_level="spent-diagnostic-browser-service-screen", summary=summary,
                  services=zalturi_data["services"], input_hashes={name: sha(getattr(args, name)) for name in ("rotation", "zalturi", "coordinator", "listener1", "listener2_map", "listener2")}, rows=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
