#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare frozen automated detectors with the spent blind-review packet."""
import argparse
import hashlib
import json
from pathlib import Path

from apta_key_web_service_screen import parse_key, parse_listener, require


COORDINATOR = "90d2b860fea3a694c15334356d1293731a8a0a9bc1506b228bdd7e4e8a349ed3"
LISTENER1 = "4141e8b4f7a7b99ee461306158603cbd08b4ecdef1c12296eb048b03d54b6bb5"
LISTENER2_MAP = "a3bdd9a482b2a5d83022fb0b394ef65e36316759302fc19da274730c01fee1a8"
LISTENER2 = "f3010b5715bd8dc7188bf71843513ef104234bc5dc22490f15316d4372a3d93e"


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("coordinator", "listener1", "listener2-map", "listener2",
                 "openkeyscan", "musical-key-finder", "output"):
        parser.add_argument("--" + name, dest=name.replace("-", "_"), type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), "output exists; no overwrite permitted")
    for path, expected in ((args.coordinator, COORDINATOR), (args.listener1, LISTENER1),
                           (args.listener2_map, LISTENER2_MAP), (args.listener2, LISTENER2)):
        require(sha(path) == expected, f"frozen review input changed: {path}")

    first = parse_listener(args.listener1, "A", 12)
    second = parse_listener(args.listener2, "B", 9)
    coordinator = json.loads(args.coordinator.read_text(encoding="utf-8"))
    original = {row["sample"]: row for row in coordinator["private_rows"]}
    mapping = json.loads(args.listener2_map.read_text(encoding="utf-8"))
    second_to_first = {row["sample"]: row["first_sample"] for row in mapping["rows"]}
    require(set(second_to_first) == set(second), "second-listener coverage mismatch")
    consensus = {}
    for second_id, first_id in second_to_first.items():
        require(second[second_id]["key"] == first[first_id]["key"], "listeners disagree")
        consensus[first_id] = first[first_id]["key"]

    openkeyscan_report = json.loads(args.openkeyscan.read_text(encoding="utf-8"))
    openkeyscan = {row["track"]: (row["key_tonic"], row["key_mode"])
                   for row in openkeyscan_report["rows"]}
    mkf_report = json.loads(args.musical_key_finder.read_text(encoding="utf-8"))
    mkf = {row["id"]: parse_key(row["key"]) for row in mkf_report["rows"]}
    expected_samples = {f"A{i:02d}" for i in range(1, 13)}
    require(set(original) == set(mkf) == expected_samples, "twelve-track coverage mismatch")

    rows = []
    for sample in sorted(expected_samples):
        track = original[sample]["track"]
        require(track in openkeyscan, f"OpenKeyScan result missing: {sample}")
        rows.append({"sample": sample, "listener1": list(first[sample]["key"]),
                     "listener2": list(consensus[sample]) if sample in consensus else None,
                     "openkeyscan": list(openkeyscan[track]), "musical_key_finder": list(mkf[sample])})

    def count(detector, samples):
        return sum(tuple(next(row for row in rows if row["sample"] == sample)[detector]) == first[sample]["key"]
                   for sample in samples)

    all_samples = sorted(expected_samples)
    consensus_samples = sorted(consensus)
    summary = {
        "listener1_12": {name: count(name, all_samples) for name in ("openkeyscan", "musical_key_finder")},
        "two_listener_consensus_9": {name: count(name, consensus_samples)
                                     for name in ("openkeyscan", "musical_key_finder")},
        "decisions": {
            "openkeyscan": "scale-as-automated-triage-only",
            "musical_key_finder": "stop-after-targeted-screen",
            "automatic_relabeling": False,
            "acceptance_claim": False,
        },
    }
    report = {"format": "apta-key-automated-triage-screen-private-1",
              "evidence_level": "spent-targeted-development-diagnostic",
              "summary": summary,
              "input_hashes": {name: sha(getattr(args, name)) for name in
                               ("coordinator", "listener1", "listener2_map", "listener2",
                                "openkeyscan", "musical_key_finder")},
              "rows": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
