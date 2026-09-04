#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Re-decode the twelve reviewed FMAK archive members without altering inputs."""
import argparse
import csv
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import wave
import zipfile

import apta_1_1_fmak_semitone_band_key_development as corpus

COORDINATOR = "90d2b860fea3a694c15334356d1293731a8a0a9bc1506b228bdd7e4e8a349ed3"
MANIFEST = "25e853990cc25dc97929f7767de825e89b9e650c7c1b8d075b6fd98c6d0ef09e"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def pcm_identity(path):
    with wave.open(str(path), "rb") as wav:
        geometry = (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes(), wav.getcomptype())
        digest = hashlib.sha256()
        size = 0
        while data := wav.readframes(65536):
            size += len(data)
            digest.update(data)
        require(size == geometry[1] * geometry[2] * geometry[3], "truncated PCM")
    return dict(geometry=geometry, pcm_sha256=digest.hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prepared", "archive", "metadata", "coordinator", "listener", "output", "ffmpeg"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    args = parser.parse_args()
    require(not args.output.exists(), "output exists; no overwrite permitted")
    require(len(args.source_revision) == 40 and all(c in "0123456789abcdef" for c in args.source_revision), "invalid source revision")
    require(sha(args.coordinator) == COORDINATOR, "coordinator changed")
    require(sha(args.prepared / "manifest.json") == MANIFEST, "manifest changed")
    manifest, labels = corpus.load_prepared(args.prepared)
    mapping_path = args.prepared / "private-sources.json"
    require(sha(mapping_path) == manifest["private_sources_sha256"], "mapping changed")
    mapping = {r["track"]: r for r in json.loads(mapping_path.read_text())}
    labels = {r["track"]: r for r in labels}
    selected = corpus.select_candidates(corpus.transport.inventory(args.metadata))
    require(corpus.transport.selection_sha256(selected) == corpus.SELECTION_SHA256, "selection changed")
    originals = {r.source_id: r for r in selected}
    with args.metadata.open(encoding="utf-8-sig", newline="") as stream:
        raw_metadata = {int(r["track_id"]): r["key_and_mode"] for r in csv.DictReader(stream)}
    rows = json.loads(args.coordinator.read_text())["private_rows"]
    require(len(rows) == 12 and {r["sample"] for r in rows} == {f"A{i:02d}" for i in range(1, 13)}, "review inventory changed")
    require(len({r["track"] for r in rows}) == 12, "duplicate reviewed track")
    version = corpus.transport.shared._ffmpeg_version(args.ffmpeg)
    require(version == manifest["ffmpeg_version"], "decoder version changed; stop before comparison")
    require(args.archive.stat().st_size == manifest["archive_size"], "archive size changed")
    print("Verifying full source archive...", flush=True)
    archive_md5, archive_sha = hashlib.md5(), hashlib.sha256()
    with args.archive.open("rb") as stream:
        while block := stream.read(8 * 1024 * 1024):
            archive_md5.update(block)
            archive_sha.update(block)
    require(archive_md5.hexdigest() == manifest["archive_md5"], "archive MD5 changed")
    args.output.mkdir(parents=True)
    report = dict(format="apta-key-review-source-audit-1", acceptance_claim=False,
                  source_revision=args.source_revision, source_worktree_clean=False,
                  evidence_level="diagnostic-source-identity-only", tool_sha256=sha(Path(__file__)),
                  helper_sha256={Path(m.__file__).name: sha(Path(m.__file__)) for m in (corpus, corpus.transport, corpus.transport.shared)},
                  manifest_sha256=MANIFEST, coordinator_sha256=COORDINATOR,
                  metadata_sha256=sha(args.metadata), archive_sha256=archive_sha.hexdigest(),
                  archive_md5=archive_md5.hexdigest(), ffmpeg_version=version, ffmpeg_sha256=sha(args.ffmpeg), rows=[])
    with zipfile.ZipFile(args.archive) as archive:
        members = corpus.transport._archive_members(archive, [originals[int(mapping[r["track"]]["source_id"])] for r in rows])
        for row in sorted(rows, key=lambda r: r["sample"]):
            sample, track = row["sample"], row["track"]
            source_id = int(mapping[track]["source_id"])
            original = originals[source_id]
            label = (original.key_tonic, original.key_mode)
            require(label == corpus.transport.normalize_key(raw_metadata[source_id]) == (row["key_tonic"], row["key_mode"]) == (labels[track]["key_tonic"], labels[track]["key_mode"]), "CSV/source label mismatch")
            member = members[source_id]
            require(PurePosixPath(member.filename).name == f"{source_id:06d}.mp3", "source ID/member mismatch")
            source = args.output / (sample + ".mp3")
            target = args.output / (sample + ".wav")
            with archive.open(member) as incoming, source.open("xb") as outgoing:
                shutil.copyfileobj(incoming, outgoing)
            corpus.transport.shared._canonicalize(args.ffmpeg, source, target)
            prepared = args.prepared / "audio" / (track + ".wav")
            listener = args.listener / (sample + ".wav")
            expected = mapping[track]["canonical_sha256"]
            hashes = [sha(path) for path in (target, prepared, listener)]
            identities = [pcm_identity(path) for path in (target, prepared, listener)]
            exact = hashes == [expected] * 3 and expected == row["sha256"] and track == "track-" + expected[:24]
            pcm_equal = identities[0] == identities[1] == identities[2]
            report["rows"].append(dict(sample=sample, source_id=source_id, archive_member=member.filename,
                                       original_label=raw_metadata[source_id], source_mp3_sha256=sha(source),
                                       redecode_sha256=hashes[0], prepared_sha256=hashes[1], listener_sha256=hashes[2],
                                       pcm=identities[0], csv_label_match=True, full_wav_match=exact, pcm_match=pcm_equal))
            report["summary"] = dict(checked=len(report["rows"]), label_matches=sum(r["csv_label_match"] for r in report["rows"]),
                                     wav_matches=sum(r["full_wav_match"] for r in report["rows"]),
                                     pcm_matches=sum(r["pcm_match"] for r in report["rows"]), complete=len(report["rows"]) == 12,
                                     acceptance_claim=False)
            (args.output / "report-private.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            require(exact and pcm_equal, "audio identity mismatch; see private report; stopped")
            print(f"{sample}: original CSV, complete WAV and PCM match", flush=True)
    print(json.dumps(report["summary"], sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
