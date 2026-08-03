#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from pathlib import Path


def run(command: list[str]) -> str:
    return subprocess.check_output(
        command,
        text=True,
        errors="replace",
        timeout=30)


def elf_symbols(library: Path) -> set[str]:
    output = run(["nm", "-D", "--defined-only", str(library)])
    symbols = set()
    for line in output.splitlines():
        fields = line.split()
        if fields:
            symbols.add(fields[-1].split("@", 1)[0])
    return symbols


def pe_symbols(library: Path) -> set[str]:
    llvm = shutil.which("llvm-readobj") or shutil.which("llvm-readobj.exe")
    if llvm:
        output = run([llvm, "--coff-exports", str(library)])
        return set(re.findall(r"Name: (apta_[A-Za-z0-9_]+)", output))

    dumpbin = shutil.which("dumpbin") or shutil.which("dumpbin.exe")
    if dumpbin:
        output = run([dumpbin, "/exports", str(library)])
        symbols = set()
        for line in output.splitlines():
            match = re.match(
                r"\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)",
                line)
            if match:
                symbols.add(match.group(1))
        return symbols
    raise RuntimeError("neither llvm-readobj nor dumpbin is available")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    expected = {
        line.strip()
        for line in args.manifest.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    }
    discovered = (
        pe_symbols(args.library)
        if args.library.suffix.lower() == ".dll"
        else elf_symbols(args.library))
    actual = {symbol for symbol in discovered if symbol.startswith("apta_")}

    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if not missing and not unexpected:
        return 0
    for symbol in missing:
        print(f"missing public symbol: {symbol}")
    for symbol in unexpected:
        print(f"unexpected exported symbol: {symbol}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
