#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the immutable APTA 1.0-rc.1 release-candidate contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


def git_blob_sha1(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: top-level JSON value must be an object")
    return value


def define_value(text: str, name: str) -> str | None:
    match = re.search(rf"^#define[ \t]+{re.escape(name)}[ \t]+(.+?)\s*$", text, re.MULTILINE)
    return match.group(1) if match else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    manifest_path = (root / args.manifest).resolve() if not args.manifest.is_absolute() else args.manifest
    manifest = read_json(manifest_path)
    failures: list[str] = []
    records: list[dict[str, Any]] = []

    if manifest.get("schema") != "apta-rc-freeze-1":
        failures.append("unexpected RC freeze schema")
    if not re.fullmatch(r"[0-9a-f]{40}", args.source_revision):
        failures.append("source revision is not a full lowercase Git SHA-1")

    try:
        head = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=root, check=True,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        ).stdout.strip()
        if head != args.source_revision:
            failures.append(f"checkout HEAD {head} does not match {args.source_revision}")
    except (OSError, subprocess.CalledProcessError) as exc:
        failures.append(f"unable to verify checkout revision: {exc}")

    version_path = root / "VERSION"
    version = version_path.read_text(encoding="utf-8").strip() if version_path.exists() else ""
    expected_version = str(manifest.get("release_candidate", ""))
    if version != expected_version:
        failures.append(f"VERSION is {version!r}, expected {expected_version!r}")

    header_path = root / "include/apta/apta_version.h"
    header = header_path.read_text(encoding="utf-8") if header_path.exists() else ""
    expected_defines = manifest.get("version_defines", {})
    for name, expected in sorted(expected_defines.items()):
        actual = define_value(header, name)
        if actual != expected:
            failures.append(f"{name} is {actual!r}, expected {expected!r}")

    changelog_path = root / "CHANGELOG.md"
    changelog = changelog_path.read_text(encoding="utf-8") if changelog_path.exists() else ""
    heading = f"## [{expected_version}] - {manifest.get('freeze_date', '')}"
    if heading not in changelog:
        failures.append(f"CHANGELOG.md is missing {heading!r}")
    for fragment in manifest.get("required_changelog_fragments", []):
        if fragment not in changelog:
            failures.append(f"CHANGELOG.md is missing required fragment: {fragment}")

    frozen_files = manifest.get("frozen_files", [])
    paths = [item.get("path") for item in frozen_files]
    if len(paths) != len(set(paths)):
        failures.append("frozen file list contains duplicate paths")
    for item in frozen_files:
        relative = item.get("path")
        expected_sha = item.get("git_blob_sha1")
        if not isinstance(relative, str) or not isinstance(expected_sha, str):
            failures.append("invalid frozen file entry")
            continue
        path = root / relative
        if not path.is_file():
            failures.append(f"missing frozen file: {relative}")
            continue
        actual_sha = git_blob_sha1(path.read_bytes())
        passed = actual_sha == expected_sha
        records.append({
            "class": item.get("class", "unspecified"),
            "git_blob_sha1": actual_sha,
            "path": relative,
            "status": "pass" if passed else "fail",
        })
        if not passed:
            failures.append(f"{relative}: blob {actual_sha}, expected {expected_sha}")

    blocker_path = root / str(manifest.get("blocker_manifest", ""))
    try:
        blockers = read_json(blocker_path)
        open_count = blockers.get("open_blocker_count")
        entries = blockers.get("blockers", [])
        if open_count != 0:
            failures.append(f"S9 open blocker count is {open_count}, expected 0")
        if len(entries) != 10:
            failures.append(f"S9 blocker inventory has {len(entries)} entries, expected 10")
        for blocker in entries:
            if blocker.get("status") != "closed":
                failures.append(f"S9 blocker {blocker.get('id')} is not closed")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        failures.append(f"unable to validate blocker manifest: {exc}")

    normative_check = root / "tests/spec/check_normative_manifest.py"
    try:
        subprocess.run(
            [sys.executable, str(normative_check)], cwd=root, check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        normative_status = "pass"
    except (OSError, subprocess.CalledProcessError) as exc:
        normative_status = "fail"
        detail = getattr(exc, "stderr", "") or str(exc)
        failures.append(f"normative manifest verification failed: {detail.strip()}")

    report = {
        "blocker_count": 10,
        "frozen_file_count": len(records),
        "frozen_files": sorted(records, key=lambda item: item["path"]),
        "normative_manifest": normative_status,
        "release_candidate": expected_version,
        "required_gates": manifest.get("required_gates", []),
        "schema": "apta-rc-freeze-report-1",
        "source_revision": args.source_revision,
        "status": "pass" if not failures else "fail",
        "unresolved_blockers": 0 if not failures else len(failures),
        "failures": failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        for failure in failures:
            print(f"RC freeze failure: {failure}")
        return 1
    print(f"RC freeze accepted {len(records)} frozen files with zero S9 blockers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
