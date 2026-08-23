#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare and score a Ballroom-based APTA meter/downbeat validation set.

The source dataset contains real 30-second ballroom recordings and manually
corrected beat/bar annotations.  Waltz and Viennese Waltz are 3/4; the other
included dance styles are 4/4.  Prepared audio and annotations are CC
BY-NC-SA 4.0 local-only derivatives and must not be committed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import apta_1_1_asap_meter_validation as common

FORMAT = "apta-1.1-ballroom-meter-validation-1"
REPORT_FORMAT = "apta-1.1-ballroom-meter-report-1"
SELECTION_SEED = "apta-1.1-ballroom-meter-validation-v1"
PER_METER_PER_SPLIT = 20
TRIPLE_GENRES = {"Waltz", "VienneseWaltz"}
AUDIO_ARCHIVE_MD5 = "2872a3e52070bc342a4510a95e2fa0b8"
ANNOTATION_ARCHIVE_MD5 = "d0c31e1a30c0caf8fd22dec25f2174cf"


@dataclass(frozen=True)
class Candidate:
    audio_path: Path
    annotation_path: Path
    genre: str
    meter_numerator: int
    beat_times: tuple[float, ...]
    beat_positions: tuple[int, ...]


def _stable_key(kind: str, value: str) -> str:
    return hashlib.sha256(f"{SELECTION_SEED}:{kind}:{value}".encode()).hexdigest()


def _utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def md5_file(path: Path) -> str:
    # The dataset publishes MD5 as a transport-integrity checksum; this is not
    # a cryptographic identity or security decision.
    digest = hashlib.md5()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_archive(path: Path, expected: str, name: str) -> None:
    if not path.is_file():
        raise common.ValidationError(f"{name} archive not found: {path}")
    observed = md5_file(path)
    if observed != expected:
        raise common.ValidationError(
            f"{name} archive MD5 must be {expected}, found {observed}"
        )


def read_beats(path: Path) -> tuple[tuple[float, ...], tuple[int, ...]]:
    times: list[float] = []
    positions: list[int] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise common.ValidationError(f"cannot read beat annotations {path}: {exc}") from exc
    for line_number, line in enumerate(lines, start=1):
        fields = line.split()
        if len(fields) != 2:
            raise common.ValidationError(f"{path}:{line_number}: expected time and beat position")
        try:
            timestamp = float(fields[0])
            position = int(fields[1])
        except ValueError as exc:
            raise common.ValidationError(f"{path}:{line_number}: invalid beat row") from exc
        if not math.isfinite(timestamp) or timestamp < 0.0 or position <= 0:
            raise common.ValidationError(f"{path}:{line_number}: invalid beat value")
        if times and timestamp <= times[-1]:
            raise common.ValidationError(f"{path}:{line_number}: beat times are not increasing")
        times.append(timestamp)
        positions.append(position)
    if len(times) < 12:
        raise common.ValidationError(f"{path}: at least 12 beat annotations are required")
    return tuple(times), tuple(positions)


def inventory(source: Path) -> list[Candidate]:
    audio_root = source / "BallroomData"
    annotation_root = source / "BallroomAnnotations-master"
    rows: list[Candidate] = []
    for audio in sorted(audio_root.glob("*/*.wav")):
        if audio.parent.name == "nada":
            continue
        annotation = annotation_root / f"{audio.stem}.beats"
        if not annotation.is_file():
            raise common.ValidationError(f"missing annotation for {audio.name}")
        times, positions = read_beats(annotation)
        observed = set(positions)
        expected_meter = 3 if audio.parent.name in TRIPLE_GENRES else 4
        if observed != set(range(1, expected_meter + 1)):
            raise common.ValidationError(
                f"unexpected beat positions for {audio.name}: {sorted(observed)}"
            )
        rows.append(
            Candidate(
                audio_path=audio,
                annotation_path=annotation,
                genre=audio.parent.name,
                meter_numerator=expected_meter,
                beat_times=times,
                beat_positions=positions,
            )
        )
    if len(rows) != 698:
        raise common.ValidationError(f"expected 698 Ballroom tracks, found {len(rows)}")
    return rows


