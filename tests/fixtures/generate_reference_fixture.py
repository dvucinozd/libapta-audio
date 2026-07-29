#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Generate the independent version-1 WOVR+META reference fixture."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


POLYNOMIAL = 0x82F63B78
CONTAINER_HEADER_SIZE = 96
DIRECTORY_ENTRY_SIZE = 40
EXPECTED_SIZE = 303
EXPECTED_SHA256 = "394403f6e0617cde449f88c35b87d7d3a136ca304ae4874cba65310724a1d7d2"


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            mask = -(crc & 1) & 0xFFFFFFFF
            crc = ((crc >> 1) ^ (POLYNOMIAL & mask)) & 0xFFFFFFFF
    return crc ^ 0xFFFFFFFF


def put_u16(buffer: bytearray, offset: int, value: int) -> None:
    buffer[offset : offset + 2] = value.to_bytes(2, "little", signed=False)


def put_u32(buffer: bytearray, offset: int, value: int) -> None:
    buffer[offset : offset + 4] = value.to_bytes(4, "little", signed=False)


def put_u64(buffer: bytearray, offset: int, value: int) -> None:
    buffer[offset : offset + 8] = value.to_bytes(8, "little", signed=False)


def build_wovr() -> bytes:
    payload = bytearray(90)
    put_u32(payload, 0, 0)       # level_id
    put_u32(payload, 4, 1024)    # frames_per_column
    put_u64(payload, 8, 0)       # origin_frame
    put_u32(payload, 16, 1)      # logical_column_count
    put_u32(payload, 20, 1)      # span_count
    put_u64(payload, 24, 48)     # span_directory_offset
    put_u64(payload, 32, 80)     # column_data_offset
    put_u32(payload, 40, 4)      # APTA_FEATURE_FINAL

    put_u64(payload, 48, 0)      # first_frame
    put_u64(payload, 56, 1024)   # end_frame
    put_u32(payload, 64, 0)      # first_column_index
    put_u32(payload, 68, 1)      # column_count
    put_u32(payload, 72, 0)      # data_column_offset

    # One zero-amplitude valid packed waveform column.
    payload[89] = 1
    return bytes(payload)


def build_meta() -> bytes:
    # Deterministic CBOR:
    # {1: "external", 5: 1700000000, 6: h'010203', 7: "fixture"}
    return bytes.fromhex(
        "a4"
        "01" "68" "65787465726e616c"
        "05" "1a6553f100"
        "06" "43" "010203"
        "07" "67" "66697874757265"
    )


def get_u16(buffer: bytes, offset: int) -> int:
    return int.from_bytes(buffer[offset : offset + 2], "little", signed=False)


def get_u32(buffer: bytes, offset: int) -> int:
    return int.from_bytes(buffer[offset : offset + 4], "little", signed=False)


def get_u64(buffer: bytes, offset: int) -> int:
    return int.from_bytes(buffer[offset : offset + 8], "little", signed=False)


