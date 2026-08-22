#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the APTA 1.1 evidence boundary before release freeze."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Any

SCHEMA = "apta-1.1-release-readiness-1"
REPORT_SCHEMA = "apta-1.1-release-readiness-report-1"
POLICY_DEVELOPMENT_VERSION = "1.0.1"
POLICY_RELEASE_VERSION = "1.1.0"
POLICY_RELEASE_TAG = "v1.1.0"

# Required external evidence cannot be removed or redirected by editing only the
# manifest. Evidence contents are validated by their dedicated frozen protocols;
# readiness intentionally requires the reviewed files to exist and be non-empty.
POLICY_REQUIRED_BLOCKERS = {
    "tempo-grid-fresh-corpus": ("evidence/1.1/tempo-ensemble-acceptance.json",),
    "confidence-calibration-holdout": ("evidence/1.1/confidence-calibration-holdout.json",),
    "final-dj-corpus": ("evidence/1.1/dj-acceptance-report.json",),
    "esp32-p4-hardware": ("evidence/1.1/esp32-p4-hardware.json",),
}

# These inputs define the minimum release-freeze surface. Keeping this inventory
# in code as well as in the manifest prevents a later JSON edit from silently
# weakening the release gate by dropping a platform ABI or normative contract.
POLICY_REQUIRED_FREEZE_PATHS = (
    "docs/api/APTA-API-1.1-DEVELOPMENT.md",
    "specification/APTA-1.1-DJ-SECTIONS.md",
    "docs/file-format/APTA-STREAMING-IO-1.1.md",
    "abi/public-header-deltas-1.1.sha256",
    "abi/public-symbols-1.1.txt",
    "abi/public-symbols-1.1.map",
    "abi/public-symbols-1.1.def",
    "abi/public-layout-ilp32.txt",
    "abi/public-layout-lp64.txt",
    "abi/public-layout-llp64.txt",
    "abi/public-layout-p32a64.txt",
)


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected JSON object")
    return value


def define_value(text: str, name: str) -> str | None:
    match = re.search(rf"^#define[ \t]+{re.escape(name)}[ \t]+(.+?)\s*$", text, re.MULTILINE)
    return match.group(1) if match else None


