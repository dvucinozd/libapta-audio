#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare and score an ASAP-based APTA 1.1 meter/downbeat validation set.

This is a targeted development corpus, not the final DJ acceptance corpus.
ASAP supplies performance MIDI plus independently authored beat, downbeat and
time-signature annotations.  The optional prepare command renders anonymous
30-second WAV excerpts with a deterministic built-in synthesizer.  Source MIDI,
rendered audio, labels and reports are local-only CC BY-NC-SA derivatives.

The deterministic split is grouped by musical score so alternate performances
of one work cannot cross from development into the untouched holdout.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.metadata
import json
import math
import shutil
import subprocess
import sys
import wave
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from statistics import median
from typing import Any, Iterable

FORMAT = "apta-1.1-asap-meter-validation-1"
REPORT_FORMAT = "apta-1.1-asap-meter-report-1"
ASAP_REVISION = "afc815c75c42e83a79c03feb6da8a35e77d4c6b8"
SELECTION_SEED = "apta-1.1-asap-meter-validation-v1"
SAMPLE_RATE = 48_000
CHANNELS = 2
SAMPLE_WIDTH = 2
WINDOW_SECONDS = 30.0
ANCHOR_SECONDS = 2.0
PER_METER_PER_SPLIT = 20
SUPPORTED_METERS = ("3/4", "4/4")
DOWNBEAT_TOLERANCE_BEATS = 0.10
HIGH_CONFIDENCE = 75
MIDO_VERSION = "1.3.3"
NUMPY_VERSION = "2.3.5"


class ValidationError(ValueError):
    pass


@dataclass(frozen=True)
class Candidate:
    source_path: str
    score_id: str
    meter: str
    window_start: float
    beats: tuple[float, ...]
    downbeats: tuple[float, ...]


@dataclass(frozen=True)
class Note:
    start: float
    end: float
    pitch: int
    velocity: int


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _stable_key(kind: str, value: str) -> str:
    return hashlib.sha256(f"{SELECTION_SEED}:{kind}:{value}".encode()).hexdigest()


def _utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _require_source_revision(root: Path) -> None:
    try:
        completed = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValidationError("ASAP source must be a Git checkout at tag v1.2") from exc
    revision = completed.stdout.strip().casefold()
    if revision != ASAP_REVISION:
        raise ValidationError(
            f"ASAP revision must be {ASAP_REVISION}, found {revision or '<empty>'}"
        )


def load_annotations(root: Path) -> dict[str, dict[str, Any]]:
    path = root / "asap_annotations.json"
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError(f"cannot read ASAP annotations: {exc}") from exc
    if not isinstance(value, dict) or not value:
        raise ValidationError("ASAP annotations must be a non-empty object")
    return value


def _single_supported_meter(row: dict[str, Any]) -> str | None:
    signatures = row.get("perf_time_signatures")
    if not isinstance(signatures, dict) or not signatures:
        return None
    meters: set[str] = set()
    for value in signatures.values():
        if not isinstance(value, list) or not value or not isinstance(value[0], str):
            return None
        meters.add(value[0])
    if len(meters) != 1:
        return None
    meter = next(iter(meters))
    return meter if meter in SUPPORTED_METERS else None


def _float_times(value: Any) -> tuple[float, ...]:
    if not isinstance(value, list):
        return ()
    try:
        times = tuple(float(item) for item in value)
    except (TypeError, ValueError):
        return ()
    if any(not math.isfinite(item) or item < 0.0 for item in times):
        return ()
    if any(right <= left for left, right in zip(times, times[1:])):
        return ()
    return times


