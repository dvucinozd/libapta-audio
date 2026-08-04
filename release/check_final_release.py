#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the final APTA 1.0 release against the accepted release candidate."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any


def git_blob_sha1(data: bytes) -> str:
    return hashlib.sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: top-level JSON value must be an object")
    return value


def define_value(text: str, name: str) -> str | None:
    match = re.search(
        rf"^#define[ \t]+{re.escape(name)}[ \t]+(.+?)\s*$",
        text,
        re.MULTILINE,
    )
    return match.group(1) if match else None


def git_output(root: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=root,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    manifest_path = (
        (root / args.manifest).resolve()
        if not args.manifest.is_absolute()
        else args.manifest
    )
    manifest = read_json(manifest_path)
    failures: list[str] = []
    inherited_records: list[dict[str, Any]] = []

    if manifest.get("schema") != "apta-final-release-1":
        failures.append("unexpected final-release schema")
    if not re.fullmatch(r"[0-9a-f]{40}", args.source_revision):
        failures.append("source revision is not a full lowercase Git SHA-1")

    try:
        head = git_output(root, "rev-parse", "HEAD")
        if head != args.source_revision:
            failures.append(
                f"checkout HEAD {head} does not match {args.source_revision}"
            )
    except (OSError, subprocess.CalledProcessError) as exc:
        failures.append(f"unable to verify checkout revision: {exc}")

    expected_version = str(manifest.get("release_version", ""))
    version_path = root / "VERSION"
    version = (
        version_path.read_text(encoding="utf-8").strip()
        if version_path.is_file()
        else ""
    )
    if version != expected_version:
        failures.append(f"VERSION is {version!r}, expected {expected_version!r}")

    header_path = root / "include/apta/apta_version.h"
    header = header_path.read_text(encoding="utf-8") if header_path.is_file() else ""
    for name, expected in sorted(manifest.get("version_defines", {}).items()):
        actual = define_value(header, name)
        if actual != expected:
            failures.append(f"{name} is {actual!r}, expected {expected!r}")

    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    for fragment in manifest.get("required_changelog_fragments", []):
        if fragment not in changelog:
            failures.append(
                f"CHANGELOG.md is missing required fragment: {fragment}"
            )

    master_spec = (root / "specification/APTA-SPEC.md").read_text(
        encoding="utf-8"
    )
    if "**Status:** APTA 1.0 Final" not in master_spec:
        failures.append("master specification is not marked APTA 1.0 Final")
    if "The stable APTA 1.0 mapping is specification 1.0" not in master_spec:
        failures.append("master specification lacks the final version mapping")

    normative_manifest = (
        root / str(manifest.get("normative_manifest", ""))
    ).read_text(encoding="utf-8")
    if "**Status:** Final APTA 1.0 manifest" not in normative_manifest:
        failures.append("normative manifest is not marked final")
    if "`v1.0.0` tag freezes this manifest" not in normative_manifest:
        failures.append("normative manifest lacks the final tag authority")

    rc_manifest_path = root / str(manifest.get("rc_freeze_manifest", ""))
    try:
        rc_manifest = read_json(rc_manifest_path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        failures.append(f"unable to read accepted RC manifest: {exc}")
        rc_manifest = {}

    accepted_rc = str(manifest.get("accepted_rc_revision", ""))
    if not re.fullmatch(r"[0-9a-f]{40}", accepted_rc):
        failures.append("accepted RC revision is not a full Git SHA-1")
    else:
        try:
            git_output(root, "cat-file", "-e", f"{accepted_rc}^{{commit}}")
        except (OSError, subprocess.CalledProcessError) as exc:
            failures.append(f"accepted RC revision is unavailable: {exc}")

    mutable = set(manifest.get("mutable_rc_frozen_paths", []))
    for item in rc_manifest.get("frozen_files", []):
        relative = item.get("path")
        expected_sha = item.get("git_blob_sha1")
        if not isinstance(relative, str) or not isinstance(expected_sha, str):
            failures.append("invalid RC frozen-file entry")
            continue
        if relative in mutable:
            continue
        path = root / relative
        if not path.is_file():
            failures.append(f"missing inherited frozen file: {relative}")
            continue
        actual_sha = git_blob_sha1(path.read_bytes())
        passed = actual_sha == expected_sha
        inherited_records.append(
            {
                "class": item.get("class", "unspecified"),
                "git_blob_sha1": actual_sha,
                "path": relative,
                "status": "pass" if passed else "fail",
            }
        )
        if not passed:
            failures.append(
                f"{relative}: inherited blob {actual_sha}, expected {expected_sha}"
            )

    allowed_paths = set(manifest.get("allowed_finalization_paths", []))
    if len(allowed_paths) != len(manifest.get("allowed_finalization_paths", [])):
        failures.append("allowed finalization path list contains duplicates")
    if accepted_rc:
        try:
            changed_output = git_output(
                root, "diff", "--name-only", f"{accepted_rc}..{args.source_revision}"
            )
            changed_paths = {
                line.strip() for line in changed_output.splitlines() if line.strip()
            }
            forbidden = sorted(changed_paths - allowed_paths)
            if forbidden:
                failures.append(
                    "finalization changed forbidden paths: " + ", ".join(forbidden)
                )
        except (OSError, subprocess.CalledProcessError) as exc:
            changed_paths = set()
            failures.append(f"unable to inspect finalization diff: {exc}")
    else:
        changed_paths = set()

    blocker_path = root / str(manifest.get("blocker_manifest", ""))
    try:
        blockers = read_json(blocker_path)
        entries = blockers.get("blockers", [])
        if blockers.get("open_blocker_count") != 0:
            failures.append("S9 open blocker count is not zero")
        if len(entries) != 10:
            failures.append(
                f"S9 blocker inventory has {len(entries)} entries, expected 10"
            )
        for blocker in entries:
            if blocker.get("status") != "closed":
                failures.append(
                    f"S9 blocker {blocker.get('id')} is not closed"
                )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        failures.append(f"unable to validate blocker manifest: {exc}")

    normative_check = root / "tests/spec/check_normative_manifest.py"
    try:
        subprocess.run(
            ["python3", str(normative_check)],
            cwd=root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        normative_status = "pass"
    except (OSError, subprocess.CalledProcessError) as exc:
        normative_status = "fail"
        detail = getattr(exc, "stderr", "") or str(exc)
        failures.append(
            f"normative manifest verification failed: {detail.strip()}"
        )

    report = {
        "accepted_rc_revision": accepted_rc,
        "changed_paths": sorted(changed_paths),
        "final_version": expected_version,
        "inherited_frozen_file_count": len(inherited_records),
        "inherited_frozen_files": sorted(
            inherited_records, key=lambda item: item["path"]
        ),
        "normative_manifest": normative_status,
        "release_tag": manifest.get("release_tag"),
        "required_gates": manifest.get("required_gates", []),
        "schema": "apta-final-release-report-1",
        "source_revision": args.source_revision,
        "status": "pass" if not failures else "fail",
        "failures": failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if failures:
        for failure in failures:
            print(f"final release failure: {failure}")
        return 1
    print(
        "final release accepted "
        f"{len(inherited_records)} inherited frozen files and "
        f"{len(changed_paths)} approved finalization paths"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