def select_candidates(
    rows: list[Candidate],
    per_meter_per_split: int = PER_METER_PER_SPLIT,
) -> dict[str, list[Candidate]]:
    selected: dict[str, list[Candidate]] = {"development": [], "holdout": []}
    required = per_meter_per_split * 2
    for meter in (3, 4):
        candidates = [row for row in rows if row.meter_numerator == meter]
        candidates.sort(key=lambda row: _stable_key("track", row.audio_path.stem))
        if len(candidates) < required:
            raise common.ValidationError(
                f"only {len(candidates)} eligible {meter}/4 tracks; {required} required"
            )
        selected["development"].extend(candidates[:per_meter_per_split])
        selected["holdout"].extend(candidates[per_meter_per_split:required])
    for split in selected:
        selected[split].sort(key=lambda row: (row.meter_numerator, row.audio_path.stem))
    development = {row.audio_path.stem for row in selected["development"]}
    holdout = {row.audio_path.stem for row in selected["holdout"]}
    if development & holdout:
        raise AssertionError("Ballroom source leaked across splits")
    return selected


def canonicalize(ffmpeg: Path, source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(
            [
                str(ffmpeg),
                "-v",
                "error",
                "-y",
                "-i",
                str(source),
                "-map_metadata",
                "-1",
                "-ar",
                str(common.SAMPLE_RATE),
                "-ac",
                str(common.CHANNELS),
                "-c:a",
                "pcm_s16le",
                str(target),
            ],
            check=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise common.ValidationError(f"ffmpeg failed for {source}: {exc}") from exc


def ffmpeg_version(ffmpeg: Path) -> str:
    try:
        completed = subprocess.run(
            [str(ffmpeg), "-version"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise common.ValidationError(f"cannot query ffmpeg version: {exc}") from exc
    first = completed.stdout.splitlines()
    if not first:
        raise common.ValidationError("ffmpeg version output is empty")
    return first[0]


def prepare(
    source: Path,
    audio_archive: Path,
    annotation_archive: Path,
    ffmpeg: Path,
    output: Path,
) -> dict[str, Any]:
    if not ffmpeg.is_file():
        raise common.ValidationError(f"ffmpeg not found: {ffmpeg}")
    require_archive(audio_archive, AUDIO_ARCHIVE_MD5, "audio")
    require_archive(annotation_archive, ANNOTATION_ARCHIVE_MD5, "annotation")
    converter_version = ffmpeg_version(ffmpeg)
    selected = select_candidates(inventory(source))
    output.mkdir(parents=True, exist_ok=True)
    labels: list[dict[str, Any]] = []
    private_sources: list[dict[str, Any]] = []
    seen: set[str] = set()
    total = sum(len(rows) for rows in selected.values())
    completed = 0
    for split in ("development", "holdout"):
        for row in selected[split]:
            completed += 1
            print(f"canonicalize {completed}/{total} {split} {row.genre}", flush=True)
            temporary = output / "rendering" / f"track-{completed:03d}.wav"
            canonicalize(ffmpeg, row.audio_path, temporary)
            audio_hash = common.sha256_file(temporary)
            track = "track-" + audio_hash[:24]
            if track in seen:
                raise common.ValidationError(f"duplicate canonical audio ID: {track}")
            seen.add(track)
            final_audio = output / "audio" / f"{track}.wav"
            final_audio.parent.mkdir(parents=True, exist_ok=True)
            temporary.replace(final_audio)
            beat_frames = [int(round(value * common.SAMPLE_RATE)) for value in row.beat_times]
            downbeat_frames = [
                frame
                for frame, position in zip(beat_frames, row.beat_positions)
                if position == 1
            ]
            labels.append(
                {
                    "track": track,
                    "split": split,
                    "meter_numerator": row.meter_numerator,
                    "meter_denominator": 4,
                    "beat_frames": beat_frames,
                    "downbeat_frames": downbeat_frames,
                }
            )
            private_sources.append(
                {
                    "track": track,
                    "source": str(row.audio_path.relative_to(source).as_posix()),
                    "annotation": str(row.annotation_path.relative_to(source).as_posix()),
                    "genre": row.genre,
                    "audio_sha256": audio_hash,
                }
            )
    rendering = output / "rendering"
    try:
        rendering.rmdir()
    except OSError:
        pass
    labels.sort(key=lambda value: value["track"])
    private_sources.sort(key=lambda value: value["track"])
    expected_audio = {f"{row['track']}.wav" for row in labels}
    audio_dir = output / "audio"
    actual_audio = {path.name for path in audio_dir.glob("*.wav")}
    if actual_audio != expected_audio:
        raise common.ValidationError(
            "output audio directory contains stale or missing WAV files; use a clean output"
        )
    labels_path = output / "labels.json"
    labels_path.write_text(json.dumps(labels, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (output / "sources.private.json").write_text(
        json.dumps(private_sources, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    counts = {
        split: {
            meter: sum(
                row["split"] == split and row["meter_numerator"] == int(meter[0])
                for row in labels
            )
            for meter in ("3/4", "4/4")
        }
        for split in ("development", "holdout")
    }
    manifest = {
        "format": FORMAT,
        "source": "Ballroom Rhythm Dataset (CC BY-NC-SA 4.0)",
        "audio_archive_md5": AUDIO_ARCHIVE_MD5,
        "annotation_archive_md5": ANNOTATION_ARCHIVE_MD5,
        "selection_seed": SELECTION_SEED,
        "ffmpeg_version": converter_version,
        "generated_utc": _utc_now(),
        "sample_rate": common.SAMPLE_RATE,
        "channels": common.CHANNELS,
        "sample_width_bytes": common.SAMPLE_WIDTH,
        "track_count": len(labels),
        "split_counts": counts,
        "track_ids": [row["track"] for row in labels],
        "labels_sha256": common.sha256_file(labels_path),
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--source", type=Path, required=True)
    prepare_parser.add_argument("--audio-archive", type=Path, required=True)
    prepare_parser.add_argument("--annotation-archive", type=Path, required=True)
    prepare_parser.add_argument("--ffmpeg", type=Path, required=True)
    prepare_parser.add_argument("--output", type=Path, required=True)
    run_parser = subparsers.add_parser("run")
    run_parser.add_argument("--prepared", type=Path, required=True)
    run_parser.add_argument("--analyzer", type=Path, required=True)
    run_parser.add_argument("--output", type=Path, required=True)
    run_parser.add_argument("--split", choices=("development", "holdout"), required=True)
    run_parser.add_argument("--source-revision", required=True)
    eval_parser = subparsers.add_parser("evaluate")
    eval_parser.add_argument("--prepared", type=Path, required=True)
    eval_parser.add_argument("--split", choices=("development", "holdout"), required=True)
    eval_parser.add_argument("--inspector", type=Path, required=True)
    eval_parser.add_argument("--mapping", type=Path, required=True)
    eval_parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            value = prepare(
                args.source,
                args.audio_archive,
                args.annotation_archive,
                args.ffmpeg,
                args.output,
            )
        elif args.command == "run":
            value = common.run_analysis(
                args.prepared,
                args.analyzer,
                args.output,
                args.split,
                args.source_revision,
                FORMAT,
            )
        else:
            value = common.evaluate(
                args.prepared,
                args.split,
                args.inspector,
                args.mapping,
                args.report,
                FORMAT,
                REPORT_FORMAT,
            )
    except (OSError, common.ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(value, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