def select_candidates(
    annotations: dict[str, dict[str, Any]],
    per_meter_per_split: int = PER_METER_PER_SPLIT,
) -> dict[str, list[Candidate]]:
    """Select deterministic development/holdout rows grouped by score."""
    by_meter_score: dict[str, dict[str, list[Candidate]]] = {
        meter: {} for meter in SUPPORTED_METERS
    }
    for source_path, row in annotations.items():
        if not isinstance(source_path, str) or not isinstance(row, dict):
            continue
        if row.get("score_and_performance_aligned") is not True:
            continue
        meter = _single_supported_meter(row)
        beats = _float_times(row.get("performance_beats"))
        downbeats = _float_times(row.get("performance_downbeats"))
        if meter is None or len(beats) < 2 or not downbeats:
            continue
        eligible = [
            value
            for value in downbeats
            if value >= ANCHOR_SECONDS
            and value + (WINDOW_SECONDS - ANCHOR_SECONDS) <= beats[-1]
        ]
        if not eligible:
            continue
        chosen_downbeat = min(
            eligible,
            key=lambda value: _stable_key("window", f"{source_path}:{value:.9f}"),
        )
        score_id = str(PurePosixPath(source_path).parent)
        candidate = Candidate(
            source_path=source_path,
            score_id=score_id,
            meter=meter,
            window_start=chosen_downbeat - ANCHOR_SECONDS,
            beats=beats,
            downbeats=downbeats,
        )
        by_meter_score[meter].setdefault(score_id, []).append(candidate)

    selected: dict[str, list[Candidate]] = {"development": [], "holdout": []}
    required_scores = per_meter_per_split * 2
    for meter in SUPPORTED_METERS:
        score_rows = by_meter_score[meter]
        if len(score_rows) < required_scores:
            raise ValidationError(
                f"ASAP has only {len(score_rows)} eligible {meter} scores; "
                f"{required_scores} required"
            )
        scores = sorted(score_rows, key=lambda value: _stable_key("score", value))
        for index, score_id in enumerate(scores[:required_scores]):
            performance = min(
                score_rows[score_id],
                key=lambda item: _stable_key("performance", item.source_path),
            )
            split = "development" if index < per_meter_per_split else "holdout"
            selected[split].append(performance)

    for split in selected:
        selected[split].sort(key=lambda item: (item.meter, item.score_id, item.source_path))
    development_scores = {item.score_id for item in selected["development"]}
    holdout_scores = {item.score_id for item in selected["holdout"]}
    if development_scores & holdout_scores:
        raise AssertionError("score-grouped selection leaked across splits")
    return selected


def _import_render_dependencies() -> tuple[Any, Any]:
    try:
        import mido  # type: ignore[import-not-found]
        import numpy  # type: ignore[import-not-found]
    except ImportError as exc:
        raise ValidationError(
            f"prepare requires mido {MIDO_VERSION} and numpy {NUMPY_VERSION}"
        ) from exc
    try:
        mido_version = importlib.metadata.version("mido")
    except importlib.metadata.PackageNotFoundError as exc:
        raise ValidationError("cannot determine the installed mido version") from exc
    if mido_version != MIDO_VERSION or numpy.__version__ != NUMPY_VERSION:
        raise ValidationError(
            f"prepare requires mido {MIDO_VERSION} and numpy {NUMPY_VERSION}; "
            f"found mido {mido_version} and numpy {numpy.__version__}"
        )
    return mido, numpy


def read_midi_notes(path: Path) -> list[Note]:
    mido, _numpy = _import_render_dependencies()
    try:
        midi = mido.MidiFile(path)
    except (OSError, ValueError, EOFError) as exc:
        raise ValidationError(f"cannot parse MIDI {path}: {exc}") from exc
    tempo = 500_000
    current = 0.0
    active: dict[tuple[int, int], list[tuple[float, int]]] = {}
    notes: list[Note] = []
    for message in mido.merge_tracks(midi.tracks):
        current += mido.tick2second(message.time, midi.ticks_per_beat, tempo)
        if message.type == "set_tempo":
            tempo = int(message.tempo)
            continue
        if message.type == "note_on" and int(message.velocity) > 0:
            key = (int(message.channel), int(message.note))
            active.setdefault(key, []).append((current, int(message.velocity)))
            continue
        if message.type not in {"note_off", "note_on"}:
            continue
        key = (int(message.channel), int(message.note))
        pending = active.get(key)
        if not pending:
            continue
        start, velocity = pending.pop(0)
        if not pending:
            active.pop(key, None)
        notes.append(Note(start, max(current, start + 0.01), key[1], velocity))
    for (_channel, pitch), pending in active.items():
        for start, velocity in pending:
            notes.append(Note(start, start + 0.25, pitch, velocity))
    notes.sort(key=lambda item: (item.start, item.pitch, item.end))
    return notes


