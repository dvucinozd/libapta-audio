#!/usr/bin/env python3
"""Reject mutable external GitHub Actions and Docker references."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable

ACTION_REF = re.compile(
    r"^\s*(?:-\s*)?uses:\s*([^\s#]+)(?:\s+#.*)?$"
)
IMAGE_REF = re.compile(
    r"^\s*image:\s*([^\s#]+)(?:\s+#.*)?$"
)
FULL_SHA = re.compile(r"^[0-9a-fA-F]{40}$")
DIGEST = re.compile(r"@sha256:[0-9a-fA-F]{64}$")


def workflow_files(root: Path) -> Iterable[Path]:
    workflows = root / ".github" / "workflows"
    for pattern in ("*.yml", "*.yaml"):
        yield from sorted(workflows.glob(pattern))


def check_file(root: Path, path: Path) -> list[dict[str, object]]:
    violations: list[dict[str, object]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        action_match = ACTION_REF.match(line)
        if action_match:
            reference = action_match.group(1).strip("\"'")
            if reference.startswith("./"):
                continue
            if reference.startswith("docker://"):
                if not DIGEST.search(reference):
                    violations.append(
                        {
                            "path": path.relative_to(root).as_posix(),
                            "line": line_number,
                            "kind": "docker-action",
                            "reference": reference,
                            "message": "docker actions must use an immutable sha256 digest",
                        }
                    )
                continue
            if "@" not in reference:
                violations.append(
                    {
                        "path": path.relative_to(root).as_posix(),
                        "line": line_number,
                        "kind": "action",
                        "reference": reference,
                        "message": "external actions must include a ref",
                    }
                )
                continue
            _, ref = reference.rsplit("@", 1)
            if not FULL_SHA.fullmatch(ref):
                violations.append(
                    {
                        "path": path.relative_to(root).as_posix(),
                        "line": line_number,
                        "kind": "action",
                        "reference": reference,
                        "message": "external actions must be pinned to a full 40-character commit SHA",
                    }
                )
            continue

        image_match = IMAGE_REF.match(line)
        if image_match:
            reference = image_match.group(1).strip("\"'")
            if "${{" in reference:
                continue
            if not DIGEST.search(reference):
                violations.append(
                    {
                        "path": path.relative_to(root).as_posix(),
                        "line": line_number,
                        "kind": "container-image",
                        "reference": reference,
                        "message": "literal container images must use an immutable sha256 digest",
                    }
                )
    return violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    violations: list[dict[str, object]] = []
    checked = []
    for path in workflow_files(root):
        checked.append(path.relative_to(root).as_posix())
        violations.extend(check_file(root, path))

    report = {
        "schema": "apta-workflow-pin-report-v1",
        "checked_workflows": checked,
        "violation_count": len(violations),
        "violations": violations,
    }
    if args.output:
        output = args.output
        if not output.is_absolute():
            output = root / output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if violations:
        for item in violations:
            print(
                f"{item['path']}:{item['line']}: {item['message']}: "
                f"{item['reference']}",
                file=sys.stderr,
            )
        return 1

    print(f"verified {len(checked)} workflow files: all external references are immutable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
