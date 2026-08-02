#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare and evaluate a read-only Rekordbox tempo corpus.

The importer reads PPTH and PQTZ directly from ANLZ0000.DAT files.  It never
writes to the Rekordbox device and never copies source recordings into the
repository.  FFmpeg creates bounded WAV windows in a caller-selected build
directory; the resulting public manifest and reports contain opaque track IDs,
not titles or source paths.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import shutil
import statistics
import struct
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Sequence


FORMAT_VERSION = "apta-rekordbox-tempo-corpus-0.1"
SPLIT_FORMAT_VERSION = "apta-rekordbox-tempo-split-0.1"
DEFAULT_START_SECONDS = 30
DEFAULT_DURATION_SECONDS = 90
DEFAULT_SAMPLE_RATE = 44100
DEFAULT_CHANNELS = 2
DEFAULT_SPLIT_SEED = "libapta-phase5-v1"
DEFAULT_HOLDOUT_FRACTION = 0.25
ACTIONABLE_CONFIDENCE = 75
GATES = (50, 55, 60, 65, 70, 75, 80, 85, 90, 95)
MODES = {
    "S4": ("s4", []),
    "S4 + S6 endorsement": ("s4-endorsed", ["--request-global"]),
    "S6": ("s6", ["--global"]),
}


class AnlzError(ValueError):
    """An ANLZ file is malformed or lacks the required tempo annotations."""


@dataclass(frozen=True)
class AnlzTempo:
    track_path: str
    modal_tempo_x100: int
    beat_count: int
    unique_tempo_count: int
    modal_share: float


@dataclass(frozen=True)
class CorpusTrack:
    track_id: str
    truth_bpm: float
    beat_count: int
    unique_tempo_count: int
    modal_share: float
    audio_path: str
    analysis_path: str


@dataclass(frozen=True)
class ImportFailure:
    analysis_path: str
    stage: str
    error: str


def _be_u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise AnlzError("truncated 32-bit field")
    return struct.unpack_from(">I", data, offset)[0]


def parse_anlz_bytes(data: bytes) -> AnlzTempo:
    """Return PPTH path and modal PQTZ tempo from one Rekordbox ANLZ file."""

    if len(data) < 28 or data[:4] != b"PMAI":
        raise AnlzError("missing PMAI header")
    header_len = _be_u32(data, 4)
    file_len = _be_u32(data, 8)
    if header_len < 12 or header_len > len(data):
        raise AnlzError("invalid PMAI header length")
    if file_len < header_len or file_len > len(data):
        raise AnlzError("invalid PMAI file length")

    track_path: str | None = None
    tempi: list[int] | None = None
    offset = header_len
    while offset < file_len:
        if offset + 12 > file_len:
            raise AnlzError("truncated tag header")
        tag_type = data[offset : offset + 4]
        tag_header_len = _be_u32(data, offset + 4)
        tag_len = _be_u32(data, offset + 8)
        if tag_header_len < 12 or tag_len < tag_header_len:
            raise AnlzError("invalid tag length")
        tag_end = offset + tag_len
        if tag_end > file_len:
            raise AnlzError("tag extends beyond PMAI data")

        if tag_type == b"PPTH":
            if tag_header_len < 16:
                raise AnlzError("short PPTH header")
            path_len = _be_u32(data, offset + 12)
            available = tag_end - (offset + 16)
            if path_len > available:
                raise AnlzError("PPTH path extends beyond tag")
            encoded = data[offset + 16 : offset + 16 + path_len]
            try:
                track_path = encoded.decode("utf-16-be").rstrip("\x00")
            except UnicodeDecodeError as exc:
                raise AnlzError("invalid PPTH UTF-16 path") from exc
        elif tag_type == b"PQTZ":
            if tag_header_len < 24:
                raise AnlzError("short PQTZ header")
            entry_count = _be_u32(data, offset + 20)
            required = entry_count * 8
            if required > tag_end - (offset + tag_header_len):
                raise AnlzError("PQTZ entries extend beyond tag")
            tempi = []
            entry_offset = offset + tag_header_len
            for index in range(entry_count):
                _beat, tempo_x100, _time_ms = struct.unpack_from(
                    ">HHI", data, entry_offset + index * 8
                )
                if tempo_x100 == 0:
                    raise AnlzError("PQTZ contains a zero tempo")
                tempi.append(tempo_x100)

        offset = tag_end

    if offset != file_len:
        raise AnlzError("tag walk did not end at PMAI file length")
    if not track_path:
        raise AnlzError("missing PPTH track path")
    if not tempi:
        raise AnlzError("missing or empty PQTZ beat grid")

    counts = Counter(tempi)
    modal_tempo, modal_count = counts.most_common(1)[0]
    return AnlzTempo(
        track_path=track_path,
        modal_tempo_x100=modal_tempo,
        beat_count=len(tempi),
        unique_tempo_count=len(counts),
        modal_share=modal_count / len(tempi),
    )


