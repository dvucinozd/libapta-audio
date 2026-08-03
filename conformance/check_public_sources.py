#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reject private-library dependencies in public conformance sources."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)
ALLOWED_LOCAL = {"apta_test_geometry.h"}
FORBIDDEN = ("apta_internal", "../", "src/", "tests/unit", "ports/")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()

    failures: list[str] = []
    for path in sorted(args.root.rglob("*")):
        if path.suffix not in {".c", ".h", ".cpp", ".cc"}:
            continue
        text = path.read_text(encoding="utf-8")
        for delimiter, include in INCLUDE.findall(text):
            normalized = include.replace("\\", "/")
            if any(token in normalized for token in FORBIDDEN):
                failures.append(f"{path}: forbidden include {include}")
            if delimiter == '"' and normalized not in ALLOWED_LOCAL:
                failures.append(f"{path}: non-public local include {include}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("public conformance sources use only installed/public headers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
