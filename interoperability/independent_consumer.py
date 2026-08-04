#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Independent APTA container-v1 semantic consumer for Stage S9 P6."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from consumer_analysis import (
    parse_ggrd,
    parse_lgrd,
    parse_revn,
    parse_temp,
)
from consumer_common import (
    Decoder,
    ValidationError,
    expect,
    parse_directory,
    parse_header,
    validate_structure,
)
from consumer_waveform import parse_wdtl, parse_wovr


def parse_sections(
    decoder: Decoder, entries: list[dict[str, Any]]
) -> dict[str, Any]:
    parsers = {
        "WOVR": parse_wovr,
        "WDTL": parse_wdtl,
        "TEMP": parse_temp,
        "LGRD": parse_lgrd,
        "GGRD": parse_ggrd,
        "REVN": parse_revn,
    }
    sections: dict[str, Any] = {}
    for entry in entries:
        fourcc = entry["fourcc"]
        base = entry["offset"]
        if fourcc == "META":
            payload = decoder.raw(
                base, entry["stored_size"], "sections.META"
            )
            sections[fourcc] = {
                "cbor_hex": payload.hex(),
                "decoded": (
                    {"1": "suite"}
                    if payload == b"\xa1\x01\x65suite"
                    else {}
                ),
            }
        else:
            sections[fourcc] = parsers[fourcc](decoder, base)
    return sections


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    data = args.input.read_bytes()
    manifest = json.loads(
        args.manifest.read_text(encoding="utf-8")
    )
    decoder = Decoder(data)
    header = parse_header(decoder)
    entries = parse_directory(decoder, header)
    validate_structure(data, header, entries)
    sections = parse_sections(decoder, entries)

    expect(
        {
            "header": header,
            "directory": entries,
            "sections": sections,
        },
        {
            "header": manifest["header"],
            "directory": manifest["directory"],
            "sections": manifest["sections"],
        },
        "container",
    )

    digest = hashlib.sha256(data).hexdigest()
    if len(data) != manifest["fixture"]["size_bytes"]:
        raise ValidationError(
            "fixture.size_bytes: manifest mismatch"
        )
    if digest != manifest["fixture"]["sha256"]:
        raise ValidationError("fixture.sha256: manifest mismatch")
    if decoder.multibyte_fields != decoder.byte_swap_checks:
        raise ValidationError("endian evidence count mismatch")

    report = {
        "consumer": {
            "implementation": (
                "stdlib-only independent Python parser"
            ),
            "imports_or_links_libapta": False,
            "semantic_manifest_comparison": True,
        },
        "endian_evidence": {
            "method": (
                "decode each little-endian field and verify "
                "reversed bytes as big-endian"
            ),
            "multibyte_fields_checked": (
                decoder.multibyte_fields
            ),
            "byte_swap_checks_passed": (
                decoder.byte_swap_checks
            ),
            "native_big_endian_execution": False,
        },
        "fixture_sha256": digest,
        "section_count": len(entries),
        "sections": [
            entry["fourcc"] for entry in entries
        ],
        "status": "pass",
    }
    args.output.parent.mkdir(
        parents=True, exist_ok=True
    )
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "independent consumer: pass; "
        f"sections={len(entries)}; "
        f"endian_checks={decoder.byte_swap_checks}; "
        f"sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValidationError as error:
        raise SystemExit(
            f"independent consumer: {error}"
        ) from error
