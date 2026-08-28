#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run(analyzer: Path, source: Path, output: Path, epoch: str) -> subprocess.CompletedProcess[bytes]:
    environment = os.environ.copy()
    environment["SOURCE_DATE_EPOCH"] = epoch
    return subprocess.run(
        [str(analyzer), str(source), "--output", str(output), "--profile", "performance"],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: apta_analyze_reproducibility.py ANALYZER INSPECTOR INPUT.wav", file=sys.stderr)
        return 2
    analyzer, inspector, source = map(Path, sys.argv[1:])
    epoch = "1767225600"

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        first = root / "first.apta"
        second = root / "second.apta"
        for output in (first, second):
            completed = run(analyzer, source, output, epoch)
            if completed.returncode != 0:
                sys.stderr.buffer.write(completed.stderr)
                return 1
        if first.read_bytes() != second.read_bytes():
            print("SOURCE_DATE_EPOCH runs produced different container bytes", file=sys.stderr)
            return 1

        inspected = subprocess.run(
            [str(inspector), str(first), "--json"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if inspected.returncode != 0:
            print(inspected.stderr, file=sys.stderr, end="")
            return 1
        metadata = json.loads(inspected.stdout)["META"]
        if metadata["creation_unix_time"] != int(epoch):
            print("container metadata ignored SOURCE_DATE_EPOCH", file=sys.stderr)
            return 1

        invalid = run(analyzer, source, root / "invalid.apta", "not-a-timestamp")
        if invalid.returncode != 2 or b"invalid SOURCE_DATE_EPOCH" not in invalid.stderr:
            print("invalid SOURCE_DATE_EPOCH was not rejected", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