def render_excerpt(notes: Iterable[Note], start: float, output: Path) -> None:
    _mido, np = _import_render_dependencies()
    frame_count = int(round(WINDOW_SECONDS * SAMPLE_RATE))
    audio = np.zeros(frame_count, dtype=np.float32)
    table_size = 4096
    theta = np.arange(table_size, dtype=np.float32) * (2.0 * np.pi / table_size)
    wavetable = (
        np.sin(theta)
        + 0.30 * np.sin(2.0 * theta)
        + 0.14 * np.sin(3.0 * theta)
        + 0.06 * np.sin(4.0 * theta)
    ).astype(np.float32)
    wavetable /= float(np.max(np.abs(wavetable)))
    maximum_note_frames = int(4.0 * SAMPLE_RATE)
    decay = np.exp(-np.arange(maximum_note_frames, dtype=np.float32) / (1.35 * SAMPLE_RATE))

    end = start + WINDOW_SECONDS
    for note in notes:
        release_end = note.end + 0.35
        if note.start >= end or release_end <= start or not 0 <= note.pitch <= 127:
            continue
        first = max(0, int(math.floor((note.start - start) * SAMPLE_RATE)))
        last = min(frame_count, int(math.ceil((release_end - start) * SAMPLE_RATE)))
        if last <= first:
            continue
        global_first = start + first / SAMPLE_RATE
        note_offset = max(0, int(round((global_first - note.start) * SAMPLE_RATE)))
        count = min(last - first, maximum_note_frames - min(note_offset, maximum_note_frames))
        if count <= 0:
            continue
        indexes = np.arange(note_offset, note_offset + count, dtype=np.float32)
        frequency = 440.0 * (2.0 ** ((note.pitch - 69) / 12.0))
        phase = np.floor(indexes * (frequency * table_size / SAMPLE_RATE)).astype(np.int64)
        envelope = decay[note_offset : note_offset + count].copy()
        attack = max(1, int(0.003 * SAMPLE_RATE))
        attack_remaining = min(count, max(0, attack - note_offset))
        if attack_remaining:
            envelope[:attack_remaining] *= (
                np.arange(note_offset, note_offset + attack_remaining, dtype=np.float32) / attack
            )
        release_frame = int(round((note.end - note.start) * SAMPLE_RATE))
        release_from = max(0, release_frame - note_offset)
        if release_from < count:
            release_count = count - release_from
            envelope[release_from:] *= np.linspace(
                1.0, 0.0, release_count, endpoint=False, dtype=np.float32
            )
        amplitude = 0.075 * (note.velocity / 127.0) ** 1.5
        audio[first : first + count] += amplitude * wavetable[phase % table_size] * envelope

    peak = float(np.max(np.abs(audio)))
    if peak <= 1e-8:
        raise ValidationError("rendered excerpt is silent")
    audio = np.tanh(audio * min(8.0, 0.88 / peak))
    audio *= 0.88 / float(np.max(np.abs(audio)))
    pcm = np.rint(audio * 32767.0).astype("<i2")
    stereo = np.column_stack((pcm, pcm)).reshape(-1)
    output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output), "wb") as target:
        target.setnchannels(CHANNELS)
        target.setsampwidth(SAMPLE_WIDTH)
        target.setframerate(SAMPLE_RATE)
        target.writeframes(stereo.tobytes())


def _frames_in_window(times: Iterable[float], start: float) -> list[int]:
    end = start + WINDOW_SECONDS
    return [
        int(round((value - start) * SAMPLE_RATE))
        for value in times
        if start <= value < end
    ]


