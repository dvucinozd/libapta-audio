#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Assemble deterministic P6 interchange evidence from independent components."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_hex(path: Path) -> bytes:
    return bytes.fromhex("".join(path.read_text(encoding="ascii").split()))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bridge-report", required=True, type=Path)
    parser.add_argument("--consumer-report", required=True, type=Path)
    parser.add_argument("--fixture-hex", required=True, type=Path)
    parser.add_argument("--canonical", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--producer-source", required=True, type=Path)
    parser.add_argument("--platform-system", required=True)
    parser.add_argument("--platform-processor", required=True)
    parser.add_argument("--platform-toolchain", required=True)
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--shared", required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    bridge = json.loads(args.bridge_report.read_text(encoding="utf-8"))
    consumer = json.loads(args.consumer_report.read_text(encoding="utf-8"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    fixture = load_hex(args.fixture_hex)
    canonical = args.canonical.read_bytes()
    fixture_sha = hashlib.sha256(fixture).hexdigest()
    canonical_sha = hashlib.sha256(canonical).hexdigest()

    if bridge.get("status") != "pass":
        raise SystemExit("bridge report is not pass")
    if consumer.get("status") != "pass":
        raise SystemExit("independent consumer report is not pass")
    if fixture != canonical:
        raise SystemExit("libapta canonical output differs from independent fixture")
    if fixture_sha != manifest["fixture"]["sha256"]:
        raise SystemExit("fixture hash differs from semantic manifest")

    report = {
        "artifact_schema": "apta-interchange-report-1",
        "canonical_byte_identity": True,
        "consumer": consumer["consumer"],
        "cross_endian_evidence": {
            **consumer["endian_evidence"],
            "declared_limitation": manifest["cross_endian"]["declared_limitation"],
        },
        "fixture": {
            "canonical_output_sha256": canonical_sha,
            "independent_fixture_sha256": fixture_sha,
            "semantic_manifest_sha256": sha256_file(args.manifest),
            "size_bytes": len(fixture),
        },
        "independent_producer": {
            "imports_or_links_libapta": False,
            "source_path": manifest["producer"]["source_path"],
            "source_sha256": sha256_file(args.producer_source),
        },
        "libapta_bridge": bridge["bridge"],
        "platform": {
            "build_configuration": args.configuration,
            "processor": args.platform_processor,
            "shared_library": args.shared.lower() in ("1", "on", "true", "yes"),
            "system": args.platform_system,
            "toolchain": args.platform_toolchain,
        },
        "section_count": consumer["section_count"],
        "sections": consumer["sections"],
        "source_revision": args.source_revision or "unknown",
        "status": "pass",
        "suite_version": manifest["suite_version"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "interchange report: pass; "
        f"fixture={fixture_sha}; platform={args.platform_system}; "
        f"shared={report['platform']['shared_library']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
