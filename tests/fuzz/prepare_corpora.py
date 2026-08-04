#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Materialize deterministic, versioned P7 libFuzzer corpora."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path
from typing import Any


PARSER_CORPORA = (
    "container-parser",
    "header-directory",
    "meta-reader",
    "wovr-wdtl-readers",
    "temp-lgrd-readers",
    "ggrd-revn-readers",
    "serializer-roundtrip",
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_hex(path: Path) -> bytes:
    return bytes.fromhex("".join(path.read_text(encoding="ascii").split()))


def write_seed(directory: Path, label: str, data: bytes) -> dict[str, Any]:
    digest = sha256_bytes(data)
    path = directory / f"{label}-{digest[:16]}"
    path.write_bytes(data)
    return {
        "path": path.name,
        "sha256": digest,
        "size_bytes": len(data),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    args = parser.parse_args()

    policy = json.loads(args.policy.read_text(encoding="utf-8"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    target_ids = [item["id"] for item in policy["targets"]]

    if len(target_ids) != len(set(target_ids)):
        raise SystemExit("duplicate fuzz target id")
    if set(PARSER_CORPORA) - set(target_ids):
        raise SystemExit("campaign policy omits a parser corpus")
    if {"pcm-validation", "state-transitions"} - set(target_ids):
        raise SystemExit("campaign policy omits a state/input corpus")

    if args.output.exists():
        shutil.rmtree(args.output)
    args.output.mkdir(parents=True)

    directories = {}
    for target_id in target_ids:
        directory = args.output / target_id
        directory.mkdir()
        directories[target_id] = directory

    parser_seeds: list[tuple[str, bytes]] = []
    for item in manifest["container_seed_sources"]:
        source = item["path"]
        if item["kind"] == "generated":
            path = args.generated / source
            data = path.read_bytes()
        elif item["kind"] == "hex":
            path = args.source_root / source
            data = load_hex(path)
        else:
            raise SystemExit(f"unsupported seed kind: {item['kind']}")
        if not data:
            raise SystemExit(f"empty seed: {source}")
        parser_seeds.append((Path(source).stem, data))

    report_targets: dict[str, list[dict[str, Any]]] = {}
    for target_id in PARSER_CORPORA:
        seen: set[str] = set()
        records = []
        for label, data in parser_seeds:
            digest = sha256_bytes(data)
            if digest in seen:
                continue
            seen.add(digest)
            records.append(write_seed(directories[target_id], label, data))
        report_targets[target_id] = records

    for target_id in ("pcm-validation", "state-transitions"):
        records = []
        for index, value in enumerate(
            manifest["synthetic_seeds"][target_id], start=1
        ):
            records.append(
                write_seed(
                    directories[target_id],
                    f"seed-{index:02d}",
                    bytes.fromhex(value),
                )
            )
        report_targets[target_id] = records

    report = {
        "schema": "apta-materialized-fuzz-corpus-1",
        "corpus_version": manifest["corpus_version"],
        "source_revision": args.source_revision,
        "policy_sha256": sha256_bytes(args.policy.read_bytes()),
        "manifest_sha256": sha256_bytes(args.manifest.read_bytes()),
        "targets": report_targets,
    }
    report_path = args.output / "corpus-report.json"
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"materialized {len(report_targets)} corpora at {args.output}; "
        f"report={report_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