def parse_anlz_file(path: Path) -> AnlzTempo:
    return parse_anlz_bytes(path.read_bytes())


def track_path_on_device(device_root: Path, rekordbox_path: str) -> Path:
    """Map a slash-separated PPTH path under device_root without traversal."""

    logical = PurePosixPath(rekordbox_path)
    parts = logical.parts[1:] if logical.is_absolute() else logical.parts
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise AnlzError(f"unsafe PPTH path: {rekordbox_path!r}")
    if parts[0].casefold() != "contents":
        raise AnlzError(f"PPTH path is outside /Contents: {rekordbox_path!r}")
    return device_root.joinpath(*parts)


def opaque_track_id(rekordbox_path: str) -> str:
    normalized = str(PurePosixPath(rekordbox_path)).casefold().encode("utf-8")
    return "rbx-" + hashlib.sha256(normalized).hexdigest()[:16]


def _relative_posix(path: Path, root: Path) -> str:
    return "/" + path.relative_to(root).as_posix()


def scan_device(device_root: Path) -> tuple[list[CorpusTrack], list[ImportFailure]]:
    analysis_root = device_root / "PIONEER" / "USBANLZ"
    if not analysis_root.is_dir():
        raise FileNotFoundError(f"Rekordbox analysis directory not found: {analysis_root}")

    tracks: list[CorpusTrack] = []
    failures: list[ImportFailure] = []
    seen_ids: set[str] = set()

    def walk_error(exc: OSError) -> None:
        filename = Path(exc.filename) if exc.filename else analysis_root
        try:
            label = _relative_posix(filename, device_root)
        except ValueError:
            label = str(filename)
        failures.append(
            ImportFailure(
                analysis_path=label,
                stage="traversal",
                error=f"{type(exc).__name__}: {exc}",
            )
        )

    analysis_paths: list[Path] = []
    for directory, _subdirectories, filenames in os.walk(
        analysis_root, onerror=walk_error
    ):
        for filename in filenames:
            if filename.casefold() == "anlz0000.dat":
                analysis_paths.append(Path(directory) / filename)
    analysis_paths.sort(key=lambda path: str(path).casefold())
    for analysis_path in analysis_paths:
        analysis_label = _relative_posix(analysis_path, device_root)
        try:
            annotation = parse_anlz_file(analysis_path)
            audio_path = track_path_on_device(device_root, annotation.track_path)
            with audio_path.open("rb") as audio:
                audio.read(1)
            track_id = opaque_track_id(annotation.track_path)
            if track_id in seen_ids:
                raise AnlzError(f"duplicate track ID for {annotation.track_path!r}")
            seen_ids.add(track_id)
            tracks.append(
                CorpusTrack(
                    track_id=track_id,
                    truth_bpm=annotation.modal_tempo_x100 / 100.0,
                    beat_count=annotation.beat_count,
                    unique_tempo_count=annotation.unique_tempo_count,
                    modal_share=annotation.modal_share,
                    audio_path=str(audio_path),
                    analysis_path=str(analysis_path),
                )
            )
        except (OSError, AnlzError) as exc:
            failures.append(
                ImportFailure(
                    analysis_path=analysis_label,
                    stage="scan",
                    error=f"{type(exc).__name__}: {exc}",
                )
            )

    tracks.sort(key=lambda track: track.track_id)
    return tracks, failures


def _decode_one(
    ffmpeg: str,
    track: CorpusTrack,
    wav_dir: Path,
    start_seconds: int,
    duration_seconds: int,
    sample_rate: int,
    channels: int,
    overwrite: bool,
) -> tuple[CorpusTrack, str | None]:
    output = wav_dir / f"{track.track_id}.wav"
    if output.is_file() and not overwrite:
        return track, None
    partial = wav_dir / f"{track.track_id}.partial.wav"
    if partial.exists():
        partial.unlink()
    command = [
        ffmpeg,
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "error",
        "-ss",
        str(start_seconds),
        "-i",
        track.audio_path,
        "-t",
        str(duration_seconds),
        "-map",
        "0:a:0",
        "-vn",
        "-ac",
        str(channels),
        "-ar",
        str(sample_rate),
        "-c:a",
        "pcm_s16le",
        "-y",
        str(partial),
    ]
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        if partial.exists():
            partial.unlink()
        error = completed.stderr.strip() or f"FFmpeg exited {completed.returncode}"
        return track, error
    partial.replace(output)
    return track, None


