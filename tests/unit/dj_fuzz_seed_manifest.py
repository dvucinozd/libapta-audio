#!/usr/bin/env python3
"""Require the committed DJ container golden in the parser fuzz corpus."""

from __future__ import annotations

import json
import pathlib
import sys


manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
expected = "tests/fixtures/dj-sections-v1-combined.apta.hex"
matches = [
    item for item in manifest["container_seed_sources"]
    if item.get("kind") == "hex" and item.get("path") == expected
]
if len(matches) != 1:
    raise SystemExit(f"expected exactly one DJ hex seed, found {len(matches)}")
