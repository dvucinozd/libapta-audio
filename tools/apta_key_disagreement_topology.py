#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect pinned spent key reports without reading audio or changing labels."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess

BASE_REVISION = "d60e0cd61dacf48d5f251a1b0c403dff443fbeb9"
INPUTS = {
    "baseline": ("build/takeover-key/default-report.json", "5053cff8bf3dd8116b936c6b4bc3c3acc2bab4d7e243adf03d7ceec407bffd80"),
    "essentia": ("build/key-reference-screen/development.json", "fa516407b2abb1dda39803053c497b143ea81152d94e062ceae6142146667eb1"),
    "openkeyscan": ("build/key-label-review/openkeyscan-development/report-private.json", "d1b45badaa03639303a3fa1a3bc6c42f60304fca71c4dd404d7a3edd7eaae66c"),
    "coordinator": ("build/key-label-review/coordinator-private.json", "90d2b860fea3a694c15334356d1293731a8a0a9bc1506b228bdd7e4e8a349ed3"),
    "review": ("build/key-label-review/automated-triage-screen/report-private.json", "34ce29a46de709d54dad6c6f73e52c64bba183c7cf36215911676d51cf56a3fe"),
}
FAMILIES = ("exact", "parallel_mode", "relative_major_minor", "same_mode_fourth_fifth",
            "other_same_mode_tonic", "other_cross_mode")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def valid_key(value):
    require(isinstance(value, (list, tuple)) and len(value) == 2, "invalid key pair")
    tonic, mode = value
    require(type(tonic) is int and 0 <= tonic < 12 and mode in ("major", "minor"), "invalid key value")
    return tonic, mode


def key(row, prefix="key"):
    return valid_key((row[prefix + "_tonic"], row[prefix + "_mode"]))


def relation(first, second):
    a, b = valid_key(first), valid_key(second)
    if a == b:
        return "exact"
    if a[0] == b[0]:
        return "parallel_mode"
    if a[1] != b[1]:
        major, minor = (a, b) if a[1] == "major" else (b, a)
        return "relative_major_minor" if (minor[0] - major[0]) % 12 == 9 else "other_cross_mode"
    return "same_mode_fourth_fifth" if (b[0] - a[0]) % 12 in (5, 7) else "other_same_mode_tonic"


def indexed(rows, field, count):
    require(isinstance(rows, list) and len(rows) == count, "unexpected row count")
    result = {row[field]: row for row in rows}
    require(len(result) == count, "duplicate identity")
    return result


def load_pinned(root):
    raw = {name: (root / path).read_bytes() for name, (path, _) in INPUTS.items()}
    for name, data in raw.items():
        require(hashlib.sha256(data).hexdigest() == INPUTS[name][1], "frozen input hash mismatch: " + name)
    return {name: json.loads(data) for name, data in raw.items()}


def join_reports(reports):
    baseline = indexed(reports["baseline"]["tracks"], "track", 72)
    essentia = indexed(reports["essentia"]["rows"], "track", 72)
    oks = indexed(reports["openkeyscan"]["rows"], "track", 72)
    require(set(baseline) == set(essentia) == set(oks), "track coverage mismatch")
    coordinator = indexed(reports["coordinator"]["private_rows"], "sample", 12)
    review = indexed(reports["review"]["rows"], "sample", 12)
    require(set(coordinator) == set(review) == {f"A{i:02d}" for i in range(1, 13)}, "review sample coverage mismatch")
    require(len({row["track"] for row in coordinator.values()}) == 12, "duplicate review track")
    require({row["track"] for row in coordinator.values()} <= set(baseline), "review track coverage mismatch")
    reviewed = {}
    for sample, row in coordinator.items():
        track, listener = row["track"], review[sample]
        require(valid_key(row["baseline"]) == key(baseline[track]) and
                valid_key(row["reference"]) == key(essentia[track]) and
                key(row) == key(baseline[track], "expected"), "review key mapping mismatch")
        require(valid_key(listener["openkeyscan"]) == key(oks[track]), "review detector mismatch")
        first = valid_key(listener["listener1"])
        second = listener["listener2"]
        require(second is None or valid_key(second) == first, "listener consensus mismatch")
        reviewed[track] = dict(sample=sample, listener1=first, consensus=first if second is not None else None)
    require(sum(row["consensus"] is not None for row in reviewed.values()) == 9, "consensus coverage mismatch")
    rows = []
    for track in sorted(baseline):
        require(re.fullmatch(r"track-[0-9a-f]{24}", track) is not None, "nonopaque track identity")
        b, e, o = baseline[track], essentia[track], oks[track]
        native, external, scan = key(b), key(e), key(o)
        expected = key(b, "expected")
        require(expected == key(e, "expected"), "retained label mismatch")
        flags = {"agrees_apta": scan == native, "agrees_essentia": scan == external,
                 "agrees_fmak": scan == expected, "apta_agrees_essentia": native == external,
                 "all_three_algorithms_agree": scan == native == external}
        require(all(o[name] is value for name, value in flags.items()), "retained agreement flag mismatch")
        require(b["key_correct"] is (native == expected) and e["correct"] is (external == expected), "retained label flag mismatch")
        require(o["canonical_source_unchanged"] is True and o["disposable_pcm_identical"] is True,
                "retained source preservation failed")
        rows.append(dict(track=track, native=native, essentia=external, openkeyscan=scan,
                         external_relation=relation(scan, external), review=reviewed.get(track)))
    require(sum(row["external_relation"] != "exact" for row in rows) == 22, "expected 22 disagreements")
    return rows


