#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Emit the independently constructed canonical v1 DJ-section fixture."""

from __future__ import annotations

import struct


def crc32c(data: bytes) -> int:
    value = 0xFFFFFFFF
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x82F63B78 if value & 1 else 0)
    return value ^ 0xFFFFFFFF


def align8(value: int) -> int:
    return (value + 7) & ~7


def wovr() -> bytes:
    payload = bytearray(90)
    struct.pack_into("<IIQIIQQI", payload, 0, 0, 96000, 0, 1, 1, 48, 80, 4)
    struct.pack_into("<QQIII", payload, 48, 0, 96000, 0, 1, 0)
    struct.pack_into("<hhHBBBB", payload, 80, -10, 20, 8, 1, 2, 3, 9)
    return bytes(payload)


def mkey() -> bytes:
    payload = bytearray(72)
    struct.pack_into("<HBBBBhIIQQII", payload, 0, 1, 4, 88, 9, 2, -7,
                     0, 2, 120, 95000, 40, 0)
    struct.pack_into("<BBhHBBII", payload, 40, 9, 2, -7, 62000, 88, 0, 0, 0)
    struct.pack_into("<BBhHBBII", payload, 56, 0, 1, 3, 12000, 40, 0, 0, 0)
    return bytes(payload)


def mtrd() -> bytes:
    payload = bytearray(160)
    struct.pack_into("<HBBHHIIQqIIQ", payload, 0, 1, 4, 86, 4, 4, 0, 2,
                     0, -4, 48, 0, 0)
    struct.pack_into("<QQQqHHBBHIIQ", payload, 48, 0, 48000, 0, -4,
                     4, 4, 4, 86, 0, 0, 4, 0)
    struct.pack_into("<QQQqHHBBHIIQ", payload, 104, 48000, 96000, 48000, 12,
                     3, 4, 4, 86, 0, 0, 7, 0)
    return bytes(payload)


def conf() -> bytes:
    payload = bytearray(80)
    struct.pack_into("<HHIII", payload, 0, 1, 32, 2, 16, 0)
    struct.pack_into("<QI HBBIIQ", payload, 16, 1 << 9, 0x10203040, 930,
                     84, 4, 1 << 3, 0, 0)
    struct.pack_into("<QI HBBIIQ", payload, 48, 1 << 10, 0x50607080, 0xFFFF,
                     0xFF, 4, 1 << 2, 0, 0)
    return bytes(payload)


def build() -> bytes:
    payloads = ((b"WOVR", wovr(), 1), (b"MKEY", mkey(), 0),
                (b"MTRD", mtrd(), 0), (b"CONF", conf(), 0))
    header_size = 96
    directory_size = 40 * len(payloads)
    cursor = align8(header_size + directory_size)
    entries: list[tuple[bytes, bytes, int, int]] = []
    for fourcc, payload, flags in payloads:
        entries.append((fourcc, payload, flags, cursor))
        cursor = align8(cursor + len(payload))
    total_size = entries[-1][3] + len(entries[-1][1])
    output = bytearray(total_size)
    struct.pack_into("<4sHHHHIIIQQQIHH32sI", output, 0, b"APTA", 96, 1,
                     1, 0, 1 << 22, 0, len(entries), 96, total_size, 96000,
                     48000, 2, 2, bytes(32), 0)
    for index, (fourcc, payload, flags, offset) in enumerate(entries):
        struct.pack_into("<4sHHQQQII", output, 96 + index * 40, fourcc, 1,
                         flags, offset, len(payload), len(payload),
                         crc32c(payload), 0)
        output[offset:offset + len(payload)] = payload
    struct.pack_into("<I", output, 92, crc32c(output[:92]))
    return bytes(output)


if __name__ == "__main__":
    print(build().hex())
