#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run a frozen OpenKeyScan comparison on disposable copies of spent audio."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import time
from urllib import request
import wave


MANIFEST = "25e853990cc25dc97929f7767de825e89b9e650c7c1b8d075b6fd98c6d0ef09e"
BASELINE = "5053cff8bf3dd8116b936c6b4bc3c3acc2bab4d7e243adf03d7ceec407bffd80"
ESSENTIA = "fa516407b2abb1dda39803053c497b143ea81152d94e062ceae6142146667eb1"
OPEN_KEY = {
    "1m": (9, "minor"), "1d": (0, "major"),
    "2m": (4, "minor"), "2d": (7, "major"),
    "3m": (11, "minor"), "3d": (2, "major"),
    "4m": (6, "minor"), "4d": (9, "major"),
    "5m": (1, "minor"), "5d": (4, "major"),
    "6m": (8, "minor"), "6d": (11, "major"),
    "7m": (3, "minor"), "7d": (6, "major"),
    "8m": (10, "minor"), "8d": (1, "major"),
    "9m": (5, "minor"), "9d": (8, "major"),
    "10m": (0, "minor"), "10d": (3, "major"),
    "11m": (7, "minor"), "11d": (10, "major"),
    "12m": (2, "minor"), "12d": (5, "major"),
}


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def pcm_identity(path):
    with wave.open(str(path), "rb") as wav:
        geometry = (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(),
                    wav.getnframes(), wav.getcomptype())
        digest = hashlib.sha256()
        size = 0
        while data := wav.readframes(65536):
            size += len(data)
            digest.update(data)
        if size != geometry[1] * geometry[2] * geometry[3]:
            raise ValueError(f"truncated PCM: {path}")
    return geometry, digest.hexdigest()


def api_json(url, payload=None):
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    call = request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with request.urlopen(call, timeout=300) as response:
        return json.load(response)


