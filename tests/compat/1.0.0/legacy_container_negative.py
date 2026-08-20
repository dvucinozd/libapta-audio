#!/usr/bin/env python3
"""Negative controls for the frozen APTA 1.0 framing consumer."""

from __future__ import annotations

import pathlib
import struct
import subprocess
import sys
import tempfile


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


def read_hex(path: pathlib.Path) -> bytes:
    return bytes.fromhex(path.read_text(encoding="ascii"))


def refresh_header(data: bytearray) -> None:
    struct.pack_into("<I", data, 92, crc32c(data[:92]))


def run(consumer: pathlib.Path, data: bytes, expect_ok: bool) -> None:
    with tempfile.NamedTemporaryFile(suffix=".apta", delete=False) as stream:
        path = pathlib.Path(stream.name)
        stream.write(data)
    try:
        status = subprocess.run(
            [str(consumer), str(path)], check=False,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
    finally:
        path.unlink(missing_ok=True)
    if (status == 0) != expect_ok:
        raise AssertionError(f"consumer status {status}, expect_ok={expect_ok}")


def main() -> int:
    consumer = pathlib.Path(sys.argv[1])
    valid = read_hex(pathlib.Path(sys.argv[2]))
    run(consumer, valid, True)

    # Misaligned but internally CRC-correct MKEY stored range.
    bad = bytearray(valid)
    struct.pack_into("<QQQ", bad, 136 + 8, 353, 71, 71)
    struct.pack_into("<I", bad, 136 + 32, crc32c(bad[353:424]))
    run(consumer, bad, False)

    # Unknown optional section aliases WOVR exactly: overlapping ranges.
    bad = bytearray(valid)
    bad[136 + 8:136 + 36] = bad[96 + 8:96 + 36]
    run(consumer, bad, False)

    # The directory itself cannot overlap the fixed header.
    bad = bytearray(valid)
    struct.pack_into("<Q", bad, 24, 88)
    refresh_header(bad)
    run(consumer, bad, False)

    # Unknown payload points into the directory itself.
    bad = bytearray(valid)
    struct.pack_into("<QQQ", bad, 136 + 8, 96, 40, 40)
    struct.pack_into("<I", bad, 136 + 32, crc32c(bad[96:136]))
    run(consumer, bad, False)

    # WOVR must be required in APTA 1.0.
    bad = bytearray(valid)
    struct.pack_into("<H", bad, 96 + 6, 0)
    run(consumer, bad, False)

    # An unknown section marked required cannot be skipped.
    bad = bytearray(valid)
    struct.pack_into("<H", bad, 136 + 6, 1)
    run(consumer, bad, False)

    # Entry-local CRC remains an independent negative control.
    bad = bytearray(valid)
    bad[96 + 32] ^= 1
    run(consumer, bad, False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
