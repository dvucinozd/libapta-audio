#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Analyze a frozen APTA 1.1 DJ corpus without exposing private source paths.

The local staging CSV may contain real filenames/paths. Each source file is
re-hashed and must reproduce the opaque ID frozen in the corpus manifest.
Before invoking apta-analyze, the WAV is copied to a temporary working
directory and presented to the analyzer only as <opaque-track-id>.wav. This
prevents apta-analyze metadata from recording the original source filename or
host path.

Publishable outputs contain only opaque IDs, relative .apta paths and hashes.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from apta_1_1_freeze_corpus import FORMAT, opaque_id, sha256_file

RUN_FORMAT = "apta-1.1-dj-corpus-run-1"
FEATURE_SET = "all"
MAPPING_FIELDS = ["track", "path"]


class RunnerError(ValueError):
    pass


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    try:
        temporary.write_text(text, encoding="utf-8")
        temporary.replace(path)
    except OSError:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def _atomic_write_json(path: Path, value: dict[str, Any]) -> None:
    _atomic_write_text(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RunnerError(f"cannot read manifest: {exc}") from exc
    if not isinstance(value, dict) or value.get("format") != FORMAT:
        raise RunnerError(f"manifest format must be {FORMAT}")
    track_ids = value.get("track_ids")
    if (
        not isinstance(track_ids, list)
        or not track_ids
        or any(not isinstance(track, str) or not track for track in track_ids)
        or track_ids != sorted(track_ids)
        or len(track_ids) != len(set(track_ids))
    ):
        raise RunnerError("manifest track_ids must be a non-empty sorted unique string array")
    if value.get("track_count") != len(track_ids):
        raise RunnerError("manifest track_count does not match track_ids")
    return value


def read_sources(corpus_root: Path, staging_csv: Path) -> list[tuple[str, Path]]:
    root = corpus_root.resolve()
    rows: list[tuple[str, Path]] = []
    seen_ids: set[str] = set()
    try:
        with staging_csv.open("r", encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source)
            if "source" not in (reader.fieldnames or []):
                raise RunnerError("staging CSV requires a source column")
            for line, raw in enumerate(reader, start=2):
                source_name = (raw.get("source") or "").strip()
                if not source_name:
                    raise RunnerError(f"staging:{line}: empty source")
                source_path = (root / source_name).resolve()
                try:
                    source_path.relative_to(root)
                except ValueError as exc:
                    raise RunnerError(f"staging:{line}: source escapes corpus root") from exc
                if not source_path.is_file():
                    raise RunnerError(
                        f"staging:{line}: missing source file {source_name!r}"
                    )
                track = opaque_id(sha256_file(source_path))
                if track in seen_ids:
                    raise RunnerError(
                        f"staging:{line}: duplicate audio content/opaque ID"
                    )
                seen_ids.add(track)
                rows.append((track, source_path))
    except OSError as exc:
        raise RunnerError(f"cannot read staging CSV: {exc}") from exc
    rows.sort(key=lambda item: item[0])
    return rows


def _validate_source_revision(value: str) -> str:
    revision = value.strip().casefold()
    if len(revision) != 40 or any(ch not in "0123456789abcdef" for ch in revision):
        raise RunnerError("source revision must be a full 40-character lowercase Git SHA")
    return revision


def _relative_output_path(path: Path, publish_root: Path) -> str:
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(publish_root)
    except ValueError as exc:
        raise RunnerError(
            "publishable output must be inside the runner working directory"
        ) from exc
    return relative.as_posix()


def analyzer_command(analyzer: Path, anonymous_source_name: str, output: Path) -> list[str]:
    prefix = (
        [sys.executable, str(analyzer)]
        if analyzer.suffix.casefold() == ".py"
        else [str(analyzer)]
    )
    return [
        *prefix,
        anonymous_source_name,
        "--output",
        str(output),
        "--features",
        FEATURE_SET,
    ]


def _base_run_metadata(
    *,
    source_revision: str,
    analyzer_sha256: str,
    manifest_sha256: str,
    track_count: int,
) -> dict[str, Any]:
    return {
        "format": RUN_FORMAT,
        "source_revision": source_revision,
        "analyzer_sha256": analyzer_sha256,
        "manifest_sha256": manifest_sha256,
        "features": FEATURE_SET,
        "track_count": track_count,
        "completed_track_count": 0,
        "complete": False,
        "outputs": [],
    }


def _load_resume_state(
    path: Path,
    expected_header: dict[str, Any],
) -> dict[str, str]:
    if not path.is_file():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    if not isinstance(value, dict):
        return {}
    for field in (
        "format",
        "source_revision",
        "analyzer_sha256",
        "manifest_sha256",
        "features",
        "track_count",
    ):
        if value.get(field) != expected_header.get(field):
            return {}
    outputs = value.get("outputs")
    if not isinstance(outputs, list):
        return {}
    reusable: dict[str, str] = {}
    for row in outputs:
        if not isinstance(row, dict):
            return {}
        track = row.get("track")
        digest = row.get("apta_sha256")
        if (
            not isinstance(track, str)
            or not isinstance(digest, str)
            or len(digest) != 64
            or any(ch not in "0123456789abcdef" for ch in digest)
        ):
            return {}
        reusable[track] = digest
    return reusable


def _write_mapping(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as target:
            writer = csv.DictWriter(
                target, fieldnames=MAPPING_FIELDS, lineterminator="\n"
            )
            writer.writeheader()
            writer.writerows(rows)
        temporary.replace(path)
    except OSError as exc:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise RunnerError(f"cannot write mapping: {exc}") from exc


def analyze_frozen_corpus(
    *,
    corpus_root: Path,
    staging_csv: Path,
    manifest_path: Path,
    analyzer: Path,
    output_dir: Path,
    mapping_output: Path,
    run_metadata_output: Path,
    source_revision: str,
    resume: bool = False,
) -> int:
    source_revision = _validate_source_revision(source_revision)
    manifest = load_manifest(manifest_path)
    expected_ids = list(manifest["track_ids"])
    sources = read_sources(corpus_root, staging_csv)
    source_ids = [track for track, _path in sources]
    if source_ids != expected_ids:
        missing = sorted(set(expected_ids) - set(source_ids))
        unexpected = sorted(set(source_ids) - set(expected_ids))
        raise RunnerError(
            "staging audio does not exactly match frozen manifest "
            f"(missing={missing}, unexpected={unexpected})"
        )

    analyzer = analyzer.resolve()
    if not analyzer.is_file():
        raise RunnerError(f"analyzer does not exist: {analyzer}")
    manifest_path = manifest_path.resolve()
    output_dir = output_dir.resolve()
    mapping_output = mapping_output.resolve()
    run_metadata_output = run_metadata_output.resolve()

    # Keep all publishable artifacts below the runner working directory so the
    # mapping can use stable relative paths and never expose a host absolute path.
    publish_root = Path.cwd().resolve()
    for target in (output_dir, mapping_output.parent, run_metadata_output.parent):
        try:
            target.relative_to(publish_root)
        except ValueError as exc:
            raise RunnerError(
                "output-dir, mapping-output and run-metadata-output must be "
                "inside the runner working directory"
            ) from exc
    output_dir.mkdir(parents=True, exist_ok=True)

    analyzer_hash = _sha256(analyzer)
    manifest_hash = _sha256(manifest_path)
    state = _base_run_metadata(
        source_revision=source_revision,
        analyzer_sha256=analyzer_hash,
        manifest_sha256=manifest_hash,
        track_count=len(expected_ids),
    )
    reusable = _load_resume_state(run_metadata_output, state) if resume else {}

    mapping_rows: list[dict[str, str]] = []
    output_rows: list[dict[str, str]] = []
    for track, source_path in sources:
        apta_path = output_dir / f"{track}.apta"
        reused = False
        if track in reusable and apta_path.is_file():
            try:
                reused = _sha256(apta_path) == reusable[track]
            except OSError:
                reused = False

        if not reused:
            try:
                with tempfile.TemporaryDirectory(prefix="apta-1.1-corpus-") as temp_name:
                    temp_root = Path(temp_name)
                    anonymous_name = f"{track}.wav"
                    anonymous_source = temp_root / anonymous_name
                    shutil.copyfile(source_path, anonymous_source)
                    completed = subprocess.run(
                        analyzer_command(analyzer, anonymous_name, apta_path),
                        cwd=temp_root,
                        check=False,
                        text=True,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                    )
            except OSError as exc:
                raise RunnerError(f"cannot execute analyzer for {track}: {exc}") from exc
            if completed.returncode != 0:
                detail = completed.stderr.strip() or completed.stdout.strip()
                raise RunnerError(
                    f"analyzer failed for {track} with exit "
                    f"{completed.returncode}: {detail}"
                )
            if not apta_path.is_file() or apta_path.stat().st_size == 0:
                raise RunnerError(
                    f"analyzer produced no non-empty output for {track}"
                )

        apta_hash = _sha256(apta_path)
        relative = _relative_output_path(apta_path, publish_root)
        mapping_rows.append({"track": track, "path": relative})
        output_rows.append({"track": track, "apta_sha256": apta_hash})
        state["completed_track_count"] = len(output_rows)
        state["outputs"] = output_rows
        _atomic_write_json(run_metadata_output, state)

    _write_mapping(mapping_output, mapping_rows)
    state["mapping_sha256"] = _sha256(mapping_output)
    state["complete"] = True
    _atomic_write_json(run_metadata_output, state)
    return len(mapping_rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-root", type=Path, required=True)
    parser.add_argument("--staging-labels", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--analyzer", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--mapping-output", type=Path, required=True)
    parser.add_argument("--run-metadata-output", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    try:
        count = analyze_frozen_corpus(
            corpus_root=args.corpus_root,
            staging_csv=args.staging_labels,
            manifest_path=args.manifest,
            analyzer=args.analyzer,
            output_dir=args.output_dir,
            mapping_output=args.mapping_output,
            run_metadata_output=args.run_metadata_output,
            source_revision=args.source_revision,
            resume=args.resume,
        )
    except (OSError, RunnerError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(f"analyzed {count} frozen APTA 1.1 corpus tracks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
