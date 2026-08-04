#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Write deterministic ESP-IDF P6 firmware-build interchange evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--fixture-hex", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--embedded-source", required=True, type=Path)
    parser.add_argument("--probe-source", required=True, type=Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("--toolchain-image", required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    fixture = bytes.fromhex(
        "".join(args.fixture_hex.read_text(encoding="ascii").split())
    )
    fixture_sha = hashlib.sha256(fixture).hexdigest()
    if fixture_sha != manifest["fixture"]["sha256"]:
        raise SystemExit("ESP-IDF fixture hash differs from P6 manifest")
    embedded_text = args.embedded_source.read_text(encoding="utf-8")
    import re
    embedded = bytes(
        int(token, 16)
        for token in re.findall(r"0x([0-9a-fA-F]{2})u", embedded_text)
    )
    if embedded != fixture:
        raise SystemExit("ESP-IDF embedded fixture differs from canonical fixture")
    if not args.firmware.is_file() or args.firmware.stat().st_size == 0:
        raise SystemExit("ESP-IDF firmware artifact is missing or empty")

    report = {
        "artifact_schema": "apta-interchange-platform-report-1",
        "execution": {
            "mode": "firmware-build-only",
            "runtime_executed": False,
            "limitation": (
                "Hosted release CI compiles and links the strict container-v1 "
                "probe but does not execute ESP32 firmware on physical hardware."
            ),
        },
        "fixture": {
            "sha256": fixture_sha,
            "size_bytes": len(fixture),
        },
        "firmware": {
            "path": args.firmware.name,
            "sha256": sha256(args.firmware),
            "size_bytes": args.firmware.stat().st_size,
        },
        "embedded_fixture": {
            "bytes_sha256": hashlib.sha256(embedded).hexdigest(),
            "source_sha256": sha256(args.embedded_source),
        },
        "integration": "libapta ESP-IDF reference integration",
        "probe_source_sha256": sha256(args.probe_source),
        "independent_dsp_implementation": False,
        "source_revision": args.source_revision,
        "status": "pass",
        "suite_version": manifest["suite_version"],
        "target": args.target,
        "toolchain_image": args.toolchain_image,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"ESP-IDF interchange report: pass; target={args.target}; "
        f"firmware={report['firmware']['sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
