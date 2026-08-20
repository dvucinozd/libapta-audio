#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def files(root: Path) -> dict[str, bytes]:
    """Return frozen ABI headers; package-version metadata is checked separately."""
    return {
        path.name: path.read_bytes()
        for path in sorted(root.glob("*.h"))
        if path.name != "apta_version.h"
    }


def normalized_digest(data: bytes) -> str:
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def preserves_snapshot_lines(live: bytes, snapshot: bytes) -> bool:
    """Require every frozen 1.0 line to remain unchanged and in order."""
    live_lines = iter(live.replace(b"\r\n", b"\n").splitlines())
    for expected in snapshot.replace(b"\r\n", b"\n").splitlines():
        if not any(candidate == expected for candidate in live_lines):
            return False
    return True


def read_delta_manifest(path: Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        digest, name = stripped.split(maxsplit=1)
        entries[name] = digest
    return entries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--live", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--delta-manifest", type=Path)
    args = parser.parse_args()

    live = files(args.live)
    snapshot = files(args.snapshot)
    deltas = (
        read_delta_manifest(args.delta_manifest)
        if args.delta_manifest is not None
        else {})

    mismatches: list[str] = []
    for name in sorted(set(live) | set(snapshot) | set(deltas)):
        if name in deltas:
            if name not in live or normalized_digest(live[name]) != deltas[name]:
                mismatches.append(f"public header delta drift: {name}")
            if (name in live and name in snapshot and
                    not preserves_snapshot_lines(live[name], snapshot[name])):
                mismatches.append(
                    f"frozen 1.0 declarations changed or reordered: {name}")
        elif live.get(name) != snapshot.get(name):
            mismatches.append(f"public header snapshot drift: {name}")

    for mismatch in mismatches:
        print(mismatch)
    return 1 if mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
