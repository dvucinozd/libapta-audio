#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the repository-level legal and release-license review record."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any


def git_blob_sha1(data: bytes) -> str:
    return hashlib.sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    manifest_path = root / args.manifest if not args.manifest.is_absolute() else args.manifest
    manifest: dict[str, Any] = json.loads(manifest_path.read_text(encoding="utf-8"))
    failures: list[str] = []
    reviewed: list[dict[str, Any]] = []

    if manifest.get("schema") != "apta-legal-review-1": failures.append("unexpected legal-review schema")
    if manifest.get("release_candidate") != (root / "VERSION").read_text(encoding="utf-8").strip(): failures.append("legal review release candidate does not match VERSION")
    if manifest.get("license_spdx") != "Apache-2.0": failures.append("release license is not Apache-2.0")
    if manifest.get("review_status") != "approved": failures.append("legal review is not approved")
    if manifest.get("bundled_third_party_sources") != []: failures.append("bundled third-party source inventory must be empty for rc.1")

    for item in manifest.get("reviewed_files", []):
        relative = item["path"]
        path = root / relative
        if not path.is_file():
            failures.append(f"missing reviewed file: {relative}")
            continue
        data = path.read_bytes()
        actual_sha = git_blob_sha1(data)
        if actual_sha != item["git_blob_sha1"]: failures.append(f"{relative}: legal-review blob mismatch")
        text = data.decode("utf-8")
        for fragment in item.get("required_fragments", []):
            if fragment not in text: failures.append(f"{relative}: missing legal invariant {fragment!r}")
        reviewed.append({"git_blob_sha1": actual_sha, "path": relative})

    if manifest.get("project_notice_required") is False and (root / "NOTICE").exists(): failures.append("NOTICE exists but the review declares no project-specific NOTICE payload")
    license_text = (root / "LICENSE").read_text(encoding="utf-8")
    if "Apache License" not in license_text or "Version 2.0, January 2004" not in license_text: failures.append("LICENSE is not the expected Apache License 2.0 text")
    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    if not re.search(r"### Security and licensing\n", changelog): failures.append("CHANGELOG lacks the Security and licensing boundary")

    report = {"bundled_third_party_sources": 0,"license_spdx": manifest.get("license_spdx"),"project_notice_required": manifest.get("project_notice_required"),"release_candidate": manifest.get("release_candidate"),"reviewed_files": sorted(reviewed, key=lambda item: item["path"]),"schema": "apta-legal-review-report-1","source_revision": args.source_revision,"status": "pass" if not failures else "fail","failures": failures}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        for failure in failures: print(f"legal review failure: {failure}")
        return 1
    print(f"legal review accepted {len(reviewed)} files under Apache-2.0")
    return 0


if __name__ == "__main__": raise SystemExit(main())
