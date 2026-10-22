#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fixed, host-only neural reference preparation and representation diagnostics.

No training, tuning, formal holdout mode or product promotion. Private audio,
labels, raw events and manifests must remain outside the checkout.
"""
from __future__ import annotations

import argparse
from collections import Counter
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tarfile

import apta_1_1_native_annotated_development as annotated
import apta_1_1_native_history_development as history
import apta_1_1_prepare_corpus as preparation

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_PATH = ROOT / "docs/rust/NEURAL-REFERENCE-PROTOCOL.json"
PARENT_PROTOCOL_SHA256 = "67969fb5d1402a1a8427f947dff286b5b71d49b5cb1c3fa482242727b5d26a2f"
PROTOCOL_SHA256 = "4cd932e5984f8b9bb589b426596de7ba4fcd56b6d5d9db9f4c4670e333e41a50"
AMENDMENT_PATH = ROOT / "docs/rust/NEURAL-REFERENCE-AVAILABILITY-AMENDMENT.json"
FORMAT = "libapta-neural-reference-development-1"
RATE = 48000
FINAL0_SHA256 = "8c328b45f59d8dd3dff219253ff6a8d6482be57d0133a29140e2febbf8eb8331"


def protocol() -> dict:
    if (history.sha256_file(PROTOCOL_PATH) != PARENT_PROTOCOL_SHA256
        or history.sha256_file(AMENDMENT_PATH) != PROTOCOL_SHA256):
        raise ValueError("registered protocol changed")
    return json.loads(PROTOCOL_PATH.read_text())


def external(path: Path) -> None:
    if path.resolve().is_relative_to(ROOT.resolve()):
        raise ValueError("private experiment artifacts must stay outside the checkout")


def hash_bytes(data: bytes, kind: str = "sha256") -> str:
    return hashlib.new(kind, data).hexdigest()


def rounded(value: Fraction | float) -> int:
    return math.floor(value + Fraction(1, 2))


def valid_events(values: list, end: int) -> list[int]:
    if not isinstance(values, list) or any(type(v) is not int or not 0 <= v < end for v in values):
        raise ValueError("events must be finite integer source frames within the input")
    if any(b <= a for a, b in zip(values, values[1:])):
        raise ValueError("events must be strictly ordered, without duplicates")
    return values


def project(beats: list[int], downbeats: list[int], first: int, end: int) -> dict:
    """Label-blind constant-grid projection; source coordinates are never reset."""
    valid_events(beats, max(end, beats[-1] + 1 if beats else end))
    valid_events(downbeats, max(end, downbeats[-1] + 1 if downbeats else end))
    if first < 0 or end <= first or not set(downbeats) <= set(beats):
        raise ValueError("invalid window or downbeats outside the beat sequence")
    inside = [beat for beat in beats if first <= beat < end]
    result = {"first_frame": first, "end_frame": end,
              "selected": {"kind": "insufficient_events"}, "meter": {}}
    if len(inside) < 4:
        return result
    n = len(inside)
    sx, sxx = n * (n - 1) // 2, n * (n - 1) * (2 * n - 1) // 6
    period = Fraction(n * sum(i * x for i, x in enumerate(inside)) - sx * sum(inside), n * sxx - sx * sx)
    if period <= 0:
        raise ValueError("non-positive fitted beat period")
    tempo = rounded(Fraction(RATE * 60000, 1) / period)
    anchor = rounded(Fraction(sum(inside), n) - period * Fraction(sx, n))
    if not 40000 <= tempo <= 300000 or not first <= anchor < end:
        result["selected"] = {"kind": "unsupported_projection"}
        return result
    result["selected"] = {"kind": "accepted", "anchor_frame": anchor,
                          "tempo_millibpm": tempo, "confidence": 0}
    # Never borrow reference or baseline meter. Require two complete equal bars.
    index = {beat: i for i, beat in enumerate(beats)}
    db = [beat for beat in downbeats if first <= beat < end]
    for left, middle, right in zip(db, db[1:], db[2:]):
        meter = index[middle] - index[left]
        if meter not in (3, 4) or index[right] - index[middle] != meter:
            continue
        q32 = (RATE * 60000 << 32) // tempo
        ordinal = rounded(Fraction((left - anchor) << 32, q32))
        position = (anchor << 32) + ordinal * q32
        if ordinal >= 0 and abs(Fraction(position, 1 << 32) - left) <= period / 10:
            result["meter"] = {"kind": "estimated", "numerator": meter, "denominator": 4,
                "downbeat_frame": position >> 32, "downbeat_fraction_q32": position & ((1 << 32) - 1)}
            break
    return result


def select_ids(index: dict, available: set[str] | None = None) -> list[str]:
    groups, seen = {}, set()
    for name, row in sorted(index["tracks"].items()):
        if name == "jazz.00054" or available is not None and name not in available:
            continue
        digest = row["audio"][1]
        if digest in seen:
            continue
        seen.add(digest)
        genre = name.split(".")[0]
        groups.setdefault(genre, []).append(name)
    if len(groups) != 10 or any(len(v) < 10 for v in groups.values()):
        raise ValueError("ten complete genre pools required")
    selected = []
    for names in groups.values():
        selected.extend(sorted(names, key=lambda x: hash_bytes(("libapta-neural-reference-20261022:" + x).encode()))[:10])
    return sorted(selected)


def revision(path: Path) -> str:
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def bind(path: Path, expected: str, bindings: dict) -> None:
    if not path.is_file() or path.is_symlink() or history.sha256_file(path) != expected:
        raise ValueError("frozen artifact hash mismatch")
    bindings[str(path.resolve())] = expected


def recheck(bindings: dict) -> None:
    for name, digest in bindings.items():
        bind(Path(name), digest, {})


def prepare(assets: Path, output: Path, ffmpeg: Path) -> dict:
    """Freeze source selection before opening any annotation contents."""
    config = protocol()
    external(output)
    if output.exists():
        raise ValueError("preparation already exists; refusing overwrite")
    archive, index_path = assets / "genres.tar.gz", assets / "gtzan-index.json"
    bindings = {}
    bind(archive, config["data"]["audio_sha256"], bindings)
    if hash_bytes(index_path.read_bytes(), "md5") != config["data"]["index_md5"]:
        raise ValueError("published GTZAN index MD5 mismatch")
    bind(index_path, history.sha256_file(index_path), bindings)
    if revision(assets / "annotations") != config["data"]["annotation_revision"]:
        raise ValueError("annotation revision differs")
    index = json.loads(index_path.read_text())
    annotation_root = assets / "annotations/gtzan/annotations/beats"
    available = {name for name in index["tracks"] if (annotation_root / ("gtzan_" + name.replace(".", "_") + ".beats")).is_file()}
    names = select_ids(index, available)
    output.mkdir(parents=True)
    history.write_new(output / "selection.private.json", {"format": FORMAT,
        "annotation_values_opened": False, "protocol_sha256": PROTOCOL_SHA256,
        "annotation_availability_checked_before_values": True,
        "source_ids": names, "source_audio_md5": {name: index["tracks"][name]["audio"][1] for name in names}})
    source = output / "selected-source"
    source.mkdir()
    found = set()
    selected = {name + ".wav": name for name in names}
    with tarfile.open(archive, "r|gz") as package:
        for member in package:
            leaf = Path(member.name).name
            if not member.isfile() or leaf not in selected:
                continue
            name = selected[leaf]
            if name in found or member.size > 4000000:
                raise ValueError("selected source duplicated or oversized")
            data = package.extractfile(member).read()
            if hash_bytes(data, "md5") != index["tracks"][name]["audio"][1]:
                raise ValueError("selected WAV differs from published index")
            (source / (name + ".wav")).write_bytes(data)
            found.add(name)
    if found != set(names):
        raise ValueError("selected source absent; no replacement allowed")
    canonical = preparation.prepare(source, output / "audio", output / "conversion.private.json", ffmpeg)
    rows = []
    for row in canonical["tracks"]:
        name = Path(row["source"]).stem
        annotation = assets / "annotations/gtzan/annotations/beats" / ("gtzan_" + name.replace(".", "_") + ".beats")
        if not annotation.is_file():
            raise ValueError("selected annotation absent; no replacement allowed")
        bind(annotation, history.sha256_file(annotation), bindings)
        truth_beats, truth_downbeats = [], []
        for line in annotation.read_text().splitlines():
            values = line.split()
            if len(values) != 2:
                raise ValueError("expected beat time and position columns")
            beat = rounded(Fraction(values[0]) * RATE)
            truth_beats.append(beat)
            if int(values[1]) == 1:
                truth_downbeats.append(beat)
        frames = min(row["frames"], RATE * 30)
        truth_beats = [b for b in truth_beats if b < frames]
        truth_downbeats = [b for b in truth_downbeats if b < frames]
        valid_events(truth_beats, frames)
        valid_events(truth_downbeats, frames)
        if len(truth_beats) < 4 or not truth_downbeats:
            raise ValueError("selected reference lacks beat/downbeat coverage; no replacement")
        db_indices = [truth_beats.index(b) for b in truth_downbeats]
        bars = {b - a for a, b in zip(db_indices, db_indices[1:])}
        meter = next(iter(bars)) if len(bars) == 1 and next(iter(bars)) in (3, 4) else 0
        audio = output / "audio" / row["canonical"]
        bind(audio, row["canonical_sha256"], bindings)
        rows.append({"track": "track-" + row["canonical_sha256"][:24], "file": row["canonical"],
            "sha256": row["canonical_sha256"], "genre": name.split(".")[0], "sample_rate": RATE,
            "channel_count": 2, "source_total_frames": row["frames"], "requested_frames": frames,
            "beat_frames": truth_beats, "downbeat_frames": truth_downbeats,
            "meter_numerator": meter, "meter_denominator": 4,
            "annotation_sha256": history.sha256_file(annotation)})
    rows.sort(key=lambda row: row["track"])
    if len(rows) != config["data"]["expected_tracks"] or len({r["track"] for r in rows}) != len(rows):
        raise ValueError("incomplete or duplicate selected cohort")
    recheck(bindings)
    manifest = {"format": FORMAT, "formal_acceptance": False, "source_role": "open-development",
        "protocol_sha256": PROTOCOL_SHA256, "selection_sha256": history.sha256_file(output / "selection.private.json"),
        "conversion_sha256": history.sha256_file(output / "conversion.private.json"),
        "dataset_excluded_from_training": True, "cross_dataset_identity_verified": False,
        "holdout_performances_opened": False, "tracks": rows, "bindings": bindings}
    history.write_new(output / "manifest.private.json", manifest)
    return {"tracks": len(rows), "manifest_sha256": history.sha256_file(output / "manifest.private.json")}


def representation_audit(evidence: Path, output: Path) -> dict:
    """Existing development only; a successful witness is not an accuracy result."""
    import apta_1_1_native_temporal_development as retained
    protocol()
    external(output)
    output.mkdir(parents=True, exist_ok=False)
    bindings, report = {}, {}
    for corpus in ("asap", "ballroom"):
        context = retained.preflight(corpus, evidence, bindings)
        totals, details = {k: Counter() for k in ("coarse", "reference_fit")}, []
        for source in context["sources"]:
            truth, coarse = source["truth"], source["coarse"]
            windows = []
            for old in coarse["windows"]:
                fitted = project(truth["beat_frames"], truth["downbeat_frames"], old["first_frame"], old["end_frame"])
                before = annotated.score_window(old, truth, RATE)
                after = annotated.score_window(fitted, truth, RATE)
                totals["coarse"].update(before)
                totals["reference_fit"].update(after)
                windows.append({"coarse": before, "reference_fit": after, "fitted": fitted})
            details.append({"track": truth["track"], "windows": windows})
        if dict(totals["coarse"]) != context["baseline"]["totals"]["coarse"]:
            raise ValueError("retained coarse scores changed")
        report[corpus] = {"totals": {k: dict(v) for k, v in totals.items()}, "details": details}
    retained.recheck(bindings)
    value = {"format": FORMAT, "kind": "reference_fit_representation_audit", "formal_acceptance": False,
        "protocol_sha256": PROTOCOL_SHA256, "candidate_accuracy": None,
        "failed_fit_is_not_impossibility_proof": True, "holdout_performances_opened": False,
        "corpora": report, "input_bindings": {str(k): v for k, v in bindings.items()}}
    history.write_new(output / "report.private.json", value)
    return {k: v["totals"] for k, v in report.items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare-gtzan")
    p.add_argument("--assets", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--ffmpeg", type=Path, default=Path("ffmpeg"))
    p = commands.add_parser("audit-representation")
    p.add_argument("--evidence", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare-gtzan":
            result = prepare(args.assets, args.output, args.ffmpeg)
        else:
            result = representation_audit(args.evidence, args.output)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"neural reference failed: {exc}\n")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
