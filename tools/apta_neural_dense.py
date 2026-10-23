#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Strict private f32 trace codec and source mapping, no neural dependencies."""
import math
import struct

MAGIC = b"APTADN01"
HEADER = struct.Struct("<8sII")
ROW = struct.Struct("<ff")
MAX_ROWS = 1501
MAX_TRACE_BYTES = 131072
SOURCE_HOP = 960


def encode(beat, downbeat):
    if len(beat) != len(downbeat) or not 0 < len(beat) <= MAX_ROWS:
        raise ValueError("invalid dense row count")
    output = bytearray(HEADER.pack(MAGIC, len(beat), 2))
    for a, b in zip(beat, downbeat):
        if not math.isfinite(a) or not math.isfinite(b):
            raise ValueError("nonfinite dense evidence")
        try:
            output.extend(ROW.pack(a, b))
        except (OverflowError, struct.error) as exc:
            raise ValueError("invalid f32 evidence") from exc
    if len(output) > MAX_TRACE_BYTES:
        raise ValueError("dense payload exceeds cap")
    return bytes(output)


def decode(payload):
    if not HEADER.size <= len(payload) <= MAX_TRACE_BYTES:
        raise ValueError("invalid dense payload size")
    magic, rows, channels = HEADER.unpack_from(payload)
    if magic != MAGIC or channels != 2 or not 0 < rows <= MAX_ROWS or len(payload) != HEADER.size + ROW.size * rows:
        raise ValueError("dense shape/framing mismatch")
    pairs = list(ROW.iter_unpack(payload[HEADER.size:]))
    if any(not math.isfinite(v) for pair in pairs for v in pair):
        raise ValueError("nonfinite saved evidence")
    beat, downbeat = [p[0] for p in pairs], [p[1] for p in pairs]
    if encode(beat, downbeat) != payload:
        raise ValueError("f32 bit pattern changed")
    return beat, downbeat


def coverage(rows, starts, chunk_lengths, chunk_size=1500, border=6):
    if (type(rows) is not int or not 0 < rows <= MAX_ROWS or not starts
            or len(starts) != len(chunk_lengths) or chunk_size != 1500 or border != 6):
        raise ValueError("invalid aggregation geometry")
    counts, ranges = [0] * rows, []
    previous = None
    for start, length in zip(starts, chunk_lengths):
        if (type(start) is not int or type(length) is not int or not 2 * border < length <= chunk_size
                or previous is not None and start < previous):
            raise ValueError("invalid upstream chunk schedule")
        previous = start
        first, end = start + border, start + border + length - 2 * border
        if not 0 <= first < end <= rows or end != min(start + chunk_size - border, rows):
            raise ValueError("aggregation assignment geometry differs")
        ranges.append([first, end])
        for i in range(first, end):
            counts[i] += 1
    if not all(counts):
        raise ValueError("unassigned dense row")
    return {"assigned_rows": rows, "minimum_assignments": min(counts), "maximum_assignments": max(counts),
            "assignment_ranges": ranges, "overlap_mode": "keep_first"}


def mapping(total, mono_length, rows, windows):
    if (type(total) is not int or not 0 < total <= 1440000 or type(mono_length) is not int
            or not total * 22050 // 48000 <= mono_length <= (total * 22050 + 47999) // 48000
            or type(rows) is not int or rows != mono_length // 441 + 1 or not 0 < rows <= MAX_ROWS
            or not 0 < len(windows) <= 8):
        raise ValueError("source/resampled/dense geometry mismatch")
    ownership, previous, covered = [], 0, []
    for window in windows:
        if set(window) != {"first_frame", "end_frame"}:
            raise ValueError("plain source geometry required")
        first, end = window["first_frame"], window["end_frame"]
        if type(first) is not int or type(end) is not int or first != previous or not first < end <= total:
            raise ValueError("source partition incomplete")
        a, b = min(rows, (first + 959) // 960), min(rows, (end + 959) // 960)
        ownership.append(dict(window, first_row=a, end_row=b))
        covered.extend(range(a, b))
        previous = end
    if previous != total:
        raise ValueError("source tail missing")
    endpoint = [i for i in range(rows) if i * SOURCE_HOP == total]
    beyond = [i for i in range(rows) if i * SOURCE_HOP > total]
    if sorted(covered + endpoint + beyond) != list(range(rows)) or len(set(covered)) != len(covered):
        raise ValueError("rows lost or duplicated across source/window boundaries")
    return {"source_sample_rate": 48000, "mono_sample_rate": 22050, "mono_samples": mono_length,
            "rows": rows, "center_origin_frame": 0, "center_hop_source_frames": SOURCE_HOP,
            "center_fps": 50, "centered_features": True, "causal_availability": False,
            "windows": ownership, "exclusive_endpoint_rows": endpoint, "beyond_source_rows": beyond}


def statistics(beat, downbeat):
    if len(beat) != len(downbeat) or not beat or any(not math.isfinite(x) for x in (*beat, *downbeat)):
        raise ValueError("invalid dense statistics input")
    return {name: {"values": len(values), "positive_logits": sum(x > 0 for x in values),
                   "nonpositive_logits": sum(x <= 0 for x in values), "minimum": min(values), "maximum": max(values)}
            for name, values in (("beat", beat), ("downbeat", downbeat))}