def pair_summary(rows, first, second):
    families = Counter(relation(row[first], row[second]) for row in rows)
    return dict(families={name: families[name] for name in FAMILIES},
                mode_pairs=dict(sorted(Counter(row[first][1] + "->" + row[second][1] for row in rows).items())),
                tonic_delta_mod12={str(i): sum((row[second][0] - row[first][0]) % 12 == i for row in rows) for i in range(12)})


def aggregate(rows):
    result = dict(count=len(rows),
                  modes={name: {mode: sum(row[name][1] == mode for row in rows) for mode in ("major", "minor")}
                         for name in ("native", "openkeyscan", "essentia")},
                  pairs={a + "_to_" + b: pair_summary(rows, a, b) for a, b in
                         (("openkeyscan", "essentia"), ("native", "openkeyscan"), ("native", "essentia"))})
    result["review_overlap"] = {}
    for field in ("listener1", "consensus"):
        subset = [row for row in rows if row["review"] and row["review"][field] is not None]
        result["review_overlap"][field] = dict(count=len(subset),
            agreements={name: sum(row[name] == row["review"][field] for row in subset)
                        for name in ("native", "openkeyscan", "essentia")})
    return result


def analyze(reports):
    rows = join_reports(reports)
    summary = {"all_72": aggregate(rows),
               "external_agreement_50": aggregate([r for r in rows if r["external_relation"] == "exact"]),
               "external_disagreement_22": aggregate([r for r in rows if r["external_relation"] != "exact"])}
    # Native behavior within each descriptive external family; no truth assignment.
    summary["disagreement_families"] = {
        name: aggregate([r for r in rows if r["external_relation"] == name])
        for name in FAMILIES if name != "exact"}
    return rows, summary


def run(root, output):
    require(not output.exists(), "output exists; no overwrite permitted")
    require(output.resolve().is_relative_to((root / "build").resolve()), "private output must stay under build")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    require(revision == BASE_REVISION, "unexpected baseline revision")
    status = subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True)
    inputs = load_pinned(root)
    rows, summary = analyze(inputs)
    provenance = dict(source_revision=revision, source_worktree_clean=not bool(status),
        input_hashes={name: digest for name, (_, digest) in INPUTS.items()},
        tool_sha256=sha(Path(__file__)),
        tests_sha256=sha(root / "tools/test_apta_key_disagreement_topology.py"),
        protocol_sha256=sha(root / "docs/status/APTA-1.1-KEY-DISAGREEMENT-TOPOLOGY.md"))
    report = dict(format="apta-key-disagreement-topology-1", evidence_level="spent-report-only-diagnostic",
                  acceptance_claim=False, candidate_promoted=False, holdout_eligible=False,
                  new_audio_access=False, native_resource_delta_bytes=0, candidate_flags=[],
                  confidence_safety="not-assessed", fixes_breaks="not-assessed-no-candidate-or-adjudicated-truth",
                  provenance=provenance, summary=summary)
    private = dict(report, private_rows=rows)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(private, indent=2, sort_keys=True, allow_nan=False) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = run(Path(__file__).resolve().parents[1], args.output)
    print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))


if __name__ == "__main__":
    main()