def repository_tags(root: Path) -> set[str]:
    # Do not turn a Git failure into an empty tag set: this is release policy and
    # inability to inspect refs must fail closed at the caller.
    output = subprocess.run(
        ["git", "tag", "--list"],
        cwd=root,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout
    return {line.strip() for line in output.splitlines() if line.strip()}


def repository_path(root: Path, relative: str) -> Path | None:
    candidate = Path(relative)
    if candidate.is_absolute():
        return None
    resolved_root = root.resolve()
    resolved = (resolved_root / candidate).resolve()
    try:
        resolved.relative_to(resolved_root)
    except ValueError:
        return None
    return resolved


def evaluate(root: Path, manifest: dict[str, Any]) -> dict[str, Any]:
    failures: list[str] = []
    blockers = manifest.get("blockers")
    if manifest.get("schema") != SCHEMA:
        failures.append("unexpected manifest schema")
    if manifest.get("development_version") != POLICY_DEVELOPMENT_VERSION:
        failures.append("development_version does not match release-readiness policy")
    if manifest.get("release_version") != POLICY_RELEASE_VERSION:
        failures.append("release_version does not match release-readiness policy")
    if manifest.get("release_tag") != POLICY_RELEASE_TAG:
        failures.append("release_tag does not match release-readiness policy")
    if not isinstance(blockers, list) or not blockers:
        failures.append("blockers must be a non-empty array")
        blockers = []

    ids: list[str] = []
    open_ids: list[str] = []
    blocker_by_id: dict[str, dict[str, Any]] = {}
    for blocker in blockers:
        if not isinstance(blocker, dict):
            failures.append("invalid blocker entry")
            continue
        blocker_id = blocker.get("id")
        status = blocker.get("status")
        evidence = blocker.get("evidence")
        if not isinstance(blocker_id, str) or not blocker_id:
            failures.append("blocker id must be non-empty")
            continue
        ids.append(blocker_id)
        if blocker_id not in blocker_by_id:
            blocker_by_id[blocker_id] = blocker
        if status not in {"open", "closed"}:
            failures.append(f"{blocker_id}: invalid status {status!r}")
            continue
        if not isinstance(evidence, list) or not evidence or any(
            not isinstance(item, str) or not item for item in evidence
        ):
            failures.append(f"{blocker_id}: evidence must be a non-empty path list")
            continue
        if status == "open":
            open_ids.append(blocker_id)
        else:
            for relative in evidence:
                path = repository_path(root, relative)
                if path is None:
                    failures.append(f"{blocker_id}: evidence path escapes repository: {relative}")
                elif not path.is_file() or path.stat().st_size == 0:
                    failures.append(f"{blocker_id}: missing evidence {relative}")

    if len(ids) != len(set(ids)):
        failures.append("duplicate blocker ids")

    for blocker_id, expected_evidence in POLICY_REQUIRED_BLOCKERS.items():
        blocker = blocker_by_id.get(blocker_id)
        if blocker is None:
            failures.append(f"policy-required blocker omitted from manifest: {blocker_id}")
            continue
        evidence = blocker.get("evidence")
        if evidence != list(expected_evidence):
            failures.append(f"{blocker_id}: evidence path does not match release-readiness policy")

    required_paths = manifest.get("required_freeze_paths", [])
    if not isinstance(required_paths, list):
        failures.append("required_freeze_paths must be an array")
        required_paths = []

    valid_required_paths: list[str] = []
    for relative in required_paths:
        if not isinstance(relative, str) or not relative:
            failures.append("invalid required freeze path")
            continue
        valid_required_paths.append(relative)
        path = repository_path(root, relative)
        if path is None:
            failures.append(f"freeze input path escapes repository: {relative}")
        elif not path.is_file() or path.stat().st_size == 0:
            failures.append(f"missing freeze input {relative}")

    if len(valid_required_paths) != len(set(valid_required_paths)):
        failures.append("duplicate required freeze paths")

    required_set = set(valid_required_paths)
    for relative in POLICY_REQUIRED_FREEZE_PATHS:
        if relative not in required_set:
            failures.append(f"policy-required freeze input omitted from manifest: {relative}")

    # This checker governs the tree *before* the deliberate release-freeze
    # commit. Even with every evidence blocker closed, freeze-eligible still
    # means development identity: no version bump and no release tag anywhere.
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    header = (root / "include/apta/apta_version.h").read_text(encoding="utf-8")
    package_string = define_value(header, "APTA_PACKAGE_VERSION_STRING")
    tags = repository_tags(root)

    if version != POLICY_DEVELOPMENT_VERSION:
        failures.append(
            f"VERSION changed before release freeze: {version!r}, expected {POLICY_DEVELOPMENT_VERSION!r}"
        )
    if package_string != f'"{POLICY_DEVELOPMENT_VERSION}"':
        failures.append(
            f"APTA_PACKAGE_VERSION_STRING changed before release freeze: {package_string!r}"
        )
    if POLICY_RELEASE_TAG in tags:
        failures.append(f"release tag {POLICY_RELEASE_TAG} already exists before release freeze")

    phase = "blocked" if open_ids else "freeze-eligible"

    return {
        "schema": REPORT_SCHEMA,
        "phase": phase,
        "open_blocker_count": len(open_ids),
        "open_blockers": sorted(open_ids),
        "release_tag": POLICY_RELEASE_TAG,
        "version": version,
        "status": "pass" if not failures else "fail",
        "failures": failures,
    }


def self_test() -> int:
    if len(POLICY_REQUIRED_BLOCKERS) != 4:
        raise RuntimeError("readiness blocker inventory must contain exactly four blockers")
    if len(POLICY_REQUIRED_FREEZE_PATHS) != len(set(POLICY_REQUIRED_FREEZE_PATHS)):
        raise RuntimeError("release freeze inventory contains duplicates")
    print("APTA 1.1 release-readiness checker self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--manifest", type=Path, default=Path("release/1.1-readiness.json"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--expect", choices=("blocked", "freeze-eligible"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    root = args.root.resolve()
    manifest_path = args.manifest if args.manifest.is_absolute() else root / args.manifest
    try:
        report = evaluate(root, load_json(manifest_path))
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"readiness error: {exc}")
        return 3

    if args.expect is not None and report["phase"] != args.expect:
        report["failures"].append(
            f"phase is {report['phase']!r}, expected {args.expect!r}"
        )
        report["status"] = "fail"

    if args.output is not None:
        output = args.output if args.output.is_absolute() else root / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(json.dumps(report, sort_keys=True))
    return 0 if report["status"] == "pass" else 2


if __name__ == "__main__":
    raise SystemExit(main())
