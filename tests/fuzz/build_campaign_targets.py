#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the frozen P7 libFuzzer inventory against the sanitized library."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--campaign", type=Path, required=True)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--library", type=Path)
    args = parser.parse_args()

    campaign = json.loads(args.campaign.read_text(encoding="utf-8"))
    library = args.library or args.build_dir / "libapta.a"
    output_dir = args.build_dir / "tests"
    output_dir.mkdir(parents=True, exist_ok=True)

    if not library.is_file():
        raise SystemExit(f"sanitized libapta archive not found: {library}")

    common = args.source_root / "tests/fuzz/fuzz_common.c"
    for item in campaign["targets"]:
        output = output_dir / item["binary"]
        command = [
            args.cc,
            "-std=c11",
            "-g",
            "-O1",
            "-fno-omit-frame-pointer",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            "-fsanitize=fuzzer,address,undefined",
            f"-I{args.source_root / 'include'}",
            f"-I{args.source_root / 'tests/fuzz'}",
            str(common),
            str(args.source_root / item["source"]),
            str(library),
            "-lm",
            "-o",
            str(output),
        ]
        print("+", " ".join(command), flush=True)
        subprocess.run(command, check=True)
        if not output.is_file():
            raise SystemExit(f"compiler did not create {output}")

    print(f"built {len(campaign['targets'])} P7 fuzz targets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