def key_tuple(row, prefix="key"):
    return row[f"{prefix}_tonic"], row[f"{prefix}_mode"]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prepared", "baseline", "essentia", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--api-base", default="http://localhost:58721")
    parser.add_argument("--analyzer-version", required=True)
    parser.add_argument("--app-sha256", required=True)
    parser.add_argument("--engine-sha256", required=True)
    args = parser.parse_args()
    require(not args.output.exists(), "output exists; no overwrite permitted")
    for path, expected in ((args.prepared / "manifest.json", MANIFEST),
                           (args.baseline, BASELINE), (args.essentia, ESSENTIA)):
        require(sha256(path) == expected, f"frozen input hash mismatch: {path}")

    health = api_json(args.api_base.rstrip("/") + "/health")
    require(health.get("success") is True and health.get("status") == "ok", "OpenKeyScan API unhealthy")
    manifest = json.loads((args.prepared / "manifest.json").read_text(encoding="utf-8"))
    labels = json.loads((args.prepared / "labels.json").read_text(encoding="utf-8"))
    mapping_path = args.prepared / "private-sources.json"
    require(sha256(mapping_path) == manifest["private_sources_sha256"], "private mapping changed")
    mapping = {row["track"]: row for row in json.loads(mapping_path.read_text(encoding="utf-8"))}
    baseline = {row["track"]: row for row in json.loads(args.baseline.read_text(encoding="utf-8"))["tracks"]}
    essentia = {row["track"]: row for row in json.loads(args.essentia.read_text(encoding="utf-8"))["rows"]}
    tracks = [row["track"] for row in labels]
    require(len(tracks) == 72 and len(set(tracks)) == 72, "expected 72 unique tracks")
    require(set(tracks) == set(manifest["track_ids"]) == set(mapping) == set(baseline) == set(essentia),
            "incomplete identity coverage")

    rows = []
    args.output.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="openkeyscan-", dir=args.output.parent) as temporary:
        temporary = Path(temporary)
        for index, label in enumerate(labels, 1):
            track = label["track"]
            source = args.prepared / "audio" / f"{track}.wav"
            source_before = sha256(source)
            require(source_before == mapping[track]["canonical_sha256"], f"source changed: {track}")
            source_pcm = pcm_identity(source)
            disposable = temporary / f"sample-{index:03d}.wav"
            shutil.copy2(source, disposable)
            require(sha256(disposable) == source_before, f"copy mismatch: {track}")
            response = api_json(args.api_base.rstrip("/") + "/analyze/single", {"file": str(disposable.resolve())})
            require(response.get("success") is True and response.get("key") in OPEN_KEY,
                    f"invalid OpenKeyScan response: {track}: {response!r}")
            require(sha256(source) == source_before, f"canonical source modified: {track}")
            require(pcm_identity(disposable) == source_pcm, f"disposable PCM modified: {track}")
            prediction = OPEN_KEY[response["key"]]
            apta = key_tuple(baseline[track])
            external = key_tuple(essentia[track])
            expected = key_tuple(label)
            rows.append({
                "track": track,
                "input_sha256": source_before,
                "open_key": response["key"],
                "key_tonic": prediction[0],
                "key_mode": prediction[1],
                "agrees_fmak": prediction == expected,
                "agrees_apta": prediction == apta,
                "agrees_essentia": prediction == external,
                "apta_agrees_essentia": apta == external,
                "all_three_algorithms_agree": prediction == apta == external,
                "disposable_full_hash_changed": sha256(disposable) != source_before,
                "disposable_pcm_identical": True,
                "canonical_source_unchanged": True,
            })
            disposable.unlink()
            if index % 12 == 0:
                print(f"completed {index}/72", flush=True)

    topology = {"all_three_agree": 0, "openkeyscan_essentia_only": 0,
                "openkeyscan_apta_only": 0, "apta_essentia_only": 0, "all_disagree": 0}
    for row in rows:
        if row["all_three_algorithms_agree"]:
            topology["all_three_agree"] += 1
        elif row["agrees_essentia"]:
            topology["openkeyscan_essentia_only"] += 1
        elif row["agrees_apta"]:
            topology["openkeyscan_apta_only"] += 1
        elif row["apta_agrees_essentia"]:
            topology["apta_essentia_only"] += 1
        else:
            topology["all_disagree"] += 1
    summary = {
        "track_count": len(rows),
        "openkeyscan_agrees_fmak": sum(row["agrees_fmak"] for row in rows),
        "openkeyscan_agrees_apta": sum(row["agrees_apta"] for row in rows),
        "openkeyscan_agrees_essentia": sum(row["agrees_essentia"] for row in rows),
        "canonical_sources_unchanged": sum(row["canonical_source_unchanged"] for row in rows),
        "disposable_pcm_identical": sum(row["disposable_pcm_identical"] for row in rows),
        "disposable_full_hash_changed": sum(row["disposable_full_hash_changed"] for row in rows),
        "algorithm_agreement_topology": topology,
        "openkeyscan_essentia_disagreement_review_candidates":
            sum(not row["agrees_essentia"] for row in rows),
    }
    report = {
        "format": "apta-openkeyscan-development-private-1",
        "acceptance_claim": False,
        "holdout_eligible": False,
        "evidence_level": "spent-development-automated-triage",
        "interpretation": "Agreement counts are not accuracy because FMAK labels are disputed.",
        "api_base": args.api_base,
        "api_health": {"success": health["success"], "status": health["status"]},
        "analyzer_version": args.analyzer_version,
        "app_sha256": args.app_sha256.lower(),
        "engine_sha256": args.engine_sha256.lower(),
        "manifest_sha256": MANIFEST,
        "baseline_report_sha256": BASELINE,
        "essentia_report_sha256": ESSENTIA,
        "seconds": time.perf_counter() - started,
        "summary": summary,
        "rows": rows,
    }
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2, sort_keys=True, allow_nan=False)
        output.write("\n")
    print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
