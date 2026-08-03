#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate and verify the canonical APTA container-version-1 fixture suite."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Callable, Iterable

HEADER_SIZE = 96
DIRECTORY_ENTRY_SIZE = 40
CONTAINER_VERSION = 1
SPECIFICATION_MAJOR = 1
SPECIFICATION_MINOR = 0
PRODUCER_API_VERSION = 1 << 22
TOTAL_SOURCE_FRAMES = 1024
SOURCE_SAMPLE_RATE = 48000
SOURCE_CHANNEL_COUNT = 1
SOURCE_CHANNEL_LAYOUT_MONO = 1

SECTION_REQUIRED = 1 << 0
FEATURE_FINAL = 4
CONFIDENCE = 90
TEMPO_MILLIBPM = 120000
FRAMES_PER_BEAT = 24000

CANONICAL_SECTION_ORDER = ("WOVR", "WDTL", "META", "TEMP", "LGRD", "GGRD", "REVN")

FIXTURE_DEFINITIONS = (
    ("v1-wovr-only", ("WOVR",)),
    ("v1-wovr-meta", ("WOVR", "META")),
    ("v1-wovr-wdtl", ("WOVR", "WDTL")),
    ("v1-wovr-temp", ("WOVR", "TEMP")),
    ("v1-wovr-temp-lgrd", ("WOVR", "TEMP", "LGRD")),
    ("v1-wovr-temp-ggrd-revn", ("WOVR", "TEMP", "GGRD", "REVN")),
    (
        "v1-all-standard-sections",
        ("WOVR", "WDTL", "META", "TEMP", "LGRD", "GGRD", "REVN"),
    ),
)


def align8(value: int) -> int:
    return (value + 7) & ~7


def put_u16(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buffer, offset, value)


def put_u32(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buffer, offset, value)