def _public_track(track: CorpusTrack) -> dict[str, object]:
    return {
        "id": track.track_id,
        "truth_bpm": track.truth_bpm,
        "beat_count": track.beat_count,
        "unique_grid_tempi": track.unique_tempo_count,
        "modal_grid_share": round(track.modal_share, 6),
    }


def _source_fingerprint(device_root: Path) -> dict[str, str]:
    fingerprints: dict[str, str] = {}
    for relative in (
        Path("PIONEER/rekordbox/export.pdb"),
        Path("PIONEER/rekordbox/exportLibrary.db"),
    ):
        path = device_root / relative
        try:
            fingerprints[relative.as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError:
            continue
    return fingerprints


def prepare(args: argparse.Namespace) -> int:
    device_root = Path(args.device_root).resolve()
    output_dir = Path(args.output_dir).resolve()
    if output_dir == device_root or device_root in output_dir.parents:
        raise SystemExit("output directory must not be on the Rekordbox device")
    output_dir.mkdir(parents=True, exist_ok=True)
    wav_dir = output_dir / "wav"
    wav_dir.mkdir(parents=True, exist_ok=True)

    tracks, failures = scan_device(device_root)
    scan_failure_count = len(failures)
    readable_annotated_tracks = len(tracks)
    if args.limit is not None:
        tracks = tracks[: args.limit]
    if not tracks:
        raise SystemExit("no readable Rekordbox tracks with PPTH and PQTZ were found")

    ffmpeg = shutil.which(args.ffmpeg)
    if ffmpeg is None:
        raise SystemExit(f"FFmpeg executable not found: {args.ffmpeg}")

    decoded: list[CorpusTrack] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        future_map = {
            executor.submit(
                _decode_one,
                ffmpeg,
                track,
                wav_dir,
                args.start,
                args.duration,
                args.sample_rate,
                args.channels,
                args.overwrite,
            ): track
            for track in tracks
        }
        complete = 0
        for future in as_completed(future_map):
            track, error = future.result()
            complete += 1
            if error is None:
                decoded.append(track)
            else:
                failures.append(
                    ImportFailure(
                        analysis_path=_relative_posix(
                            Path(track.analysis_path), device_root
                        ),
                        stage="decode",
                        error=error,
                    )
                )
            print(f"decode {complete}/{len(tracks)}", end="\r", flush=True)
    print()
    decoded.sort(key=lambda track: track.track_id)

    private_manifest = {
        "format": FORMAT_VERSION,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "device_root": str(device_root),
        "window": {
            "start_seconds": args.start,
            "duration_seconds": args.duration,
            "sample_rate": args.sample_rate,
            "channels": args.channels,
        },
        "tracks": [asdict(track) for track in decoded],
        "failures": [asdict(failure) for failure in failures],
    }
    public_manifest = {
        "format": FORMAT_VERSION,
        "generated_utc": private_manifest["generated_utc"],
        "source_fingerprints": _source_fingerprint(device_root),
        "window": private_manifest["window"],
        "analysis_files_seen": readable_annotated_tracks + scan_failure_count,
        "readable_annotated_tracks": readable_annotated_tracks,
        "selected_track_count": len(tracks),
        "decoded_track_count": len(decoded),
        "excluded_count": len(failures),
        "tracks": [_public_track(track) for track in decoded],
        "exclusions": [
            {"stage": failure.stage, "error_type": failure.error.split(":", 1)[0]}
            for failure in failures
        ],
    }
    (output_dir / "corpus.private.json").write_text(
        json.dumps(private_manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (output_dir / "corpus.json").write_text(
        json.dumps(public_manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    with (output_dir / "tracks.txt").open("w", encoding="utf-8", newline="\n") as out:
        out.write("# Generated WAV windows; source audio remains on the Rekordbox device.\n")
        for track in decoded:
            out.write(f"{wav_dir / (track.track_id + '.wav')} {track.truth_bpm:.2f}\n")

    print(
        f"prepared {len(decoded)} track(s); excluded {len(failures)}; "
        f"output: {output_dir}"
    )
    return 1 if args.strict and failures else 0


def _read_results(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    with path.open(newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            candidate_tempi = [
                int(value) / 1000.0
                for value in row.get("candidate_millibpm", "").split(";")
                if value
            ]
            candidate_scores = [
                int(value)
                for value in row.get("candidate_scores", "").split(";")
                if value
            ]
            if len(candidate_tempi) != len(candidate_scores):
                raise ValueError(f"candidate column length mismatch in {path}")
            rows.append(
                {
                    "track": Path(row["track"]).stem,
                    "truth": int(row["truth_millibpm"]) / 1000.0,
                    "reported": int(row["reported_millibpm"]) / 1000.0,
                    "relation": row["relation"],
                    "confidence": int(row["confidence"]),
                    "state": int(row["state"]),
                    "candidate_count": int(row["candidate_count"]),
                    "separation": float(row["separation"]),
                    "octave_error": bool(int(row["octave_error"])),
                    "candidate_tempi": candidate_tempi,
                    "candidate_scores": candidate_scores,
                    "global_tempo": int(row.get("global_millibpm", "0") or 0)
                    / 1000.0,
                    "global_confidence": int(row.get("global_confidence", "0") or 0),
                }
            )
    return rows


def _split_stratum(
    s4: dict[str, object], endorsed: dict[str, object]
) -> str:
    """Classify a baseline outcome before any phase-5 tuning occurs."""

    if bool(s4["octave_error"]) and int(s4["confidence"]) >= ACTIONABLE_CONFIDENCE:
        return "s4-high-confidence-octave"
    if s4["relation"] == "exact" and endorsed["relation"] != "exact":
        return "endorsement-broken"
    if s4["relation"] != "exact" and endorsed["relation"] == "exact":
        return "endorsement-fixed"
    if s4["relation"] == "exact":
        return "s4-exact-unchanged"
    if bool(s4["octave_error"]):
        return "s4-octave-unresolved"
    return "s4-other-unresolved"


def make_split(
    s4_rows: Sequence[dict[str, object]],
    endorsed_rows: Sequence[dict[str, object]],
    seed: str = DEFAULT_SPLIT_SEED,
    holdout_fraction: float = DEFAULT_HOLDOUT_FRACTION,
) -> dict[str, object]:
    """Return a deterministic stratified development/hold-out assignment."""

    if not 0.0 < holdout_fraction < 1.0:
        raise ValueError("holdout fraction must be between zero and one")
    endorsed_by_id = {str(row["track"]): row for row in endorsed_rows}
    if len(endorsed_by_id) != len(endorsed_rows):
        raise ValueError("endorsed result set contains duplicate track IDs")

    strata: dict[str, list[str]] = {}
    seen_s4: set[str] = set()
    for row in s4_rows:
        track_id = str(row["track"])
        if track_id in seen_s4:
            raise ValueError("S4 result set contains duplicate track IDs")
        seen_s4.add(track_id)
        endorsed = endorsed_by_id.get(track_id)
        if endorsed is None:
            raise ValueError(f"endorsed result set lacks {track_id}")
        stratum = _split_stratum(row, endorsed)
        strata.setdefault(stratum, []).append(track_id)
    if len(endorsed_by_id) != len(s4_rows):
        raise ValueError("S4 and endorsed result sets have different track IDs")

    assignments: dict[str, tuple[str, str]] = {}
    stratum_counts: dict[str, dict[str, int]] = {}
    for stratum in sorted(strata):
        ids = sorted(
            strata[stratum],
            key=lambda track_id: (
                hashlib.sha256(f"{seed}\0{track_id}".encode("utf-8")).digest(),
                track_id,
            ),
        )
        holdout_count = int(len(ids) * holdout_fraction + 0.5)
        if len(ids) >= 2:
            holdout_count = min(max(holdout_count, 1), len(ids) - 1)
        else:
            holdout_count = 0
        holdout_ids = set(ids[:holdout_count])
        stratum_counts[stratum] = {
            "total": len(ids),
            "development": len(ids) - holdout_count,
            "holdout": holdout_count,
        }
        for track_id in ids:
            partition = "holdout" if track_id in holdout_ids else "development"
            assignments[track_id] = (partition, stratum)

    tracks = [
        {"id": track_id, "partition": partition, "stratum": stratum}
        for track_id, (partition, stratum) in sorted(assignments.items())
    ]
    return {
        "format": SPLIT_FORMAT_VERSION,
        "seed": seed,
        "requested_holdout_fraction": holdout_fraction,
        "track_count": len(tracks),
        "development_count": sum(
            track["partition"] == "development" for track in tracks
        ),
        "holdout_count": sum(track["partition"] == "holdout" for track in tracks),
        "strata": stratum_counts,
        "tracks": tracks,
    }


def _track_lines_by_id(path: Path) -> dict[str, str]:
    lines: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        audio_path, separator, _truth = line.rpartition(" ")
        if not separator or not audio_path:
            raise ValueError(f"malformed track-list row: {raw!r}")
        track_id = Path(audio_path).stem
        if track_id in lines:
            raise ValueError(f"duplicate track-list ID: {track_id}")
        lines[track_id] = line
    return lines


def _partition_rows(
    rows: Sequence[dict[str, object]], split: dict[str, object], partition: str
) -> list[dict[str, object]]:
    selected = {
        str(track["id"])
        for track in split["tracks"]
        if track["partition"] == partition
    }
    result = [row for row in rows if str(row["track"]) in selected]
    result_ids = [str(row["track"]) for row in result]
    if len(result_ids) != len(set(result_ids)):
        raise ValueError(f"{partition} result set contains duplicate track IDs")
    if set(result_ids) != selected:
        raise ValueError(f"{partition} result IDs do not match the frozen split")
    return result


def _comparison_counts(comparison: dict[str, object]) -> dict[str, int]:
    return {
        "changed_selection_count": int(comparison["changed_selection_count"]),
        "fixed_count": int(comparison["fixed_count"]),
        "broken_count": int(comparison["broken_count"]),
        "net_exact_gain": int(comparison["net_exact_gain"]),
    }


def split_summary(
    rows_by_mode: dict[str, Sequence[dict[str, object]]], split: dict[str, object]
) -> dict[str, object]:
    partitions: dict[str, object] = {}
    for partition in ("development", "holdout"):
        partition_rows = {
            name: _partition_rows(rows, split, partition)
            for name, rows in rows_by_mode.items()
        }
        summaries = {
            name: summarize_results(rows) for name, rows in partition_rows.items()
        }
        comparison = compare_results(
            partition_rows["S4"], partition_rows["S4 + S6 endorsement"]
        )
        partitions[partition] = {
            "modes": summaries,
            "endorsement": _comparison_counts(comparison),
        }
    return {
        "format": "apta-rekordbox-tempo-split-report-0.1",
        "split_format": split["format"],
        "seed": split["seed"],
        "track_count": split["track_count"],
        "partitions": partitions,
    }


def render_split_report(report: dict[str, object]) -> str:
    lines = [
        "# Phase-5 frozen split baseline",
        "",
        f"- Tracks: {report['track_count']}",
        f"- Seed: `{report['seed']}`",
        "- Hold-out rows are reported only as aggregates.",
    ]
    for partition in ("development", "holdout"):
        data = report["partitions"][partition]
        lines.extend(
            [
                "",
                f"## {partition.title()}",
                "",
                "| Mode | Tracks | Within 1% | Octave | Other | Errors >=75 | Octave errors >=75 |",
                "|---|---:|---:|---:|---:|---:|---:|",
            ]
        )
        for name, summary in data["modes"].items():
            lines.append(
                f"| {name} | {summary['tracks']} | "
                f"{summary['within_1_percent']} ({_percent(summary['within_1_percent_rate'])}) | "
                f"{summary['octave_family_errors']} | {summary['other_errors']} | "
                f"{summary['high_confidence_errors']} | "
                f"{summary['high_confidence_octave_errors']} |"
            )
        endorsement = data["endorsement"]
        lines.extend(
            [
                "",
                "Endorsement: "
                f"changed {endorsement['changed_selection_count']}, "
                f"fixed {endorsement['fixed_count']}, "
                f"broke {endorsement['broken_count']}, "
                f"net {endorsement['net_exact_gain']:+d}.",
            ]
        )
    lines.extend(
        [
            "",
            "Audio, titles, source paths and per-track hold-out outcomes are absent.",
            "",
        ]
    )
    return "\n".join(lines)


def write_split_baseline(prepared: Path, split: dict[str, object]) -> None:
    rows_by_mode = {
        name: _read_results(prepared / f"results-{stem}.csv")
        for name, (stem, _mode_args) in MODES.items()
    }
    report = split_summary(rows_by_mode, split)
    report["generated_utc"] = datetime.now(timezone.utc).isoformat()
    (prepared / "split-baseline-report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    (prepared / "SPLIT-BASELINE.md").write_text(
        render_split_report(report), encoding="utf-8"
    )


def split_corpus(args: argparse.Namespace) -> int:
    prepared = Path(args.prepared).resolve()
    manifest_path = prepared / "corpus.json"
    tracks_path = prepared / "tracks.txt"
    if not manifest_path.is_file() or not tracks_path.is_file():
        raise SystemExit("prepared directory lacks corpus.json or tracks.txt")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    s4_rows = _read_results(prepared / "results-s4.csv")
    endorsed_rows = _read_results(prepared / "results-s4-endorsed.csv")
    split = make_split(s4_rows, endorsed_rows, args.seed, args.holdout_fraction)
    if split["track_count"] != manifest["decoded_track_count"]:
        raise SystemExit("baseline result count does not match corpus manifest")

    split["generated_utc"] = datetime.now(timezone.utc).isoformat()
    split["corpus_sha256"] = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
    split["baseline_sha256"] = {
        name: hashlib.sha256((prepared / name).read_bytes()).hexdigest()
        for name in ("results-s4.csv", "results-s4-endorsed.csv", "results-s6.csv")
    }
    output_path = prepared / args.output
    output_path.write_text(
        json.dumps(split, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    source_lines = _track_lines_by_id(tracks_path)
    assignments = {track["id"]: track["partition"] for track in split["tracks"]}
    if set(source_lines) != set(assignments):
        raise SystemExit("track list and split contain different IDs")
    for partition in ("development", "holdout"):
        rows = [
            source_lines[track_id]
            for track_id in sorted(source_lines)
            if assignments[track_id] == partition
        ]
        (prepared / f"tracks-{partition}.txt").write_text(
            "# Phase-5 frozen partition; source audio remains outside Git.\n"
            + "\n".join(rows)
            + "\n",
            encoding="utf-8",
        )

    write_split_baseline(prepared, split)

    print(
        f"split {split['track_count']} track(s): "
        f"development={split['development_count']}, holdout={split['holdout_count']}; "
        f"output: {output_path}"
    )
    return 0


def summarize_results(rows: Sequence[dict[str, object]]) -> dict[str, object]:
    if not rows:
        raise ValueError("result CSV is empty")
    total = len(rows)
    correct = [row for row in rows if row["relation"] == "exact"]
    errors = [row for row in rows if row["relation"] != "exact"]
    error_percent = [
        abs(float(row["reported"]) - float(row["truth"]))
        / float(row["truth"])
        * 100.0
        for row in rows
        if float(row["reported"]) > 0.0
    ]
    exact_error_percent = [
        abs(float(row["reported"]) - float(row["truth"]))
        / float(row["truth"])
        * 100.0
        for row in correct
    ]
    half_beat_minutes = [
        0.5 / abs(float(row["reported"]) - float(row["truth"]))
        for row in correct
        if float(row["reported"]) != float(row["truth"])
    ]
    gates: dict[str, object] = {}
    for gate in GATES:
        admitted = [row for row in rows if int(row["confidence"]) >= gate]
        admitted_correct = [row for row in admitted if row["relation"] == "exact"]
        admitted_wrong = len(admitted) - len(admitted_correct)
        gates[str(gate)] = {
            "admitted": len(admitted),
            "correct": len(admitted_correct),
            "wrong": admitted_wrong,
            "precision": (
                len(admitted_correct) / len(admitted) if admitted else None
            ),
            "recall": len(admitted_correct) / len(correct) if correct else None,
            "octave_errors": sum(bool(row["octave_error"]) for row in admitted),
        }
    return {
        "tracks": total,
        "within_1_percent": len(correct),
        "within_1_percent_rate": len(correct) / total,
        "within_0_1_percent": sum(value <= 0.1 for value in error_percent),
        "octave_family_errors": sum(bool(row["octave_error"]) for row in rows),
        "other_errors": sum(
            not bool(row["octave_error"]) for row in errors
        ),
        "correct_confidence_mean": (
            statistics.fmean(int(row["confidence"]) for row in correct)
            if correct
            else None
        ),
        "incorrect_confidence_mean": (
            statistics.fmean(int(row["confidence"]) for row in errors)
            if errors
            else None
        ),
        "incorrect_confidence_max": (
            max(int(row["confidence"]) for row in errors) if errors else None
        ),
        "correct_confidence_min": (
            min(int(row["confidence"]) for row in correct) if correct else None
        ),
        "median_error_percent_among_correct": (
            statistics.median(exact_error_percent) if exact_error_percent else None
        ),
        "median_minutes_to_half_beat_among_correct_nonzero": (
            statistics.median(half_beat_minutes) if half_beat_minutes else None
        ),
        "actionable_threshold": ACTIONABLE_CONFIDENCE,
        "high_confidence_errors": sum(
            row["relation"] != "exact"
            and int(row["confidence"]) >= ACTIONABLE_CONFIDENCE
            for row in rows
        ),
        "high_confidence_octave_errors": sum(
            bool(row["octave_error"])
            and int(row["confidence"]) >= ACTIONABLE_CONFIDENCE
            for row in rows
        ),
        "gates": gates,
    }


def _percent(value: object) -> str:
    return "—" if value is None else f"{float(value) * 100.0:.1f}%"


def _number(value: object, digits: int = 3) -> str:
    return "—" if value is None else f"{float(value):.{digits}f}"


def _integer(value: object) -> str:
    return "—" if value is None else str(int(value))


def compare_results(
    baseline: Sequence[dict[str, object]], candidate: Sequence[dict[str, object]]
) -> dict[str, object]:
    by_id = {str(row["track"]): row for row in candidate}
    if len(by_id) != len(candidate):
        raise ValueError("candidate result set contains duplicate track IDs")
    fixed: list[str] = []
    broken: list[str] = []
    changed: list[str] = []
    for before in baseline:
        track_id = str(before["track"])
        after = by_id.get(track_id)
        if after is None:
            raise ValueError(f"candidate result set lacks {track_id}")
        if before["reported"] != after["reported"]:
            changed.append(track_id)
        before_exact = before["relation"] == "exact"
        after_exact = after["relation"] == "exact"
        if not before_exact and after_exact:
            fixed.append(track_id)
        elif before_exact and not after_exact:
            broken.append(track_id)
    if len(by_id) != len(baseline):
        raise ValueError("result sets have different track IDs")
    return {
        "changed_selection_count": len(changed),
        "fixed_count": len(fixed),
        "broken_count": len(broken),
        "net_exact_gain": len(fixed) - len(broken),
        "changed_track_ids": changed,
        "fixed_track_ids": fixed,
        "broken_track_ids": broken,
    }


def render_report(
    summaries: dict[str, dict[str, object]],
    comparisons: dict[str, dict[str, object]],
    manifest: dict,
) -> str:
    endorsement = comparisons["S4 to S4 + S6 endorsement"]
    b1_failures = {
        name: summary["high_confidence_octave_errors"]
        for name, summary in summaries.items()
    }
    lines = [
        "# Rekordbox independent tempo-corpus report",
        "",
        f"- Corpus format: `{manifest['format']}`",
        f"- Decoded tracks: {manifest['decoded_track_count']}",
        f"- Excluded during import: {manifest['excluded_count']}",
        (
            "- Window: "
            f"{manifest['window']['start_seconds']} s + "
            f"{manifest['window']['duration_seconds']} s, "
            f"{manifest['window']['sample_rate']} Hz, "
            f"{manifest['window']['channels']} channels"
        ),
        "- Ground truth: modal tempo from Rekordbox `PQTZ`; audio path from `PPTH`",
        "",
        "## Independent-validation outcome",
        "",
        (
            f"S6 endorsement changed {endorsement['changed_selection_count']} selections, "
            f"fixed {endorsement['fixed_count']} S4 misses and broke "
            f"{endorsement['broken_count']} S4-correct tracks, for a net gain of "
            f"{endorsement['net_exact_gain']}. The earlier `fixed, none broken` result "
            "therefore does not generalize unchanged to this corpus."
        ),
        "",
        (
            f"At the documented actionable confidence threshold of "
            f"{ACTIONABLE_CONFIDENCE}, high-confidence octave errors are: "
            + ", ".join(f"{name}={count}" for name, count in b1_failures.items())
            + ". B1's zero-error requirement does not hold on this independent corpus."
        ),
        "",
        "## Accuracy",
        "",
        "| Mode | Tracks | Within 1% | Within 0.1% | Octave | Other | Median error (correct) |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for name, summary in summaries.items():
        lines.append(
            f"| {name} | {summary['tracks']} | "
            f"{summary['within_1_percent']} ({_percent(summary['within_1_percent_rate'])}) | "
            f"{summary['within_0_1_percent']} | {summary['octave_family_errors']} | "
            f"{summary['other_errors']} | "
            f"{_number(summary['median_error_percent_among_correct'])}% |"
        )
    lines.extend(
        [
            "",
            "## Confidence and actionable errors",
            "",
            "| Mode | Correct mean | Incorrect mean | Incorrect max | Correct min | Errors >=75 | Octave errors >=75 |",
            "|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for name, summary in summaries.items():
        lines.append(
            f"| {name} | {_number(summary['correct_confidence_mean'], 1)} | "
            f"{_number(summary['incorrect_confidence_mean'], 1)} | "
            f"{_integer(summary['incorrect_confidence_max'])} | "
            f"{_integer(summary['correct_confidence_min'])} | "
            f"{summary['high_confidence_errors']} | "
            f"{summary['high_confidence_octave_errors']} |"
        )
    lines.extend(
        [
            "",
            "## Threshold sweep",
            "",
            "Each cell is `correct/wrong admitted`.",
            "",
            "| Gate | " + " | ".join(summaries) + " |",
            "|---:|" + "---:|" * len(summaries),
        ]
    )
    for gate in GATES:
        cells = []
        for summary in summaries.values():
            point = summary["gates"][str(gate)]
            cells.append(f"{point['correct']}/{point['wrong']}")
        lines.append(f"| {gate} | " + " | ".join(cells) + " |")
    lines.extend(
        [
            "",
            "Audio, titles and private source paths are intentionally absent from this report.",
            "",
        ]
    )
    return "\n".join(lines)


def write_reports(prepared: Path, manifest: dict) -> None:
    rows_by_mode: dict[str, list[dict[str, object]]] = {}
    summaries: dict[str, dict[str, object]] = {}
    for name, (stem, _mode_args) in MODES.items():
        csv_path = prepared / f"results-{stem}.csv"
        if not csv_path.is_file():
            raise SystemExit(f"result CSV not found: {csv_path}")
        rows = _read_results(csv_path)
        if len(rows) != manifest["decoded_track_count"]:
            raise SystemExit(
                f"{name} emitted {len(rows)} rows for "
                f"{manifest['decoded_track_count']} decoded tracks"
            )
        rows_by_mode[name] = rows
        summaries[name] = summarize_results(rows)

    comparisons = {
        "S4 to S4 + S6 endorsement": compare_results(
            rows_by_mode["S4"], rows_by_mode["S4 + S6 endorsement"]
        )
    }
    report = {
        "format": "apta-rekordbox-tempo-report-0.1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "corpus": manifest,
        "modes": summaries,
        "comparisons": comparisons,
        "results": rows_by_mode,
    }
    (prepared / "report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    (prepared / "REPORT.md").write_text(
        render_report(summaries, comparisons, manifest), encoding="utf-8"
    )


def run_corpus(args: argparse.Namespace) -> int:
    prepared = Path(args.prepared).resolve()
    manifest_path = prepared / "corpus.json"
    tracks_path = prepared / "tracks.txt"
    if not manifest_path.is_file() or not tracks_path.is_file():
        raise SystemExit("prepared directory lacks corpus.json or tracks.txt")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    corpus_exe = Path(args.corpus_exe).resolve()
    if not corpus_exe.is_file():
        raise SystemExit(f"corpus executable not found: {corpus_exe}")

    for index, (name, (stem, mode_args)) in enumerate(MODES.items(), start=1):
        csv_path = prepared / f"results-{stem}.csv"
        text_path = prepared / f"results-{stem}.txt"
        command = [
            str(corpus_exe),
            "--tracks",
            str(tracks_path),
            "--verbose",
            "--results-csv",
            str(csv_path),
            *mode_args,
        ]
        print(f"run {index}/{len(MODES)}: {name}", flush=True)
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        text_path.write_text(
            completed.stdout
            + ("\n--- stderr ---\n" + completed.stderr if completed.stderr else ""),
            encoding="utf-8",
        )
        if completed.returncode != 0:
            raise SystemExit(
                f"{name} corpus run failed ({completed.returncode}); see {text_path}"
            )
    write_reports(prepared, manifest)
    print(f"report: {prepared / 'REPORT.md'}")
    return 0


def report_existing(args: argparse.Namespace) -> int:
    prepared = Path(args.prepared).resolve()
    manifest_path = prepared / "corpus.json"
    if not manifest_path.is_file():
        raise SystemExit("prepared directory lacks corpus.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    write_reports(prepared, manifest)
    print(f"report: {prepared / 'REPORT.md'}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    prepare_parser = subparsers.add_parser(
        "prepare", help="scan ANLZ files and decode bounded WAV windows"
    )
    prepare_parser.add_argument("--device-root", required=True)
    prepare_parser.add_argument("--output-dir", required=True)
    prepare_parser.add_argument("--ffmpeg", default="ffmpeg")
    prepare_parser.add_argument("--start", type=int, default=DEFAULT_START_SECONDS)
    prepare_parser.add_argument(
        "--duration", type=int, default=DEFAULT_DURATION_SECONDS
    )
    prepare_parser.add_argument("--sample-rate", type=int, default=DEFAULT_SAMPLE_RATE)
    prepare_parser.add_argument("--channels", type=int, default=DEFAULT_CHANNELS)
    prepare_parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    prepare_parser.add_argument("--limit", type=int)
    prepare_parser.add_argument("--overwrite", action="store_true")
    prepare_parser.add_argument("--strict", action="store_true")
    prepare_parser.set_defaults(func=prepare)

    run_parser = subparsers.add_parser(
        "run", help="run S4, endorsed S4 and S6 and write comparison reports"
    )
    run_parser.add_argument("--prepared", required=True)
    run_parser.add_argument("--corpus-exe", required=True)
    run_parser.set_defaults(func=run_corpus)

    report_parser = subparsers.add_parser(
        "report", help="regenerate reports from existing result CSV files"
    )
    report_parser.add_argument("--prepared", required=True)
    report_parser.set_defaults(func=report_existing)

    split_parser = subparsers.add_parser(
        "split", help="freeze deterministic development and hold-out partitions"
    )
    split_parser.add_argument("--prepared", required=True)
    split_parser.add_argument("--seed", default=DEFAULT_SPLIT_SEED)
    split_parser.add_argument(
        "--holdout-fraction", type=float, default=DEFAULT_HOLDOUT_FRACTION
    )
    split_parser.add_argument("--output", default="split.json")
    split_parser.set_defaults(func=split_corpus)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    numeric = ("start", "duration", "sample_rate", "channels", "jobs")
    for name in numeric:
        if hasattr(args, name) and getattr(args, name) <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if getattr(args, "limit", None) is not None and args.limit <= 0:
        parser.error("--limit must be positive")
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
