#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Analyze a frozen APTA 1.1 DJ corpus without exposing source names.

The input staging CSV may contain local source paths. Those paths are used only
locally to re-derive the opaque track IDs already frozen in the manifest. APTA
output filenames and the generated exporter mapping contain only opaque IDs.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from apta_1_1_freeze_corpus import FORMAT, opaque_id, sha256_file

MAPPING_FIELDS = ["track", "path"]


class RunnerError(ValueError):
    pass


def load_manifest(path: Path) -> list[str]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RunnerError(f"cannot read manifest: {exc}") from exc
    if not isinstance(value, dict) or value.get("format") != FORMAT:
        raise RunnerError("manifest format is not apta-1.1-dj-validation-1")
    track_ids = value.get("track_ids")
    if (
        not isinstance(track_ids, list)
        or any(not isinstance(track, str) or not track for track in track_ids)
        or track_ids != sorted(track_ids)
        or len(track_ids) != len(set(track_ids))
    ):
        raise RunnerError("manifest track_ids must be a sorted unique string array")
    if value.get("track_count") != len(track_ids):
        raise RunnerError("manifest track_count does not match track_ids")
    return track_ids


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
                    raise RunnerError(
                        f"staging:{line}: source escapes corpus root"
                    ) from exc
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


def analyzer_command(analyzer: Path, source: Path, output: Path) -> list[str]:
    prefix = [sys.executable, str(analyzer)] if analyzer.suffix.casefold() == ".py" else [str(analyzer)]
    return [
        *prefix,
        str(source),
        "--output",
        str(output),
        "--features",
        "all",
    ]


def analyze_frozen_corpus(
    corpus_root: Path,
    staging_csv: Path,
    manifest_path: Path,
    analyzer: Path,
    output_dir: Path,
    mapping_output: Path,
) -> int:
    expected_ids = load_manifest(manifest_path)
    sources = read_sources(corpus_root, staging_csv)
    source_ids = [track for track, _path in sources]
    if source_ids != expected_ids:
        missing = sorted(set(expected_ids) - set(source_ids))
        unexpected = sorted(set(source_ids) - set(expected_ids))
        raise RunnerError(
            "staging audio does not exactly match frozen manifest "
            f"(missing={missing}, unexpected={unexpected})"
        )
    if not analyzer.is_file():
        raise RunnerError(f"analyzer does not exist: {analyzer}")

    output_dir.mkdir(parents=True, exist_ok=True)
    mapping_rows: list[dict[str, str]] = []
    for track, source_path in sources:
        apta_path = (output_dir / f"{track}.apta").resolve()
        try:
            completed = subprocess.run(
                analyzer_command(analyzer, source_path, apta_path),
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
                f"analyzer failed for {track} with exit {completed.returncode}: {detail}"
            )
        if not apta_path.is_file() or apta_path.stat().st_size == 0:
            raise RunnerError(f"analyzer produced no non-empty output for {track}")
        mapping_rows.append({"track": track, "path": str(apta_path)})

    mapping_output.parent.mkdir(parents=True, exist_ok=True)
    temporary = mapping_output.with_name(mapping_output.name + ".tmp")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as target:
            writer = csv.DictWriter(
                target, fieldnames=MAPPING_FIELDS, lineterminator="\n"
            )
            writer.writeheader()
            writer.writerows(mapping_rows)
        temporary.replace(mapping_output)
    except OSError as exc:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass
        raise RunnerError(f"cannot write mapping: {exc}") from exc
    return len(mapping_rows)


def _write_staging(path: Path, sources: list[str]) -> None:
    with path.open("w", encoding="utf-8", newline="") as target:
        writer = csv.writer(target, lineterminator="\n")
        writer.writerow(["source"])
        for source in sources:
            writer.writerow([source])


def _self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="apta-1.1-runner-") as temporary:
        root = Path(temporary)
        corpus = root / "corpus"
        corpus.mkdir()
        first = corpus / "Artist - Private Title.wav"
        second = corpus / "another-private-name.wav"
        first.write_bytes(b"synthetic-audio-a")
        second.write_bytes(b"synthetic-audio-b")
        staging = root / "staging.csv"
        _write_staging(staging, [first.name, second.name])

        ids = sorted(
            [opaque_id(sha256_file(first)), opaque_id(sha256_file(second))]
        )
        manifest = root / "manifest.json"
        manifest.write_text(
            json.dumps(
                {"format": FORMAT, "track_count": len(ids), "track_ids": ids},
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )

        fake = root / "fake_analyzer.py"
        fake.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib, sys\n"
            "args = sys.argv[1:]\n"
            "assert args[-2:] == ['--features', 'all']\n"
            "out = pathlib.Path(args[args.index('--output') + 1])\n"
            "out.write_bytes(b'APTA-test')\n",
            encoding="utf-8",
        )
        os.chmod(fake, 0o755)

        output_dir = root / "analyzed"
        mapping = root / "mapping.csv"
        count = analyze_frozen_corpus(
            corpus, staging, manifest, fake, output_dir, mapping
        )
        if count != 2:
            raise RunnerError("self-test: wrong analyzed count")
        mapping_text = mapping.read_text(encoding="utf-8")
        if first.name in mapping_text or second.name in mapping_text:
            raise RunnerError("self-test: private source name leaked into mapping")
        with mapping.open("r", encoding="utf-8", newline="") as source:
            rows = list(csv.DictReader(source))
        if [row["track"] for row in rows] != ids:
            raise RunnerError("self-test: mapping IDs are not frozen-order IDs")
        if any(Path(row["path"]).name != row["track"] + ".apta" for row in rows):
            raise RunnerError("self-test: output path is not opaque-ID based")

        bad_manifest = root / "bad-manifest.json"
        bad_manifest.write_text(
            json.dumps(
                {"format": FORMAT, "track_count": 1, "track_ids": ids[:1]},
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        try:
            analyze_frozen_corpus(
                corpus, staging, bad_manifest, fake, output_dir, mapping
            )
        except RunnerError:
            pass
        else:
            raise RunnerError("self-test: manifest mismatch was accepted")

        traversal = root / "traversal.csv"
        _write_staging(traversal, ["../outside.wav"])
        (root / "outside.wav").write_bytes(b"outside")
        try:
            read_sources(corpus, traversal)
        except RunnerError:
            pass
        else:
            raise RunnerError("self-test: path traversal was accepted")

    print("APTA 1.1 frozen-corpus runner self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-root", type=Path)
    parser.add_argument("--staging-labels", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--analyzer", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--mapping-output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        try:
            return _self_test()
        except (OSError, RunnerError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 2
    required = (
        args.corpus_root,
        args.staging_labels,
        args.manifest,
        args.analyzer,
        args.output_dir,
        args.mapping_output,
    )
    if any(value is None for value in required):
        parser.error(
            "--corpus-root, --staging-labels, --manifest, --analyzer, "
            "--output-dir and --mapping-output are required"
        )
    try:
        count = analyze_frozen_corpus(
            args.corpus_root,
            args.staging_labels,
            args.manifest,
            args.analyzer,
            args.output_dir,
            args.mapping_output,
        )
    except (OSError, RunnerError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(f"analyzed {count} frozen APTA 1.1 corpus tracks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
