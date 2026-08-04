#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify a stable APTA maintenance release against its immutable base tag."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: top-level JSON value must be an object")
    return value


def define_value(text: str, name: str) -> str | None:
    match = re.search(rf"^#define[ \t]+{re.escape(name)}[ \t]+(.+?)\s*$", text, re.MULTILINE)
    return match.group(1) if match else None


def git_output(root: Path, *args: str) -> str:
    return subprocess.run(["git", *args], cwd=root, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()


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

    if manifest.get("schema") != "apta-maintenance-release-1":
        failures.append("unexpected maintenance-release schema")
    if not re.fullmatch(r"[0-9a-f]{40}", args.source_revision):
        failures.append("source revision is not a full lowercase Git SHA-1")

    try:
        head = git_output(root, "rev-parse", "HEAD")
        if head != args.source_revision:
            failures.append(f"checkout HEAD {head} does not match {args.source_revision}")
    except (OSError, subprocess.CalledProcessError) as exc:
        failures.append(f"unable to verify checkout revision: {exc}")

    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    expected_version = str(manifest.get("release_version", ""))
    if version != expected_version:
        failures.append(f"VERSION is {version!r}, expected {expected_version!r}")

    header = (root / "include/apta/apta_version.h").read_text(encoding="utf-8")
    for name, expected in sorted(manifest.get("version_defines", {}).items()):
        actual = define_value(header, name)
        if actual != expected:
            failures.append(f"{name} is {actual!r}, expected {expected!r}")

    base_tag = str(manifest.get("base_tag", ""))
    try:
        git_output(root, "cat-file", "-e", f"{base_tag}^{{commit}}")
        changed = {line for line in git_output(root, "diff", "--name-only", f"{base_tag}..{args.source_revision}").splitlines() if line}
    except (OSError, subprocess.CalledProcessError) as exc:
        changed = set()
        failures.append(f"unable to inspect maintenance diff: {exc}")

    allowed = set(manifest.get("allowed_paths", []))
    forbidden = sorted(changed - allowed)
    if forbidden:
        failures.append("maintenance release changed forbidden paths: " + ", ".join(forbidden))
    missing = sorted(set(manifest.get("required_changed_paths", [])) - changed)
    if missing:
        failures.append("required maintenance paths did not change: " + ", ".join(missing))

    protected_prefixes = tuple(manifest.get("protected_prefixes", []))
    protected = sorted(path for path in changed if path.startswith(protected_prefixes))
    if protected:
        failures.append("protected implementation/ABI paths changed: " + ", ".join(protected))

    public_header_changes = sorted(path for path in changed if path.startswith("include/apta/") and path != "include/apta/apta_version.h")
    if public_header_changes:
        failures.append("non-version public headers changed: " + ", ".join(public_header_changes))

    for path, fragments in manifest.get("required_fragments", {}).items():
        text = (root / path).read_text(encoding="utf-8")
        for fragment in fragments:
            if fragment not in text:
                failures.append(f"{path}: missing required fragment {fragment!r}")

    try:
        subprocess.run(["python3", str(root / "tests/spec/check_normative_manifest.py")], cwd=root,
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        normative_status = "pass"
    except (OSError, subprocess.CalledProcessError) as exc:
        normative_status = "fail"
        failures.append("normative manifest verification failed: " + (getattr(exc, "stderr", "") or str(exc)).strip())

    report = {
        "base_tag": base_tag,
        "changed_paths": sorted(changed),
        "normative_manifest": normative_status,
        "release_tag": manifest.get("release_tag"),
        "release_version": expected_version,
        "schema": "apta-maintenance-release-report-1",
        "source_revision": args.source_revision,
        "status": "pass" if not failures else "fail",
        "failures": failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        for failure in failures:
            print(f"maintenance release failure: {failure}")
        return 1
    print(f"maintenance release accepted {len(changed)} approved paths from {base_tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
