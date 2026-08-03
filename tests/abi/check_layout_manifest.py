#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    actual = subprocess.check_output([str(args.probe)], text=True).replace("\r\n", "\n")
    expected = args.manifest.read_text(encoding="utf-8").replace("\r\n", "\n")
    if actual == expected:
        return 0

    print(f"layout manifest mismatch: {args.manifest}")
    print("--- ACTUAL LAYOUT BEGIN ---")
    print(actual, end="" if actual.endswith("\n") else "\n")
    print("--- ACTUAL LAYOUT END ---")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