def validate_fixture(fixture: bytes) -> None:
    if len(fixture) != EXPECTED_SIZE:
        raise RuntimeError(
            f"fixture size mismatch: expected={EXPECTED_SIZE} actual={len(fixture)}"
        )
    if fixture[:4] != b"APTA":
        raise RuntimeError("fixture magic mismatch")

    header_size = get_u16(fixture, 4)
    section_count = get_u32(fixture, 20)
    directory_offset = get_u64(fixture, 24)
    declared_size = get_u64(fixture, 32)
    if header_size != CONTAINER_HEADER_SIZE:
        raise RuntimeError(f"unexpected container header size: {header_size}")
    if directory_offset != header_size:
        raise RuntimeError("directory does not immediately follow the header")
    if declared_size != len(fixture):
        raise RuntimeError(
            f"declared size mismatch: declared={declared_size} actual={len(fixture)}"
        )
    if get_u32(fixture, 92) != crc32c(fixture[:92]):
        raise RuntimeError("container header CRC32C mismatch")

    directory_end = directory_offset + section_count * DIRECTORY_ENTRY_SIZE
    if directory_end > len(fixture):
        raise RuntimeError("section directory extends beyond the fixture")
    previous_end = directory_end
    for index in range(section_count):
        entry_offset = directory_offset + index * DIRECTORY_ENTRY_SIZE
        entry = fixture[entry_offset : entry_offset + DIRECTORY_ENTRY_SIZE]
        section_offset = get_u64(entry, 8)
        stored_size = get_u64(entry, 16)
        logical_size = get_u64(entry, 24)
        section_end = section_offset + stored_size
        if (
            section_offset < directory_end
            or section_offset < previous_end
            or section_end > len(fixture)
        ):
            raise RuntimeError(f"invalid section range at directory index {index}")
        if stored_size != logical_size:
            raise RuntimeError(
                f"compressed sections are not expected at directory index {index}"
            )
        payload = fixture[section_offset:section_end]
        if get_u32(entry, 32) != crc32c(payload):
            raise RuntimeError(f"section CRC32C mismatch at directory index {index}")
        previous_end = section_end

    if previous_end != len(fixture):
        raise RuntimeError(
            f"fixture has {len(fixture) - previous_end} trailing undeclared bytes"
        )


def build_fixture() -> bytes:
    wovr = build_wovr()
    meta = build_meta()
    output = bytearray(EXPECTED_SIZE)

    output[0:4] = b"APTA"
    put_u16(output, 4, 96)
    put_u16(output, 6, 1)
    put_u16(output, 8, 0)
    put_u16(output, 10, 1)
    put_u32(output, 12, 0x00001000)  # APTA_API_VERSION_ENCODE(0, 1, 0)
    put_u32(output, 16, 0)
    put_u32(output, 20, 2)
    put_u64(output, 24, 96)
    put_u64(output, 32, EXPECTED_SIZE)
    put_u64(output, 40, 1024)
    put_u32(output, 48, 48000)
    put_u16(output, 52, 1)
    put_u16(output, 54, 1)

    wovr_entry = 96
    output[wovr_entry : wovr_entry + 4] = b"WOVR"
    put_u16(output, wovr_entry + 4, 1)
    put_u16(output, wovr_entry + 6, 1)
    put_u64(output, wovr_entry + 8, 176)
    put_u64(output, wovr_entry + 16, len(wovr))
    put_u64(output, wovr_entry + 24, len(wovr))
    put_u32(output, wovr_entry + 32, crc32c(wovr))

    meta_entry = 136
    output[meta_entry : meta_entry + 4] = b"META"
    put_u16(output, meta_entry + 4, 1)
    put_u16(output, meta_entry + 6, 0)
    put_u64(output, meta_entry + 8, 272)
    put_u64(output, meta_entry + 16, len(meta))
    put_u64(output, meta_entry + 24, len(meta))
    put_u32(output, meta_entry + 32, crc32c(meta))

    output[176 : 176 + len(wovr)] = wovr
    output[272 : 272 + len(meta)] = meta
    put_u32(output, 92, crc32c(bytes(output[:92])))

    fixture = bytes(output)
    validate_fixture(fixture)
    digest = hashlib.sha256(fixture).hexdigest()
    if digest != EXPECTED_SHA256:
        raise RuntimeError(
            f"fixture invariant failed: size={len(fixture)} sha256={digest}"
        )
    return fixture


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check-hex", type=Path)
    args = parser.parse_args()

    fixture = build_fixture()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(fixture)

    if args.check_hex is not None:
        expected_hex = "".join(args.check_hex.read_text(encoding="ascii").split())
        actual_hex = fixture.hex()
        if expected_hex != actual_hex:
            raise SystemExit("committed fixture hex does not match independent producer")

    print(f"{hashlib.sha256(fixture).hexdigest()}  {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
