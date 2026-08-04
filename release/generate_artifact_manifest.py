#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate and verify a deterministic RC archive/checksum manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

ARCHIVE_SUFFIXES = (".tar.gz", ".zip")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    archives = sorted(path for path in root.rglob("*") if path.is_file() and path.name.endswith(ARCHIVE_SUFFIXES))
    if len(archives) < 3: raise SystemExit(f"expected at least three RC archives, found {len(archives)}")
    records = []
    for archive in archives:
        if args.version not in archive.name: raise SystemExit(f"archive name does not contain {args.version}: {archive.name}")
        actual = sha256_file(archive)
        sidecar = Path(str(archive) + ".sha256")
        if not sidecar.is_file(): raise SystemExit(f"missing SHA-256 sidecar for {archive}")
        match = re.search(r"\b([0-9a-fA-F]{64})\b", sidecar.read_text(encoding="utf-8"))
        if not match: raise SystemExit(f"invalid SHA-256 sidecar: {sidecar}")
        if match.group(1).lower() != actual: raise SystemExit(f"checksum mismatch for {archive.name}")
        records.append({"archive": archive.relative_to(root).as_posix(),"sha256": actual,"sidecar": sidecar.relative_to(root).as_posix(),"size_bytes": archive.stat().st_size})
    report = {"archive_count": len(records),"archives": records,"platform": args.platform,"release_candidate": args.version,"schema": "apta-rc-artifact-manifest-1","source_revision": args.source_revision,"status": "pass"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"recorded {len(records)} verified RC archives for {args.platform}")
    return 0


if __name__ == "__main__": raise SystemExit(main())
