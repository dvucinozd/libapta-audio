#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Classify every configured CTest test with one ordered manifest rule."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Any


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain an object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ctest", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--config", default="")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    manifest = load(args.manifest)
    if manifest.get("manifest_version") != "apta-test-classification-1":
        raise ValueError("unsupported classification manifest version")
    categories = set(manifest["categories"])
    rules: list[tuple[str, str, re.Pattern[str]]] = []
    for rule in manifest["rules"]:
        category = rule["category"]
        if category not in categories:
            raise ValueError(f"unknown category in rule {rule['id']}")
        rules.append((rule["id"], category, re.compile(rule["pattern"])))

    command = [
        args.ctest,
        "--test-dir",
        str(args.build_dir),
        "--show-only=json-v1",
    ]
    if args.config:
        command.extend(["-C", args.config])
    completed = subprocess.run(
        command, capture_output=True, text=True, check=False
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "ctest inventory failed")
    inventory = json.loads(completed.stdout)
    names = sorted(test["name"] for test in inventory.get("tests", []))
    if not names:
        raise ValueError("CTest inventory is empty")

    rows: list[dict[str, str]] = []
    for name in names:
        for rule_id, category, pattern in rules:
            if pattern.search(name):
                rows.append(
                    {"name": name, "category": category, "rule": rule_id}
                )
                break
        else:
            raise ValueError(f"unclassified CTest test: {name}")

    counts = Counter(row["category"] for row in rows)
    output = {
        "inventory_version": "apta-test-inventory-1",
        "classification_manifest_version": manifest["manifest_version"],
        "classification_manifest_sha256": hashlib.sha256(
            args.manifest.read_bytes()
        ).hexdigest(),
        "test_count": len(rows),
        "counts": {key: counts.get(key, 0) for key in sorted(categories)},
        "tests": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(output, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(
        f"classified {len(rows)} tests: "
        + ", ".join(f"{key}={output['counts'][key]}" for key in sorted(categories))
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"test classification error: {error}", file=sys.stderr)
        raise SystemExit(2)