def put_u64(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<Q", buffer, offset, value)


def get_u16(buffer: bytes, offset: int) -> int:
    return struct.unpack_from("<H", buffer, offset)[0]


def get_u32(buffer: bytes, offset: int) -> int:
    return struct.unpack_from("<I", buffer, offset)[0]


def get_u64(buffer: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", buffer, offset)[0]


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if (crc & 1) else 0)
    return crc ^ 0xFFFFFFFF


def build_wovr() -> bytes:
    payload = bytearray(48 + 32 + 10)
    put_u32(payload, 0, 0)
    put_u32(payload, 4, 1024)
    put_u64(payload, 8, 0)
    put_u32(payload, 16, 1)
    put_u32(payload, 20, 1)
    put_u64(payload, 24, 48)
    put_u64(payload, 32, 80)
    put_u32(payload, 40, FEATURE_FINAL)

    put_u64(payload, 48, 0)
    put_u64(payload, 56, TOTAL_SOURCE_FRAMES)
    put_u32(payload, 64, 0)
    put_u32(payload, 68, 1)
    put_u32(payload, 72, 0)

    payload[89] = 1  # APTA_WAVEFORM_COLUMN_VALID
    return bytes(payload)


def build_wdtl() -> bytes:
    payload = bytearray(16 + 48 + 10)
    put_u32(payload, 0, 1)
    put_u64(payload, 8, 16)

    descriptor = 16
    put_u32(payload, descriptor + 0, 1)
    put_u32(payload, descriptor + 4, 0)
    put_u64(payload, descriptor + 8, 0)
    put_u64(payload, descriptor + 16, 256)
    put_u32(payload, descriptor + 24, 0)
    put_u32(payload, descriptor + 28, 1)
    put_u64(payload, descriptor + 32, 64)
    put_u32(payload, descriptor + 40, FEATURE_FINAL)
    payload[descriptor + 46] = CONFIDENCE

    payload[73] = 1  # APTA_WAVEFORM_COLUMN_VALID
    return bytes(payload)


def build_meta() -> bytes:
    # Deterministic CBOR map: {1: "suite"}
    return b"\xa1\x01\x65suite"


def build_temp() -> bytes:
    payload = bytearray(56 + 16)
    put_u16(payload, 0, 1)
    payload[2] = FEATURE_FINAL
    payload[3] = CONFIDENCE
    put_u32(payload, 8, TEMPO_MILLIBPM)
    put_u32(payload, 12, 1)
    put_u64(payload, 16, 0)
    put_u64(payload, 24, TOTAL_SOURCE_FRAMES)
    put_u64(payload, 32, 0)
    put_u64(payload, 40, TOTAL_SOURCE_FRAMES)
    put_u32(payload, 48, 1)

    candidate = 56
    put_u32(payload, candidate + 0, TEMPO_MILLIBPM)
    put_u16(payload, candidate + 4, 1000)
    payload[candidate + 6] = CONFIDENCE
    payload[candidate + 7] = 0
    return bytes(payload)


def build_lgrd() -> bytes:
    payload = bytearray(144)
    put_u16(payload, 0, 1)
    payload[2] = FEATURE_FINAL
    payload[3] = CONFIDENCE
    put_u32(payload, 8, 1)  # SEGMENTS
    put_u32(payload, 12, 1)
    for offset in (16, 32, 48, 64):
        put_u64(payload, offset, 0)
        put_u64(payload, offset + 8, TOTAL_SOURCE_FRAMES)
    put_u64(payload, 80, 0)
    put_u64(payload, 96, 0)
    put_u64(payload, 104, FRAMES_PER_BEAT)
    put_u32(payload, 116, 1)
    put_u32(payload, 120, TEMPO_MILLIBPM)
    put_u32(payload, 124, 1)
    put_u32(payload, 128, 0)
    payload[136] = FEATURE_FINAL
    payload[137] = CONFIDENCE
    return bytes(payload)


def build_ggrd() -> bytes:
    payload = bytearray(96 + 80)
    put_u16(payload, 0, 1)
    payload[2] = FEATURE_FINAL
    payload[3] = CONFIDENCE
    put_u32(payload, 8, 1)  # SEGMENTS
    put_u32(payload, 12, 1)
    put_u32(payload, 16, 1)
    put_u32(payload, 20, 0)
    for offset in (24, 40, 56, 72):
        put_u64(payload, offset, 0)
        put_u64(payload, offset + 8, TOTAL_SOURCE_FRAMES)

    segment = 96
    put_u64(payload, segment + 0, 0)
    put_u64(payload, segment + 8, TOTAL_SOURCE_FRAMES)
    put_u64(payload, segment + 16, 0)
    put_u64(payload, segment + 32, 0)
    put_u64(payload, segment + 40, FRAMES_PER_BEAT)
    put_u32(payload, segment + 52, 1)
    put_u32(payload, segment + 56, TEMPO_MILLIBPM)
    put_u32(payload, segment + 60, 2)
    put_u32(payload, segment + 64, 1)
    payload[segment + 72] = FEATURE_FINAL
    payload[segment + 73] = CONFIDENCE
    return bytes(payload)


def build_revn() -> bytes:
    payload = bytearray(80)
    put_u16(payload, 0, 1)
    payload[2] = 2  # APTA_GRID_REVISION_APPLIED
    payload[3] = CONFIDENCE
    put_u32(payload, 8, 1)
    put_u32(payload, 16, 1)  # SEGMENTS
    put_u32(payload, 20, 1)
    put_u32(payload, 24, 0)
    put_u64(payload, 32, 0)
    put_u64(payload, 40, TOTAL_SOURCE_FRAMES)
    return bytes(payload)


PAYLOAD_BUILDERS: dict[str, Callable[[], bytes]] = {
    "WOVR": build_wovr,
    "WDTL": build_wdtl,
    "META": build_meta,
    "TEMP": build_temp,
    "LGRD": build_lgrd,
    "GGRD": build_ggrd,
    "REVN": build_revn,
}


def validate_section_combination(sections: Iterable[str]) -> tuple[str, ...]:
    ordered = tuple(sections)
    if not ordered or ordered[0] != "WOVR":
        raise RuntimeError("every fixture must start with WOVR")
    if len(set(ordered)) != len(ordered):
        raise RuntimeError(f"duplicate section in {ordered}")
    if tuple(sorted(ordered, key=CANONICAL_SECTION_ORDER.index)) != ordered:
        raise RuntimeError(f"non-canonical order: {ordered}")
    if "LGRD" in ordered and "TEMP" not in ordered:
        raise RuntimeError("LGRD requires TEMP")
    if ("GGRD" in ordered) != ("REVN" in ordered):
        raise RuntimeError("GGRD and REVN must appear as a pair")
    if "GGRD" in ordered:
        if "TEMP" not in ordered:
            raise RuntimeError("GGRD requires TEMP")
        if ordered.index("REVN") != ordered.index("GGRD") + 1:
            raise RuntimeError("REVN must immediately follow GGRD")
    return ordered


def build_container(sections: Iterable[str]) -> bytes:
    ordered = validate_section_combination(sections)
    payloads = [(fourcc, PAYLOAD_BUILDERS[fourcc]()) for fourcc in ordered]

    directory_offset = HEADER_SIZE
    cursor = align8(HEADER_SIZE + len(payloads) * DIRECTORY_ENTRY_SIZE)
    layout: list[tuple[str, bytes, int]] = []
    for fourcc, payload in payloads:
        layout.append((fourcc, payload, cursor))
        cursor += len(payload)
        if fourcc != payloads[-1][0]:
            cursor = align8(cursor)

    output = bytearray(cursor)
    output[0:4] = b"APTA"
    put_u16(output, 4, HEADER_SIZE)
    put_u16(output, 6, CONTAINER_VERSION)
    put_u16(output, 8, SPECIFICATION_MAJOR)
    put_u16(output, 10, SPECIFICATION_MINOR)
    put_u32(output, 12, PRODUCER_API_VERSION)
    put_u32(output, 16, 0)
    put_u32(output, 20, len(layout))
    put_u64(output, 24, directory_offset)
    put_u64(output, 32, len(output))
    put_u64(output, 40, TOTAL_SOURCE_FRAMES)
    put_u32(output, 48, SOURCE_SAMPLE_RATE)
    put_u16(output, 52, SOURCE_CHANNEL_COUNT)
    put_u16(output, 54, SOURCE_CHANNEL_LAYOUT_MONO)

    for index, (fourcc, payload, payload_offset) in enumerate(layout):
        entry = directory_offset + index * DIRECTORY_ENTRY_SIZE
        output[entry : entry + 4] = fourcc.encode("ascii")
        put_u16(output, entry + 4, 1)
        put_u16(output, entry + 6, SECTION_REQUIRED if fourcc == "WOVR" else 0)
        put_u64(output, entry + 8, payload_offset)
        put_u64(output, entry + 16, len(payload))
        put_u64(output, entry + 24, len(payload))
        put_u32(output, entry + 32, crc32c(payload))
        output[payload_offset : payload_offset + len(payload)] = payload

    put_u32(output, 92, crc32c(bytes(output[:92])))
    fixture = bytes(output)
    validate_container(fixture, ordered)
    return fixture


def validate_container(fixture: bytes, expected_sections: tuple[str, ...]) -> None:
    if len(fixture) < HEADER_SIZE or fixture[:4] != b"APTA":
        raise RuntimeError("invalid fixed header")
    if get_u16(fixture, 4) != HEADER_SIZE:
        raise RuntimeError("unexpected header size")
    if get_u16(fixture, 6) != CONTAINER_VERSION:
        raise RuntimeError("unexpected container version")
    if get_u32(fixture, 20) != len(expected_sections):
        raise RuntimeError("section count mismatch")
    if get_u64(fixture, 24) != HEADER_SIZE:
        raise RuntimeError("directory offset mismatch")
    if get_u64(fixture, 32) != len(fixture):
        raise RuntimeError("total size mismatch")
    if get_u32(fixture, 92) != crc32c(fixture[:92]):
        raise RuntimeError("header CRC32C mismatch")

    previous_end = align8(HEADER_SIZE + len(expected_sections) * DIRECTORY_ENTRY_SIZE)
    for index, expected_fourcc in enumerate(expected_sections):
        entry = HEADER_SIZE + index * DIRECTORY_ENTRY_SIZE
        fourcc = fixture[entry : entry + 4].decode("ascii")
        if fourcc != expected_fourcc:
            raise RuntimeError(f"directory order mismatch: {fourcc} != {expected_fourcc}")
        if get_u16(fixture, entry + 4) != 1:
            raise RuntimeError(f"unexpected {fourcc} section version")
        expected_flags = SECTION_REQUIRED if fourcc == "WOVR" else 0
        if get_u16(fixture, entry + 6) != expected_flags:
            raise RuntimeError(f"unexpected {fourcc} section flags")
        offset = get_u64(fixture, entry + 8)
        stored_size = get_u64(fixture, entry + 16)
        logical_size = get_u64(fixture, entry + 24)
        if offset % 8 != 0 or offset < previous_end:
            raise RuntimeError(f"invalid {fourcc} payload offset")
        if stored_size != logical_size or offset + stored_size > len(fixture):
            raise RuntimeError(f"invalid {fourcc} payload size")
        payload = fixture[offset : offset + stored_size]
        if get_u32(fixture, entry + 32) != crc32c(payload):
            raise RuntimeError(f"{fourcc} CRC32C mismatch")
        if get_u32(fixture, entry + 36) != 0:
            raise RuntimeError(f"{fourcc} directory reserved value is non-zero")
        previous_end = offset + stored_size

    if previous_end != len(fixture):
        raise RuntimeError("fixture has trailing undeclared bytes")


def wrap_hex(data: bytes, width: int = 96) -> str:
    encoded = data.hex()
    return "\n".join(encoded[index : index + width] for index in range(0, len(encoded), width)) + "\n"


def build_manifest(fixtures: dict[str, tuple[tuple[str, ...], bytes]]) -> dict[str, object]:
    rows = []
    for name, (sections, fixture) in fixtures.items():
        rows.append(
            {
                "name": name,
                "sections": list(sections),
                "section_versions": [1 for _ in sections],
                "canonical": True,
                "expected_strict": "ok",
                "expected_permissive": "ok",
                "size_bytes": len(fixture),
                "sha256": hashlib.sha256(fixture).hexdigest(),
                "hex_path": f"tests/fixtures/container-v1-suite/{name}.apta.hex",
            }
        )
    return {
        "manifest_version": "apta-container-v1-fixture-suite-1",
        "container_version": CONTAINER_VERSION,
        "specification_version": "1.0",
        "producer_api_version": "1.0.0",
        "producer": "independent-python-container-v1-suite-generator",
        "generator_path": "tests/fixtures/generate_container_v1_suite.py",
        "consumer_path": "tests/unit/external_fixture.c",
        "fixtures": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--check-hex-dir", type=Path)
    parser.add_argument("--check-manifest", type=Path)
    parser.add_argument("--write-hex-dir", type=Path)
    parser.add_argument("--write-manifest", type=Path)
    args = parser.parse_args()

    fixtures: dict[str, tuple[tuple[str, ...], bytes]] = {}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, sections in FIXTURE_DEFINITIONS:
        ordered = validate_section_combination(sections)
        fixture = build_container(ordered)
        fixtures[name] = (ordered, fixture)
        (args.output_dir / f"{name}.apta").write_bytes(fixture)

    manifest = build_manifest(fixtures)
    manifest_text = json.dumps(manifest, indent=2) + "\n"

    if args.check_hex_dir is not None:
        for name, (_, fixture) in fixtures.items():
            path = args.check_hex_dir / f"{name}.apta.hex"
            expected = "".join(path.read_text(encoding="ascii").split())
            if expected != fixture.hex():
                raise SystemExit(f"committed fixture hex mismatch: {path}")

    if args.check_manifest is not None:
        committed = json.loads(args.check_manifest.read_text(encoding="utf-8"))
        if committed != manifest:
            raise SystemExit(f"fixture manifest mismatch: {args.check_manifest}")

    if args.write_hex_dir is not None:
        args.write_hex_dir.mkdir(parents=True, exist_ok=True)
        for name, (_, fixture) in fixtures.items():
            (args.write_hex_dir / f"{name}.apta.hex").write_text(
                wrap_hex(fixture), encoding="ascii"
            )

    if args.write_manifest is not None:
        args.write_manifest.parent.mkdir(parents=True, exist_ok=True)
        args.write_manifest.write_text(manifest_text, encoding="utf-8")

    for name, (_, fixture) in fixtures.items():
        print(f"{hashlib.sha256(fixture).hexdigest()}  {name}.apta")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
