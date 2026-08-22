#!/usr/bin/env python3
"""Self-test the deterministic standalone ESP-IDF component packager."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGER = ROOT / "ports/espidf/package_component.py"
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(output: Path, archive: Path) -> None:
    subprocess.run(
        [
            sys.executable,
            str(PACKAGER),
            "--source-root",
            str(ROOT),
            "--output-dir",
            str(output),
            "--archive",
            str(archive),
            "--source-revision",
            "0123456789abcdef0123456789abcdef01234567",
        ],
        check=True,
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="apta-component-test-") as temp:
        root = Path(temp)
        first = root / "first/libapta_audio"
        second = root / "second/libapta_audio"
        first_zip = root / "first.zip"
        second_zip = root / "second.zip"
        run(first, first_zip)
        run(second, second_zip)

        if digest(first_zip) != digest(second_zip):
            raise SystemExit("standalone component archive is not reproducible")

        required = {
            "CMakeLists.txt",
            "Kconfig",
            "idf_component.yml",
            "README.md",
            "LICENSE",
            "CHANGELOG.md",
            "API.md",
            "PACKAGE-MANIFEST.json",
            "include/apta/apta.h",
            "include/apta/apta_espidf.h",
            "src/core/apta_context.c",
            "src/key/apta_key.c",
            "src/key/apta_key_internal.h",
            "examples/cooperative_scheduler/CMakeLists.txt",
            "examples/cooperative_scheduler/main/idf_component.yml",
        }
        present = {
            path.relative_to(first).as_posix()
            for path in first.rglob("*")
            if path.is_file()
        }
        missing = sorted(required - present)
        if missing:
            raise SystemExit(f"standalone component is missing files: {missing}")

        cmake = (first / "CMakeLists.txt").read_text(encoding="utf-8")
        if 'set(APTA_ROOT "${CMAKE_CURRENT_LIST_DIR}")' not in cmake:
            raise SystemExit("component CMake does not use packaged local sources")
        if 'CMAKE_CURRENT_LIST_DIR}/../..' in cmake:
            raise SystemExit("component CMake still references the monorepo root")

        component_manifest = (first / "idf_component.yml").read_text(encoding="utf-8")
        for expected in (
            f'version: "{VERSION}"',
            'license: "Apache-2.0"',
            'repository_info:',
            'path: "ports/espidf"',
        ):
            if expected not in component_manifest:
                raise SystemExit(f"component manifest is missing {expected!r}")

        example_manifest = (
            first / "examples/cooperative_scheduler/main/idf_component.yml"
        ).read_text(encoding="utf-8")
        if 'override_path: "../../.."' not in example_manifest:
            raise SystemExit("packaged example does not use the local component override")

        package_manifest = json.loads(
            (first / "PACKAGE-MANIFEST.json").read_text(encoding="utf-8")
        )
        if package_manifest["version"] != VERSION:
            raise SystemExit("package manifest version mismatch")
        if not package_manifest["files"]:
            raise SystemExit("package manifest has no file inventory")

        with zipfile.ZipFile(first_zip) as archive:
            names = archive.namelist()
            prefix = f"libapta_audio-{VERSION}/"
            if not names or any(not name.startswith(prefix) for name in names):
                raise SystemExit("archive root prefix is invalid")
            if any("../" in name or name.startswith("/") for name in names):
                raise SystemExit("archive contains an unsafe path")

    print("standalone ESP-IDF component package: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
