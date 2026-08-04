#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the frozen P7 security-review scope and critical file invariants."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    records: list[dict[str, Any]] = []
    failures: list[str] = []

    paths = [item["path"] for item in manifest["reviewed_paths"]]
    if len(paths) != len(set(paths)):
        failures.append("duplicate reviewed path")

    for item in manifest["reviewed_paths"]:
        relative = item["path"]
        path = args.root / relative
        record: dict[str, Any] = {
            "path": relative,
            "category": item["category"],
            "exists": path.is_file(),
            "required_substrings": {},
        }
        if not path.is_file():
            failures.append(f"missing reviewed path: {relative}")
            records.append(record)
            continue

        data = path.read_bytes()
        text = data.decode("utf-8")
        record["sha256"] = hashlib.sha256(data).hexdigest()
        record["size_bytes"] = len(data)
        for token in item.get("required_substrings", []):
            present = token in text
            record["required_substrings"][token] = present
            if not present:
                failures.append(f"{relative}: missing invariant {token!r}")
        records.append(record)

    report = {
        "schema": "apta-security-review-invariant-report-1",
        "review_version": manifest["review_version"],
        "manifest_sha256": sha256_file(args.manifest),
        "source_revision": args.source_revision,
        "status": "pass" if not failures else "fail",
        "reviewed_files": records,
        "accepted_deviations": manifest["accepted_deviations"],
        "failures": failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"P7 security invariant review: {report['status']}; "
        f"files={len(records)}"
    )
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
