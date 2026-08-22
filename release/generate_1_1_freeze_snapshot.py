#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate a hash-pinned APTA 1.1 pre-freeze snapshot.

The generator is intentionally unusable while any 1.1 evidence blocker is
open. It records the exact committed evidence and API/ABI/wire inputs that were
reviewed at the freeze-eligible source revision. It does not bump versions,
create a release commit, or create a Git tag.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

import check_1_1_readiness as readiness

SCHEMA = "apta-1.1-freeze-snapshot-1"


class SnapshotError(RuntimeError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def git_blob_sha1(data: bytes) -> str:
    return hashlib.sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest()


def git_output(root: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=root,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout.strip()


def require_clean_tracked_worktree(root: Path) -> None:
    subprocess.run(
        ["git", "diff", "--quiet", "HEAD", "--"],
        cwd=root,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    subprocess.run(
        ["git", "diff", "--cached", "--quiet", "HEAD", "--"],
        cwd=root,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def committed_record(root: Path, relative: str, record_class: str) -> dict[str, str]:
    path = readiness.repository_path(root, relative)
    if path is None:
        raise SnapshotError(f"path escapes repository: {relative}")
    if not path.is_file():
        raise SnapshotError(f"missing snapshot input: {relative}")

    data = path.read_bytes()
    computed_blob = git_blob_sha1(data)
    try:
        committed_blob = git_output(root, "rev-parse", f"HEAD:{relative}")
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SnapshotError(f"snapshot input is not tracked at HEAD: {relative}") from exc
    if committed_blob != computed_blob:
        raise SnapshotError(
            f"snapshot input differs from HEAD: {relative} ({computed_blob} != {committed_blob})"
        )
    return {
        "class": record_class,
        "git_blob_sha1": computed_blob,
        "path": relative,
        "sha256": sha256_bytes(data),
    }


def generate(root: Path, source_revision: str) -> dict[str, Any]:
    root = root.resolve()
    if not re.fullmatch(r"[0-9a-f]{40}", source_revision):
        raise SnapshotError("source revision must be a full lowercase Git SHA-1")

    try:
        head = git_output(root, "rev-parse", "HEAD")
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SnapshotError("unable to determine checkout HEAD") from exc
    if head != source_revision:
        raise SnapshotError(f"checkout HEAD {head} does not match {source_revision}")

    manifest_path = root / "release/1.1-readiness.json"
    try:
        manifest = readiness.load_json(manifest_path)
        report = readiness.evaluate(root, manifest)
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        raise SnapshotError(f"unable to evaluate release readiness: {exc}") from exc
    if report.get("status") != "pass" or report.get("phase") != "freeze-eligible":
        open_ids = report.get("open_blockers", [])
        raise SnapshotError(
            "tree is not freeze-eligible; open blockers: " + ", ".join(str(x) for x in open_ids)
        )

    try:
        require_clean_tracked_worktree(root)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SnapshotError("tracked worktree must be clean before snapshot generation") from exc

    readiness_record = committed_record(
        root, "release/1.1-readiness.json", "release-readiness"
    )

    evidence_records: list[dict[str, str]] = []
    blockers = manifest.get("blockers", [])
    assert isinstance(blockers, list)
    for blocker in sorted(blockers, key=lambda item: str(item.get("id", ""))):
        blocker_id = str(blocker.get("id", ""))
        evidence = blocker.get("evidence", [])
        assert isinstance(evidence, list)
        for relative in evidence:
            assert isinstance(relative, str)
            record = committed_record(root, relative, "external-evidence")
            record["blocker_id"] = blocker_id
            evidence_records.append(record)

    freeze_records = [
        committed_record(root, relative, "freeze-input")
        for relative in readiness.POLICY_REQUIRED_FREEZE_PATHS
    ]

    return {
        "schema": SCHEMA,
        "source_revision": source_revision,
        "development_version": readiness.POLICY_DEVELOPMENT_VERSION,
        "release_version": readiness.POLICY_RELEASE_VERSION,
        "release_tag": readiness.POLICY_RELEASE_TAG,
        "readiness": readiness_record,
        "evidence": evidence_records,
        "freeze_inputs": freeze_records,
    }


def self_test() -> int:
    sample = b"apta-freeze-snapshot\n"
    if sha256_bytes(sample) != hashlib.sha256(sample).hexdigest():
        raise RuntimeError("SHA-256 self-test failed")
    if git_blob_sha1(sample) != hashlib.sha1(
        f"blob {len(sample)}\0".encode("ascii") + sample
    ).hexdigest():
        raise RuntimeError("Git blob hash self-test failed")
    print("APTA 1.1 freeze snapshot generator self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--source-revision")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.source_revision is None or args.output is None:
        parser.error("--source-revision and --output are required")

    try:
        snapshot = generate(args.root, args.source_revision)
    except SnapshotError as exc:
        print(f"freeze snapshot error: {exc}", file=sys.stderr)
        return 2

    output = args.output if args.output.is_absolute() else args.root.resolve() / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(snapshot, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(snapshot, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
