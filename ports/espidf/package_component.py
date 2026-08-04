#!/usr/bin/env python3
"""Build a deterministic, standalone ESP-IDF component from the monorepo."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import sys
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

COMPONENT_NAME = "libapta_audio"
DEFAULT_EPOCH = 1767225600  # 2026-01-01T00:00:00Z; matches stable release packaging.
COPY_TREES = (
    Path("include/apta"),
    Path("src/beatgrid"),
    Path("src/core"),
    Path("src/serialization"),
    Path("src/tempo"),
    Path("src/waveform"),
)
PORT_FILES = (
    Path("CMakeLists.txt"),
    Path("Kconfig"),
    Path("idf_component.yml"),
    Path("apta_espidf.c"),
)
PORT_TREES = (Path("include/apta"),)
EXAMPLE_SOURCE = Path("examples/espidf/cooperative_scheduler")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--source-revision", default=os.environ.get("APTA_SOURCE_REVISION", "unknown"))
    parser.add_argument(
        "--source-date-epoch",
        type=int,
        default=int(os.environ.get("SOURCE_DATE_EPOCH", str(DEFAULT_EPOCH))),
    )
    return parser.parse_args()


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def copy_file(source: Path, destination: Path) -> None:
    if not source.is_file() or source.is_symlink():
        fail(f"required regular file is missing or unsafe: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    destination.chmod(0o644)


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir() or source.is_symlink():
        fail(f"required directory is missing or unsafe: {source}")
    for path in sorted(source.rglob("*")):
        if path.is_symlink():
            fail(f"symbolic links are not allowed in the component package: {path}")
        if path.is_dir():
            continue
        relative = path.relative_to(source)
        copy_file(path, destination / relative)


def rewrite_component_cmake(component_root: Path) -> None:
    cmake = component_root / "CMakeLists.txt"
    text = cmake.read_text(encoding="utf-8")
    monorepo_root = 'set(APTA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")'
    packaged_root = 'set(APTA_ROOT "${CMAKE_CURRENT_LIST_DIR}")'
    if text.count(monorepo_root) != 1:
        fail("ESP-IDF CMake root marker is missing or ambiguous")
    cmake.write_text(
        text.replace(monorepo_root, packaged_root, 1),
        encoding="utf-8",
        newline="\n",
    )


def rewrite_example_for_package(example_root: Path, version: str) -> None:
    cmake = example_root / "CMakeLists.txt"
    cmake.write_text(
        "# SPDX-License-Identifier: Apache-2.0\n"
        "cmake_minimum_required(VERSION 3.16)\n\n"
        "set(EXTRA_COMPONENT_DIRS \"${CMAKE_CURRENT_LIST_DIR}/../..\")\n\n"
        "include($ENV{IDF_PATH}/tools/cmake/project.cmake)\n"
        "project(apta_cooperative_scheduler)\n",
        encoding="utf-8",
        newline="\n",
    )
    manifest = example_root / "main" / "idf_component.yml"
    manifest.write_text(
        "# SPDX-License-Identifier: Apache-2.0\n\n"
        "dependencies:\n"
        "  idf: \">=5.5\"\n"
        "  espressif/esp-dsp:\n"
        "    version: \"^1.8.2\"\n"
        "    require: private\n"
        f"  {COMPONENT_NAME}:\n"
        f"    version: \"^{version}\"\n"
        "    override_path: \"../../..\"\n",
        encoding="utf-8",
        newline="\n",
    )

    main_cmake = example_root / "main" / "CMakeLists.txt"
    main_cmake_text = main_cmake.read_text(encoding="utf-8")
    monorepo_requirement = "REQUIRES espidf esp_timer heap log"
    packaged_requirement = f"REQUIRES {COMPONENT_NAME} esp_timer heap log"
    if main_cmake_text.count(monorepo_requirement) != 1:
        fail("packaged example component requirement marker is missing or ambiguous")
    rewritten_main_cmake = main_cmake_text.replace(
        monorepo_requirement,
        packaged_requirement,
        1,
    )
    if "REQUIRES espidf" in rewritten_main_cmake:
        fail("packaged example still requires the monorepo component name")
    main_cmake.write_text(
        rewritten_main_cmake,
        encoding="utf-8",
        newline="\n",
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def package_manifest(root: Path, version: str, revision: str) -> dict[str, object]:
    rows = []
    for path in sorted(root.rglob("*")):
        if path.is_dir():
            continue
        relative = path.relative_to(root).as_posix()
        if relative == "PACKAGE-MANIFEST.json":
            continue
        rows.append(
            {
                "path": relative,
                "size_bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
        )
    return {
        "schema": "libapta-espidf-component-package-1",
        "component": COMPONENT_NAME,
        "version": version,
        "source_revision": revision,
        "files": rows,
    }


def normalized_zip_datetime(epoch: int) -> tuple[int, int, int, int, int, int]:
    value = datetime.fromtimestamp(epoch, tz=timezone.utc)
    if value.year < 1980:
        value = datetime(1980, 1, 1, tzinfo=timezone.utc)
    return (value.year, value.month, value.day, value.hour, value.minute, value.second)


def write_archive(component_root: Path, archive: Path, version: str, epoch: int) -> None:
    archive.parent.mkdir(parents=True, exist_ok=True)
    prefix = PurePosixPath(f"{COMPONENT_NAME}-{version}")
    timestamp = normalized_zip_datetime(epoch)
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in sorted(component_root.rglob("*")):
            if path.is_dir():
                continue
            relative = PurePosixPath(path.relative_to(component_root).as_posix())
            info = zipfile.ZipInfo(str(prefix / relative), timestamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            info.create_system = 3
            output.writestr(info, path.read_bytes())


def build(args: argparse.Namespace) -> None:
    source_root = args.source_root.resolve()
    output_dir = args.output_dir.resolve()
    port_root = source_root / "ports/espidf"
    version = (source_root / "VERSION").read_text(encoding="utf-8").strip()
    if not version or any(char not in "0123456789." for char in version):
        fail(f"unsupported package version in VERSION: {version!r}")

    if output_dir == source_root or source_root in output_dir.parents:
        # Output inside source is allowed only in conventional disposable directories.
        allowed = {"build", "dist", "out"}
        relative_parts = output_dir.relative_to(source_root).parts
        if not relative_parts or relative_parts[0] not in allowed:
            fail("output inside the source tree must be under build/, dist/ or out/")

    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    for relative in PORT_FILES:
        copy_file(port_root / relative, output_dir / relative)
    for relative in PORT_TREES:
        copy_tree(port_root / relative, output_dir / relative)
    for relative in COPY_TREES:
        copy_tree(source_root / relative, output_dir / relative)

    rewrite_component_cmake(output_dir)

    copy_file(source_root / "LICENSE", output_dir / "LICENSE")
    copy_file(source_root / "CHANGELOG.md", output_dir / "CHANGELOG.md")
    copy_file(source_root / "docs/api/APTA-API-ABI-1.0.md", output_dir / "API.md")
    copy_file(
        source_root / "docs/distribution/ESP-IDF-COMPONENT-REGISTRY.md",
        output_dir / "README.md",
    )
    copy_file(port_root / "README.md", output_dir / "PORTING.md")
    copy_tree(source_root / EXAMPLE_SOURCE, output_dir / "examples/cooperative_scheduler")
    rewrite_example_for_package(output_dir / "examples/cooperative_scheduler", version)

    manifest = package_manifest(output_dir, version, args.source_revision)
    (output_dir / "PACKAGE-MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )

    if args.archive:
        write_archive(output_dir, args.archive.resolve(), version, args.source_date_epoch)


def main() -> int:
    build(parse_args())
    return 0


if __name__ == "__main__":
    sys.exit(main())
