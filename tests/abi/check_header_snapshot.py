#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def files(root: Path) -> dict[str, bytes]:
    return {
        str(path.relative_to(root)): path.read_bytes()
        for path in sorted(root.rglob("*.h"))
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--live", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    args = parser.parse_args()

    live = files(args.live)
    snapshot = files(args.snapshot)
    if live == snapshot:
        return 0

    for name in sorted(set(live) | set(snapshot)):
        if live.get(name) != snapshot.get(name):
            print(f"public header snapshot drift: {name}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
