#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Audit the spent FMAK mapping and prepare a private, key-only blind review."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import wave

import apta_1_1_fmak_semitone_band_key_development as corpus

MANIFEST = "25e853990cc25dc97929f7767de825e89b9e650c7c1b8d075b6fd98c6d0ef09e"
BASELINE = "5053cff8bf3dd8116b936c6b4bc3c3acc2bab4d7e243adf03d7ceec407bffd80"
REFERENCE = "fa516407b2abb1dda39803053c497b143ea81152d94e062ceae6142146667eb1"
SEED = "apta-key-blind-review-20260904-v1"
GROUPS = ("both_wrong_agree", "both_wrong_disagree", "one_correct")
INSTRUCTIONS = """Slijepa provjera tonaliteta

Preslusajte cijelu svaku snimku. Ne trazite naziv pjesme, izvorne oznake ni
odgovore algoritama. Po mogucnosti koristite instrument za provjeru tonike.
U answers.csv upisite vlastiti odgovor, bez pogadjanja kada niste sigurni:
- tonic: C, C#, D, Eb, E, F, F#, G, Ab, A, Bb ili B (enharmonije su dopustene)
- mode: major, minor, other ili uncertain
- certainty: low, medium ili high (vlastita sigurnost, nije DSP confidence)
- notes: obrazlozenje, vremenske oznake modulacije/nejasnih dijelova
Ako nema stabilnog tonaliteta, ostavite tonic prazan i objasnite u notes.
Spremite popunjeni CSV i vratite ga prije gledanja bilo kakvih drugih rezultata.
Za drugog neovisnog slusatelja koristite novu kopiju praznog obrasca.
Ovaj paket je dijagnosticki uzorak, ne test ukupne tocnosti niti release dokaz.
"""


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def indexed(rows):
    result = {row["track"]: row for row in rows}
    require(len(result) == len(rows), "duplicate track")
    return result


def key(row, prefix="key"):
    return row[prefix + "_tonic"], row[prefix + "_mode"]


def select(rows):
    selected = []
    for group in GROUPS:
        for mode in ("major", "minor"):
            pool = [r for r in rows if r["group"] == group and r["key_mode"] == mode]
            pool.sort(key=lambda r: hashlib.sha256(f'{SEED}:select:{r["track"]}'.encode()).hexdigest())
            require(len(pool) >= 2, "insufficient frozen review stratum")
            selected.extend(pool[:2])
    selected.sort(key=lambda r: hashlib.sha256(f'{SEED}:order:{r["track"]}'.encode()).hexdigest())
    require(len({r["track"] for r in selected}) == 12, "nonunique review selection")
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prepared", "metadata", "baseline", "reference", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    require(not subprocess.check_output(["git", "status", "--porcelain"], cwd=root), "source checkout must be clean")
    require(not args.output.exists(), "output already exists; never overwrite reviews")
    for path, expected in ((args.prepared / "manifest.json", MANIFEST),
                           (args.baseline, BASELINE), (args.reference, REFERENCE)):
        require(sha(path) == expected, "frozen input hash mismatch")
    manifest, labels = corpus.load_prepared(args.prepared)
    candidates = corpus.select_candidates(corpus.transport.inventory(args.metadata))
    require(corpus.transport.selection_sha256(candidates) == corpus.SELECTION_SHA256, "selection mismatch")
    originals = {r.source_id: r for r in candidates}
    mapping_path = args.prepared / "private-sources.json"
    require(sha(mapping_path) == manifest["private_sources_sha256"], "mapping hash mismatch")
    mappings = indexed(json.loads(mapping_path.read_text(encoding="utf-8")))
    baseline = indexed(json.loads(args.baseline.read_text(encoding="utf-8"))["tracks"])
    reference = indexed(json.loads(args.reference.read_text(encoding="utf-8"))["rows"])
    ids = set(indexed(labels))
    require(ids == set(mappings) == set(baseline) == set(reference), "identity coverage mismatch")
    require({int(r["source_id"]) for r in mappings.values()} == set(originals), "source coverage mismatch")
    rows = []
    for label in labels:
        track = label["track"]
        mapping = mappings[track]
        source = originals[int(mapping["source_id"])]
        require(label["split"] == "development", "not development")
        require(key(label) == (source.key_tonic, source.key_mode), "metadata label mismatch")
        require(key(label) == key(baseline[track], "expected") == key(reference[track], "expected"), "report label mismatch")
        path = args.prepared / "audio" / (track + ".wav")
        digest = sha(path)
        require(digest == mapping["canonical_sha256"] and track == "track-" + digest[:24], "WAV hash mismatch")
        with wave.open(str(path), "rb") as wav:
            require((wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getcomptype()) == (48000, 2, 2, "NONE"), "WAV format mismatch")
            seconds = wav.getnframes() / wav.getframerate()
        b, r = key(baseline[track]), key(reference[track])
        bc, rc = b == key(label), r == key(label)
        require(bc == baseline[track]["key_correct"] and rc == reference[track]["correct"], "correctness flag mismatch")
        group = "both_correct" if bc and rc else "one_correct" if bc != rc else "both_wrong_agree" if b == r else "both_wrong_disagree"
        rows.append(dict(label, group=group, baseline=b, reference=r, seconds=seconds, sha256=digest))
    selected = select(rows)
    listener = args.output / "listener"
    listener.mkdir(parents=True)
    hidden = []
    for i, row in enumerate(selected, 1):
        sample = f"A{i:02d}"
        target = listener / (sample + ".wav")
        shutil.copyfile(args.prepared / "audio" / (row["track"] + ".wav"), target)
        require(sha(target) == row["sha256"], "copy hash mismatch")
        hidden.append(dict(row, sample=sample))
    with (listener / "answers.csv").open("x", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["sample", "tonic", "mode", "certainty", "notes"])
        writer.writerows([r["sample"], "", "", "", ""] for r in hidden)
    (listener / "README.txt").write_text(INSTRUCTIONS, encoding="utf-8")
    items = "\n".join(f'<h2>{r["sample"]}</h2><audio controls preload="none" src="{r["sample"]}.wav"></audio>' for r in hidden)
    (listener / "index.html").write_text('<!doctype html><html lang="hr"><meta charset="utf-8"><title>Slijepa provjera</title><body><h1>Slijepa provjera tonaliteta</h1><pre>' + INSTRUCTIONS + '</pre><p><a href="answers.csv" download>Obrazac za odgovore</a></p>' + items + '</body></html>', encoding="utf-8")
    summary = dict(audited_tracks=len(rows), metadata_label_matches=len(rows), report_label_matches=len(rows),
                   canonical_hash_matches=len(rows), listener_files_verified=12, acceptance_claim=False,
                   archive_redecode_performed=False, musical_label_truth="awaiting-independent-listening")
    report = dict(format="apta-key-blind-review-1", source_revision=revision, selection_seed=SEED,
                  tool_sha256=sha(Path(__file__)), manifest_sha256=MANIFEST, baseline_sha256=BASELINE,
                  reference_sha256=REFERENCE, metadata_sha256=sha(args.metadata), summary=summary, private_rows=hidden)
    (args.output / "coordinator-private.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    # Publish only aggregates on stdout, never the blind answer key or track mapping.
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