def prepare(root: Path, output: Path) -> dict[str, Any]:
    _require_source_revision(root)
    annotations = load_annotations(root)
    selected = select_candidates(annotations)
    output.mkdir(parents=True, exist_ok=True)
    audio_dir = output / "audio"
    temporary_dir = output / "rendering"
    temporary_dir.mkdir(parents=True, exist_ok=True)
    labels: list[dict[str, Any]] = []
    private_sources: list[dict[str, Any]] = []
    seen_tracks: set[str] = set()
    total = sum(len(rows) for rows in selected.values())
    completed = 0
    for split in ("development", "holdout"):
        for candidate in selected[split]:
            completed += 1
            print(f"render {completed}/{total} {split} {candidate.meter}", flush=True)
            notes = read_midi_notes(root / PurePosixPath(candidate.source_path))
            temporary = temporary_dir / f"excerpt-{completed:03d}.wav"
            render_excerpt(notes, candidate.window_start, temporary)
            audio_hash = sha256_file(temporary)
            track = "track-" + audio_hash[:24]
            if track in seen_tracks:
                raise ValidationError(f"duplicate rendered audio ID: {track}")
            seen_tracks.add(track)
            final_audio = audio_dir / f"{track}.wav"
            final_audio.parent.mkdir(parents=True, exist_ok=True)
            temporary.replace(final_audio)
            beats = _frames_in_window(candidate.beats, candidate.window_start)
            downbeats = _frames_in_window(candidate.downbeats, candidate.window_start)
            if len(beats) < 4 or len(downbeats) < 2:
                raise ValidationError(f"insufficient reference events for {candidate.source_path}")
            labels.append(
                {
                    "track": track,
                    "split": split,
                    "meter_numerator": int(candidate.meter.split("/")[0]),
                    "meter_denominator": 4,
                    "beat_frames": beats,
                    "downbeat_frames": downbeats,
                }
            )
            private_sources.append(
                {
                    "track": track,
                    "source_path": candidate.source_path,
                    "score_id": candidate.score_id,
                    "window_start_seconds": candidate.window_start,
                    "audio_sha256": audio_hash,
                }
            )
    shutil.rmtree(temporary_dir, ignore_errors=True)
    labels.sort(key=lambda row: row["track"])
    private_sources.sort(key=lambda row: row["track"])
    expected_audio = {f"{row['track']}.wav" for row in labels}
    actual_audio = {path.name for path in audio_dir.glob("*.wav")}
    if actual_audio != expected_audio:
        raise ValidationError(
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
                1
                for row in labels
                if row["split"] == split
                and row["meter_numerator"] == int(meter.split("/")[0])
            )
            for meter in SUPPORTED_METERS
        }
        for split in ("development", "holdout")
    }
    manifest = {
        "format": FORMAT,
        "source": "ASAP v1.2 (CC BY-NC-SA 4.0)",
        "source_revision": ASAP_REVISION,
        "selection_seed": SELECTION_SEED,
        "render_dependencies": {
            "mido": MIDO_VERSION,
            "numpy": NUMPY_VERSION,
        },
        "generated_utc": _utc_now(),
        "sample_rate": SAMPLE_RATE,
        "channels": CHANNELS,
        "sample_width_bytes": SAMPLE_WIDTH,
        "window_seconds": WINDOW_SECONDS,
        "track_count": len(labels),
        "split_counts": counts,
        "track_ids": [row["track"] for row in labels],
        "labels_sha256": sha256_file(labels_path),
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def load_prepared(
    prepared: Path,
    expected_format: str = FORMAT,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    try:
        manifest = json.loads((prepared / "manifest.json").read_text(encoding="utf-8"))
        labels = json.loads((prepared / "labels.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError(f"cannot read prepared corpus: {exc}") from exc
    if not isinstance(manifest, dict) or manifest.get("format") != expected_format:
        raise ValidationError(f"manifest format must be {expected_format}")
    if not isinstance(labels, list) or not labels:
        raise ValidationError("labels must be a non-empty array")
    if manifest.get("labels_sha256") != sha256_file(prepared / "labels.json"):
        raise ValidationError("labels hash does not match manifest")
    ids = [row.get("track") for row in labels if isinstance(row, dict)]
    if len(ids) != len(labels) or ids != manifest.get("track_ids"):
        raise ValidationError("labels do not exactly match manifest track IDs")
    for row in labels:
        if row.get("split") not in {"development", "holdout"}:
            raise ValidationError("label split must be development or holdout")
        if (row.get("meter_numerator"), row.get("meter_denominator")) not in {
            (3, 4),
            (4, 4),
        }:
            raise ValidationError("label meter must be 3/4 or 4/4")
        for field in ("beat_frames", "downbeat_frames"):
            values = row.get(field)
            if (
                not isinstance(values, list)
                or not values
                or any(not isinstance(value, int) or value < 0 for value in values)
                or any(right <= left for left, right in zip(values, values[1:]))
            ):
                raise ValidationError(f"{field} must be a sorted non-empty frame array")
    return manifest, labels


def run_analysis(
    prepared: Path,
    analyzer: Path,
    output: Path,
    split: str,
    source_revision: str,
    corpus_format: str = FORMAT,
) -> dict[str, Any]:
    manifest, labels = load_prepared(prepared, corpus_format)
    if not analyzer.is_file():
        raise ValidationError(f"analyzer not found: {analyzer}")
    normalized_revision = source_revision.strip().casefold()
    if len(normalized_revision) != 40 or any(
        character not in "0123456789abcdef" for character in normalized_revision
    ):
        raise ValidationError("source revision must be a full lowercase Git SHA")
    rows = [row for row in labels if row["split"] == split]
    if not rows:
        raise ValidationError(f"no {split} rows in prepared corpus")
    output.mkdir(parents=True, exist_ok=True)
    analyzed = output / "analyzed"
    analyzed.mkdir(parents=True, exist_ok=True)
    mappings: list[dict[str, str]] = []
    outputs: list[dict[str, str]] = []
    for index, row in enumerate(rows, start=1):
        track = str(row["track"])
        audio = prepared / "audio" / f"{track}.wav"
        if not audio.is_file():
            raise ValidationError(f"prepared audio not found: {audio}")
        if "track-" + sha256_file(audio)[:24] != track:
            raise ValidationError(f"prepared audio hash does not match {track}")
        target = analyzed / f"{track}.apta"
        print(f"analyze {index}/{len(rows)} {track}", flush=True)
        try:
            subprocess.run(
                [str(analyzer), str(audio), "--output", str(target), "--features", "all"],
                check=True,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            raise ValidationError(f"analysis failed for {track}: {exc}") from exc
        mappings.append({"track": track, "path": str(target.resolve())})
        outputs.append({"track": track, "apta_sha256": sha256_file(target)})
    mapping_path = output / "mapping.csv"
    with mapping_path.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=("track", "path"), lineterminator="\n")
        writer.writeheader()
        writer.writerows(mappings)
    run = {
        "format": corpus_format,
        "split": split,
        "source_revision": normalized_revision,
        "analyzer_sha256": sha256_file(analyzer),
        "manifest_sha256": sha256_file(prepared / "manifest.json"),
        "mapping_sha256": sha256_file(mapping_path),
        "track_count": len(rows),
        "complete": True,
        "outputs": outputs,
    }
    (output / "run.json").write_text(
        json.dumps(run, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return run


def _local_period(beat_frames: list[int], reference: int) -> float:
    gaps = [right - left for left, right in zip(beat_frames, beat_frames[1:])]
    if not gaps:
        raise ValidationError("at least two beat frames are required")
    indexed = sorted(
        zip(beat_frames[:-1], gaps), key=lambda item: abs(item[0] - reference)
    )
    return float(median(gap for _frame, gap in indexed[: min(5, len(indexed))]))


def score_rows(
    labels: Iterable[dict[str, Any]], results: Iterable[dict[str, Any]]
) -> dict[str, Any]:
    truth = {str(row["track"]): row for row in labels}
    observed = {str(row["track"]): row for row in results}
    if set(truth) != set(observed):
        raise ValidationError("result IDs do not exactly match selected label IDs")
    details: list[dict[str, Any]] = []
    for track in sorted(truth):
        expected = truth[track]
        result = observed[track]
        predicted = int(result["downbeat_frame"])
        nearest = min(expected["downbeat_frames"], key=lambda value: abs(value - predicted))
        period = _local_period(expected["beat_frames"], nearest)
        error_beats = abs(predicted - nearest) / period
        reported_period = float(result["beat_period_frames"])
        period_relative_error = abs(reported_period - period) / period
        meter_ok = (
            int(result["meter_numerator"]) == int(expected["meter_numerator"])
            and int(result["meter_denominator"]) == int(expected["meter_denominator"])
        )
        downbeat_ok = error_beats <= DOWNBEAT_TOLERANCE_BEATS
        details.append(
            {
                "track": track,
                "meter_numerator": int(expected["meter_numerator"]),
                "meter_correct": meter_ok,
                "downbeat_correct": downbeat_ok,
                "downbeat_error_beats": error_beats,
                "period_relative_error": period_relative_error,
                "meter_confidence": int(result["meter_confidence"]),
                "downbeat_confidence": int(result["downbeat_confidence"]),
            }
        )

    def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
        count = len(rows)
        meter_correct = sum(bool(row["meter_correct"]) for row in rows)
        downbeat_correct = sum(bool(row["downbeat_correct"]) for row in rows)
        period_one = sum(float(row["period_relative_error"]) <= 0.01 for row in rows)
        period_ten = sum(float(row["period_relative_error"]) <= 0.10 for row in rows)
        high_meter = [row for row in rows if row["meter_confidence"] >= HIGH_CONFIDENCE]
        high_downbeat = [row for row in rows if row["downbeat_confidence"] >= HIGH_CONFIDENCE]
        return {
            "track_count": count,
            "meter_correct": meter_correct,
            "meter_accuracy": meter_correct / count if count else 0.0,
            "downbeat_correct": downbeat_correct,
            "downbeat_accuracy": downbeat_correct / count if count else 0.0,
            "period_within_1_percent": period_one,
            "period_accuracy_1_percent": period_one / count if count else 0.0,
            "period_within_10_percent": period_ten,
            "period_accuracy_10_percent": period_ten / count if count else 0.0,
            "median_period_relative_error": median(
                [float(row["period_relative_error"]) for row in rows]
            )
            if rows
            else None,
            "median_downbeat_error_beats": median(
                [float(row["downbeat_error_beats"]) for row in rows]
            )
            if rows
            else None,
            "high_confidence_meter_count": len(high_meter),
            "high_confidence_meter_errors": sum(
                not bool(row["meter_correct"]) for row in high_meter
            ),
            "high_confidence_downbeat_count": len(high_downbeat),
            "high_confidence_downbeat_errors": sum(
                not bool(row["downbeat_correct"]) for row in high_downbeat
            ),
        }

    return {
        "overall": summarize(details),
        "by_meter": {
            meter: summarize(
                [row for row in details if row["meter_numerator"] == int(meter[0])]
            )
            for meter in SUPPORTED_METERS
        },
        "tracks": details,
    }


def evaluate(
    prepared: Path,
    split: str,
    inspector: Path,
    mapping: Path,
    report: Path,
    corpus_format: str = FORMAT,
    report_format: str = REPORT_FORMAT,
) -> dict[str, Any]:
    _manifest, labels = load_prepared(prepared, corpus_format)
    selected = [row for row in labels if row["split"] == split]
    try:
        import apta_1_1_export_acceptance_results as exporter
    except ImportError as exc:
        raise ValidationError("cannot import the APTA acceptance result exporter") from exc
    mapped = exporter.read_mapping(mapping)
    mapped_ids = sorted(track for track, _path in mapped)
    expected_ids = sorted(str(row["track"]) for row in selected)
    if mapped_ids != expected_ids:
        raise ValidationError("mapping IDs do not exactly match the selected split")
    try:
        results = [
            exporter.parse_inspection(track, exporter.inspect_file(inspector, path))
            for track, path in mapped
        ]
    except exporter.ExportError as exc:
        raise ValidationError(f"cannot export analyzed results: {exc}") from exc
    scores = score_rows(selected, results)
    value = {
        "format": report_format,
        "split": split,
        "downbeat_tolerance_beats": DOWNBEAT_TOLERANCE_BEATS,
        "high_confidence_threshold": HIGH_CONFIDENCE,
        **scores,
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--asap-root", type=Path, required=True)
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
            value = prepare(args.asap_root, args.output)
        elif args.command == "run":
            value = run_analysis(
                args.prepared,
                args.analyzer,
                args.output,
                args.split,
                args.source_revision,
            )
        else:
            value = evaluate(
                args.prepared, args.split, args.inspector, args.mapping, args.report
            )
    except (OSError, ValidationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(value, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
