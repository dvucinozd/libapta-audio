# SPDX-License-Identifier: Apache-2.0
"""Waveform section decoders for the independent P6 consumer."""

from __future__ import annotations

from typing import Any

from consumer_common import Decoder


def waveform_column(
    decoder: Decoder, base: int, path: str
) -> dict[str, Any]:
    return {
        "minimum": decoder.i16(base, f"{path}.minimum"),
        "maximum": decoder.i16(base + 2, f"{path}.maximum"),
        "rms": decoder.u16(base + 4, f"{path}.rms"),
        "low": decoder.raw(base + 6, 1, f"{path}.low")[0],
        "mid": decoder.raw(base + 7, 1, f"{path}.mid")[0],
        "high": decoder.raw(base + 8, 1, f"{path}.high")[0],
        "flags": decoder.raw(base + 9, 1, f"{path}.flags")[0],
    }


def parse_wovr(decoder: Decoder, base: int) -> dict[str, Any]:
    path = "sections.WOVR"
    span_count = decoder.u32(base + 20, f"{path}.span_count")
    span_offset = decoder.u64(
        base + 24, f"{path}.span_directory_offset"
    )
    column_offset = decoder.u64(
        base + 32, f"{path}.column_data_offset"
    )
    spans = []
    columns = []
    packed_index = 0
    for index in range(span_count):
        entry = base + span_offset + index * 32
        count = decoder.u32(
            entry + 20, f"{path}.spans[{index}].column_count"
        )
        spans.append(
            {
                "first_frame": decoder.u64(
                    entry, f"{path}.spans[{index}].first_frame"
                ),
                "end_frame": decoder.u64(
                    entry + 8, f"{path}.spans[{index}].end_frame"
                ),
                "first_column_index": decoder.u32(
                    entry + 16,
                    f"{path}.spans[{index}].first_column_index",
                ),
                "column_count": count,
                "data_column_offset": decoder.u32(
                    entry + 24,
                    f"{path}.spans[{index}].data_column_offset",
                ),
                "reserved": decoder.u32(
                    entry + 28, f"{path}.spans[{index}].reserved"
                ),
            }
        )
        for _ in range(count):
            columns.append(
                waveform_column(
                    decoder,
                    base + column_offset + packed_index * 10,
                    f"{path}.columns[{packed_index}]",
                )
            )
            packed_index += 1
    return {
        "level_id": decoder.u32(base, f"{path}.level_id"),
        "frames_per_column": decoder.u32(
            base + 4, f"{path}.frames_per_column"
        ),
        "origin_frame": decoder.u64(base + 8, f"{path}.origin_frame"),
        "logical_column_count": decoder.u32(
            base + 16, f"{path}.logical_column_count"
        ),
        "span_count": span_count,
        "span_directory_offset": span_offset,
        "column_data_offset": column_offset,
        "flags": decoder.u32(base + 40, f"{path}.flags"),
        "reserved": decoder.u32(base + 44, f"{path}.reserved"),
        "spans": spans,
        "columns": columns,
    }


def parse_wdtl(decoder: Decoder, base: int) -> dict[str, Any]:
    path = "sections.WDTL"
    tile_count = decoder.u32(base, f"{path}.tile_count")
    directory_offset = decoder.u64(
        base + 8, f"{path}.tile_directory_offset"
    )
    tiles = []
    for index in range(tile_count):
        entry = base + directory_offset + index * 48
        count = decoder.u32(
            entry + 28, f"{path}.tiles[{index}].column_count"
        )
        columns_offset = decoder.u64(
            entry + 32, f"{path}.tiles[{index}].columns_offset"
        )
        tiles.append(
            {
                "level_id": decoder.u32(
                    entry, f"{path}.tiles[{index}].level_id"
                ),
                "tile_index": decoder.u32(
                    entry + 4, f"{path}.tiles[{index}].tile_index"
                ),
                "first_frame": decoder.u64(
                    entry + 8, f"{path}.tiles[{index}].first_frame"
                ),
                "end_frame": decoder.u64(
                    entry + 16, f"{path}.tiles[{index}].end_frame"
                ),
                "first_column_index": decoder.u32(
                    entry + 24,
                    f"{path}.tiles[{index}].first_column_index",
                ),
                "column_count": count,
                "columns_offset": columns_offset,
                "feature_state": decoder.u32(
                    entry + 40,
                    f"{path}.tiles[{index}].feature_state",
                ),
                "flags": decoder.u16(
                    entry + 44, f"{path}.tiles[{index}].flags"
                ),
                "confidence": decoder.raw(
                    entry + 46,
                    1,
                    f"{path}.tiles[{index}].confidence",
                )[0],
                "reserved": decoder.raw(
                    entry + 47,
                    1,
                    f"{path}.tiles[{index}].reserved",
                )[0],
                "columns": [
                    waveform_column(
                        decoder,
                        base + columns_offset + column * 10,
                        f"{path}.tiles[{index}].columns[{column}]",
                    )
                    for column in range(count)
                ],
            }
        )
    return {
        "tile_count": tile_count,
        "flags": decoder.u32(base + 4, f"{path}.flags"),
        "tile_directory_offset": directory_offset,
        "tiles": tiles,
    }
