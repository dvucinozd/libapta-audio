#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Verify the APTA normative manifest against Git blob SHA-1 values."""

from __future__ import annotations

import hashlib
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "specification"
MANIFEST = SPEC / "APTA-1.0-NORMATIVE-MANIFEST.md"
ROW = re.compile(
    r"^\| `(?P<path>[^`]+)` \| [^|]+ \| `(?P<sha>[0-9a-f]{40})` \|$"
)


def git_blob_sha1(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def main() -> int:
    failures: list[str] = []
    rows = 0

    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        match = ROW.match(line)
        if match is None:
            continue
        rows += 1
        relative = match.group("path")
        expected = match.group("sha")
        path = SPEC / relative
        if not path.is_file():
            failures.append(f"missing normative file: {relative}")
            continue
        actual = git_blob_sha1(path.read_bytes())
        if actual != expected:
            failures.append(
                f"normative blob mismatch: {relative}: "
                f"expected={expected} actual={actual}"
            )

    if rows == 0:
        failures.append("normative manifest contains no candidate rows")

    manifest_paths = {
        match.group("path")
        for line in MANIFEST.read_text(encoding="utf-8").splitlines()
        if (match := ROW.match(line)) is not None
    }
    if "APTA-SPEC.md" not in manifest_paths:
        failures.append("master specification is absent from the manifest")
    if "container-v1-registry.md" not in manifest_paths:
        failures.append("container version 1 registry is absent from the manifest")

    if failures:
        for failure in failures:
            print(failure)
        return 1

    print(f"verified {rows} normative Git blob hashes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
