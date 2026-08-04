# SPDX-License-Identifier: Apache-2.0
"""Shared primitives for the independent APTA container-v1 consumer."""

from __future__ import annotations

from typing import Any

HEADER_SIZE = 96
ENTRY_SIZE = 40
CANONICAL_ORDER = ("WOVR", "WDTL", "META", "TEMP", "LGRD", "GGRD", "REVN")


class ValidationError(RuntimeError):
    pass


class Decoder:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.multibyte_fields = 0
        self.byte_swap_checks = 0

    def raw(self, offset: int, size: int, path: str) -> bytes:
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValidationError(
                f"{path}: range {offset}+{size} exceeds {len(self.data)} bytes"
            )
        return self.data[offset : offset + size]

    def _integer(self, offset: int, size: int, signed: bool, path: str) -> int:
        raw = self.raw(offset, size, path)
        self.multibyte_fields += 1
        value = int.from_bytes(raw, "little", signed=signed)
        swapped = int.from_bytes(raw[::-1], "big", signed=signed)
        if swapped != value:
            raise ValidationError(f"{path}: deterministic byte-swap decode mismatch")
        self.byte_swap_checks += 1
        return value

    def u16(self, offset: int, path: str) -> int:
        return self._integer(offset, 2, False, path)

    def i16(self, offset: int, path: str) -> int:
        return self._integer(offset, 2, True, path)

    def u32(self, offset: int, path: str) -> int:
        return self._integer(offset, 4, False, path)

    def u64(self, offset: int, path: str) -> int:
        return self._integer(offset, 8, False, path)

    def i64(self, offset: int, path: str) -> int:
        return self._integer(offset, 8, True, path)


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


def expect(actual: Any, expected: Any, path: str) -> None:
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            raise ValidationError(
                f"{path}: expected object, got {type(actual).__name__}"
            )
        if set(actual) != set(expected):
            missing = sorted(set(expected) - set(actual))
            extra = sorted(set(actual) - set(expected))
            raise ValidationError(
                f"{path}: key mismatch; missing={missing}; extra={extra}"
            )
        for key in sorted(expected):
            expect(actual[key], expected[key], f"{path}.{key}")
        return
    if isinstance(expected, list):
        if not isinstance(actual, list) or len(actual) != len(expected):
            actual_size = len(actual) if isinstance(actual, list) else "not-list"
            raise ValidationError(
                f"{path}: list length mismatch; "
                f"expected={len(expected)} actual={actual_size}"
            )
        for index, value in enumerate(expected):
            expect(actual[index], value, f"{path}[{index}]")
        return
    if actual != expected:
        raise ValidationError(f"{path}: expected={expected!r}, actual={actual!r}")


def parse_header(decoder: Decoder) -> dict[str, Any]:
    return {
        "magic": decoder.raw(0, 4, "header.magic").decode("ascii"),
        "header_size": decoder.u16(4, "header.header_size"),
        "container_version": decoder.u16(6, "header.container_version"),
        "spec_major": decoder.u16(8, "header.spec_major"),
        "spec_minor": decoder.u16(10, "header.spec_minor"),
        "producer_api_version": decoder.u32(12, "header.producer_api_version"),
        "flags": decoder.u32(16, "header.flags"),
        "section_count": decoder.u32(20, "header.section_count"),
        "dir_offset": decoder.u64(24, "header.dir_offset"),
        "total_file_size": decoder.u64(32, "header.total_file_size"),
        "total_source_frames": decoder.u64(40, "header.total_source_frames"),
        "sample_rate": decoder.u32(48, "header.sample_rate"),
        "channel_count": decoder.u16(52, "header.channel_count"),
        "channel_layout": decoder.u16(54, "header.channel_layout"),
        "fingerprint": decoder.raw(56, 32, "header.fingerprint").hex(),
        "fingerprint_kind": decoder.u32(88, "header.fingerprint_kind"),
        "crc": decoder.u32(92, "header.crc"),
    }


def parse_directory(
    decoder: Decoder, header: dict[str, Any]
) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for index in range(header["section_count"]):
        offset = header["dir_offset"] + index * ENTRY_SIZE
        path = f"directory[{index}]"
        entries.append(
            {
                "fourcc": decoder.raw(
                    offset, 4, f"{path}.fourcc"
                ).decode("ascii"),
                "version": decoder.u16(offset + 4, f"{path}.version"),
                "flags": decoder.u16(offset + 6, f"{path}.flags"),
                "offset": decoder.u64(offset + 8, f"{path}.offset"),
                "stored_size": decoder.u64(
                    offset + 16, f"{path}.stored_size"
                ),
                "logical_size": decoder.u64(
                    offset + 24, f"{path}.logical_size"
                ),
                "crc32c": decoder.u32(offset + 32, f"{path}.crc32c"),
                "reserved": decoder.u32(offset + 36, f"{path}.reserved"),
            }
        )
    return entries


def validate_structure(
    data: bytes,
    header: dict[str, Any],
    entries: list[dict[str, Any]],
) -> None:
    if header["magic"] != "APTA":
        raise ValidationError("header.magic: expected 'APTA'")
    if header["header_size"] != HEADER_SIZE:
        raise ValidationError("header.header_size: canonical value must be 96")
    if header["container_version"] != 1:
        raise ValidationError("header.container_version: expected 1")
    if header["total_file_size"] != len(data):
        raise ValidationError("header.total_file_size: differs from actual size")
    if header["crc"] != crc32c(data[:92]):
        raise ValidationError("header.crc: CRC32C mismatch")
    if header["dir_offset"] != HEADER_SIZE or header["dir_offset"] % 8:
        raise ValidationError(
            "header.dir_offset: non-canonical directory position"
        )
    directory_end = header["dir_offset"] + len(entries) * ENTRY_SIZE
    if directory_end > len(data):
        raise ValidationError("directory: exceeds file")
    if [entry["fourcc"] for entry in entries] != list(CANONICAL_ORDER):
        raise ValidationError("directory: canonical section order mismatch")
    previous_end = (directory_end + 7) & ~7
    for index, entry in enumerate(entries):
        path = f"directory[{index}]"
        if entry["version"] != 1:
            raise ValidationError(f"{path}.version: expected 1")
        expected_flags = 1 if entry["fourcc"] == "WOVR" else 0
        if entry["flags"] != expected_flags:
            raise ValidationError(
                f"{path}.flags: expected {expected_flags}"
            )
        if entry["reserved"] != 0:
            raise ValidationError(f"{path}.reserved: expected zero")
        if entry["stored_size"] != entry["logical_size"]:
            raise ValidationError(
                f"{path}: stored/logical size mismatch"
            )
        if entry["offset"] % 8:
            raise ValidationError(f"{path}.offset: not 8-byte aligned")
        if entry["offset"] < previous_end:
            raise ValidationError(
                f"{path}.offset: overlap or non-canonical placement"
            )
        if any(data[previous_end : entry["offset"]]):
            raise ValidationError(
                f"{path}: non-zero inter-section padding"
            )
        end = entry["offset"] + entry["stored_size"]
        if end > len(data):
            raise ValidationError(f"{path}: payload exceeds file")
        payload = data[entry["offset"] : end]
        if entry["crc32c"] != crc32c(payload):
            raise ValidationError(f"{path}.crc32c: payload CRC mismatch")
        previous_end = end
    if previous_end != len(data):
        raise ValidationError("container: trailing undeclared bytes")
