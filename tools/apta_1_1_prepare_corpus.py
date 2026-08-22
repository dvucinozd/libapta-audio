#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare local MP3/FLAC/WAV sources as canonical APTA 1.1 qualification WAVs.

The output corpus is deliberately simple and deterministic at the format level:
48 kHz, stereo, signed 16-bit little-endian PCM WAV with source metadata removed.
The canonical WAV, not the compressed source, is what must be labelled, frozen,
hashed and analyzed for APTA 1.1 acceptance.

The generated preparation manifest is LOCAL-ONLY because it contains original
source paths. Do not commit it with qualification evidence.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
import wave
from pathlib import Path

SUPPORTED_SUFFIXES = {".wav", ".flac", ".mp3"}
CANONICAL_SAMPLE_RATE = 48000
CANONICAL_CHANNELS = 2
CANONICAL_SAMPLE_WIDTH = 2
MANIFEST_FORMAT = "apta-1.1-corpus-preparation-1"


class PreparationError(ValueError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def discover_sources(root: Path) -> list[Path]:
    resolved = root.resolve()
    if not resolved.is_dir():
        raise PreparationError(f"source root is not a directory: {root}")
    files = [
        path
        for path in resolved.rglob("*")
        if path.is_file() and path.suffix.casefold() in SUPPORTED_SUFFIXES
    ]
    files.sort(key=lambda path: path.relative_to(resolved).as_posix().casefold())
    if not files:
        raise PreparationError("no .wav, .flac or .mp3 sources found")
    return files


def canonical_relative_path(source_root: Path, source: Path) -> Path:
    relative = source.resolve().relative_to(source_root.resolve())
    return relative.with_suffix(".wav")


def ffmpeg_command(ffmpeg: Path, source: Path, output: Path) -> list[str]:
    return [
        str(ffmpeg),
        "-hide_banner",
        "-loglevel",
        "error",
        "-nostdin",
        "-y",
        "-i",
        str(source),
        "-map",
        "0:a:0",
        "-vn",
        "-sn",
        "-dn",
        "-map_metadata",
        "-1",
        "-fflags",
        "+bitexact",
        "-flags:a",
        "+bitexact",
        "-ar",
        str(CANONICAL_SAMPLE_RATE),
        "-ac",
        str(CANONICAL_CHANNELS),
        "-c:a",
        "pcm_s16le",
        str(output),
    ]


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
        detail = getattr(exc, "stderr", "") or str(exc)
        raise PreparationError(f"cannot execute ffmpeg: {detail.strip()}") from exc
    first = completed.stdout.splitlines()[0].strip() if completed.stdout else ""
    if not first:
        raise PreparationError("ffmpeg -version returned no version line")
    return first


def validate_canonical_wav(path: Path) -> tuple[int, int]:
    try:
        with wave.open(str(path), "rb") as wav:
            if wav.getframerate() != CANONICAL_SAMPLE_RATE:
                raise PreparationError(
                    f"canonical WAV has sample rate {wav.getframerate()}, expected {CANONICAL_SAMPLE_RATE}: {path}"
                )
            if wav.getnchannels() != CANONICAL_CHANNELS:
                raise PreparationError(
                    f"canonical WAV has {wav.getnchannels()} channels, expected {CANONICAL_CHANNELS}: {path}"
                )
            if wav.getsampwidth() != CANONICAL_SAMPLE_WIDTH or wav.getcomptype() != "NONE":
                raise PreparationError(f"canonical WAV is not stereo PCM16: {path}")
            frames = wav.getnframes()
            if frames <= 0:
                raise PreparationError(f"canonical WAV has no audio frames: {path}")
            return wav.getframerate(), frames
    except (wave.Error, OSError) as exc:
        raise PreparationError(f"cannot validate canonical WAV {path}: {exc}") from exc


def prepare(
    source_root: Path,
    output_root: Path,
    manifest_output: Path,
    ffmpeg: Path,
) -> dict[str, object]:
    sources = discover_sources(source_root)
    output_root = output_root.resolve()
    source_root = source_root.resolve()
    if output_root == source_root or source_root in output_root.parents:
        raise PreparationError("output root must not be the source root or inside it")

    destinations: dict[Path, Path] = {}
    for source in sources:
        relative = canonical_relative_path(source_root, source)
        key = Path(relative.as_posix().casefold())
        previous = destinations.get(key)
        if previous is not None:
            raise PreparationError(
                "canonical path collision after replacing extension: "
                f"{previous.relative_to(source_root)} and {source.relative_to(source_root)}"
            )
        destinations[key] = source

    version = ffmpeg_version(ffmpeg)
    rows: list[dict[str, object]] = []
    for source in sources:
        relative_source = source.relative_to(source_root)
        relative_output = canonical_relative_path(source_root, source)
        output = output_root / relative_output
        output.parent.mkdir(parents=True, exist_ok=True)
        try:
            completed = subprocess.run(
                ffmpeg_command(ffmpeg, source, output),
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        except OSError as exc:
            raise PreparationError(f"cannot run ffmpeg for {relative_source}: {exc}") from exc
        if completed.returncode != 0:
            detail = completed.stderr.strip() or completed.stdout.strip()
            raise PreparationError(
                f"ffmpeg failed for {relative_source} with exit {completed.returncode}: {detail}"
            )
        if not output.is_file() or output.stat().st_size == 0:
            raise PreparationError(f"ffmpeg produced no output for {relative_source}")
        sample_rate, frames = validate_canonical_wav(output)
        rows.append(
            {
                "source": relative_source.as_posix(),
                "source_sha256": sha256_file(source),
                "canonical": relative_output.as_posix(),
                "canonical_sha256": sha256_file(output),
                "sample_rate": sample_rate,
                "frames": frames,
            }
        )

    manifest: dict[str, object] = {
        "format": MANIFEST_FORMAT,
        "local_only": True,
        "ffmpeg_version": version,
        "canonical_audio": {
            "container": "wav",
            "codec": "pcm_s16le",
            "sample_rate": CANONICAL_SAMPLE_RATE,
            "channels": CANONICAL_CHANNELS,
        },
        "track_count": len(rows),
        "tracks": rows,
    }
    manifest_output.parent.mkdir(parents=True, exist_ok=True)
    manifest_output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--manifest-output", type=Path, required=True)
    parser.add_argument("--ffmpeg", type=Path, default=Path("ffmpeg"))
    args = parser.parse_args()
    try:
        manifest = prepare(
            args.source_root,
            args.output_root,
            args.manifest_output,
            args.ffmpeg,
        )
    except (OSError, PreparationError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(
        f"prepared {manifest['track_count']} canonical APTA 1.1 WAV track(s); "
        f"local manifest: {args.manifest_output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
