# SPDX-License-Identifier: Apache-2.0
"""Tempo, grid and revision decoders for the independent P6 consumer."""

from __future__ import annotations

from typing import Any

from consumer_common import Decoder


def parse_temp(decoder: Decoder, base: int) -> dict[str, Any]:
    path = "sections.TEMP"
    count = decoder.u32(base + 48, f"{path}.candidate_count")
    candidates = []
    for index in range(count):
        entry = base + 56 + index * 16
        candidates.append(
            {
                "tempo_millibpm": decoder.u32(
                    entry,
                    f"{path}.candidates[{index}].tempo_millibpm",
                ),
                "score": decoder.u16(
                    entry + 4, f"{path}.candidates[{index}].score"
                ),
                "confidence": decoder.raw(
                    entry + 6,
                    1,
                    f"{path}.candidates[{index}].confidence",
                )[0],
                "relation_to_selected": decoder.raw(
                    entry + 7,
                    1,
                    f"{path}.candidates[{index}].relation_to_selected",
                )[0],
                "flags": decoder.u32(
                    entry + 8, f"{path}.candidates[{index}].flags"
                ),
                "reserved": decoder.u32(
                    entry + 12,
                    f"{path}.candidates[{index}].reserved",
                ),
            }
        )
    return {
        "payload_version": decoder.u16(
            base, f"{path}.payload_version"
        ),
        "feature_state": decoder.raw(
            base + 2, 1, f"{path}.feature_state"
        )[0],
        "confidence": decoder.raw(
            base + 3, 1, f"{path}.confidence"
        )[0],
        "tempo_flags": decoder.u32(base + 4, f"{path}.tempo_flags"),
        "tempo_millibpm": decoder.u32(
            base + 8, f"{path}.tempo_millibpm"
        ),
        "candidate_set_id": decoder.u32(
            base + 12, f"{path}.candidate_set_id"
        ),
        "evidence_first_frame": decoder.u64(
            base + 16, f"{path}.evidence_first_frame"
        ),
        "evidence_end_frame": decoder.u64(
            base + 24, f"{path}.evidence_end_frame"
        ),
        "applicability_first_frame": decoder.u64(
            base + 32, f"{path}.applicability_first_frame"
        ),
        "applicability_end_frame": decoder.u64(
            base + 40, f"{path}.applicability_end_frame"
        ),
        "candidate_count": count,
        "reserved": decoder.u32(base + 52, f"{path}.reserved"),
        "candidates": candidates,
    }


def parse_lgrd(decoder: Decoder, base: int) -> dict[str, Any]:
    path = "sections.LGRD"
    return {
        "payload_version": decoder.u16(
            base, f"{path}.payload_version"
        ),
        "grid_state": decoder.raw(
            base + 2, 1, f"{path}.grid_state"
        )[0],
        "grid_confidence": decoder.raw(
            base + 3, 1, f"{path}.grid_confidence"
        )[0],
        "grid_flags": decoder.u32(base + 4, f"{path}.grid_flags"),
        "representation": decoder.u32(
            base + 8, f"{path}.representation"
        ),
        "segment_count": decoder.u32(
            base + 12, f"{path}.segment_count"
        ),
        "requested_first_frame": decoder.u64(
            base + 16, f"{path}.requested_first_frame"
        ),
        "requested_end_frame": decoder.u64(
            base + 24, f"{path}.requested_end_frame"
        ),
        "evidence_first_frame": decoder.u64(
            base + 32, f"{path}.evidence_first_frame"
        ),
        "evidence_end_frame": decoder.u64(
            base + 40, f"{path}.evidence_end_frame"
        ),
        "applicability_first_frame": decoder.u64(
            base + 48, f"{path}.applicability_first_frame"
        ),
        "applicability_end_frame": decoder.u64(
            base + 56, f"{path}.applicability_end_frame"
        ),
        "coverage_first_frame": decoder.u64(
            base + 64, f"{path}.coverage_first_frame"
        ),
        "coverage_end_frame": decoder.u64(
            base + 72, f"{path}.coverage_end_frame"
        ),
        "anchor_whole_frame": decoder.u64(
            base + 80, f"{path}.anchor_whole_frame"
        ),
        "anchor_fraction_q32": decoder.u32(
            base + 88, f"{path}.anchor_fraction_q32"
        ),
        "reserved_anchor": decoder.u32(
            base + 92, f"{path}.reserved_anchor"
        ),
        "anchor_ordinal": decoder.i64(
            base + 96, f"{path}.anchor_ordinal"
        ),
        "period_whole_frames": decoder.u64(
            base + 104, f"{path}.period_whole_frames"
        ),
        "period_fraction_q32": decoder.u32(
            base + 112, f"{path}.period_fraction_q32"
        ),
        "beat_count": decoder.u32(
            base + 116, f"{path}.beat_count"
        ),
        "nominal_tempo_millibpm": decoder.u32(
            base + 120, f"{path}.nominal_tempo_millibpm"
        ),
        "segment_id": decoder.u32(
            base + 124, f"{path}.segment_id"
        ),
        "revision": decoder.u32(base + 128, f"{path}.revision"),
        "segment_flags": decoder.u32(
            base + 132, f"{path}.segment_flags"
        ),
        "segment_state": decoder.raw(
            base + 136, 1, f"{path}.segment_state"
        )[0],
        "segment_confidence": decoder.raw(
            base + 137, 1, f"{path}.segment_confidence"
        )[0],
        "reserved16": decoder.u16(
            base + 138, f"{path}.reserved16"
        ),
        "reserved32": decoder.u32(
            base + 140, f"{path}.reserved32"
        ),
    }


def parse_ggrd(decoder: Decoder, base: int) -> dict[str, Any]:
    path = "sections.GGRD"
    segment_count = decoder.u32(
        base + 16, f"{path}.segment_count"
    )
    beat_count = decoder.u32(base + 20, f"{path}.beat_count")
    segments = []
    for index in range(segment_count):
        entry = base + 96 + index * 80
        item = f"{path}.segments[{index}]"
        segments.append(
            {
                "applicability_first_frame": decoder.u64(
                    entry, f"{item}.applicability_first_frame"
                ),
                "applicability_end_frame": decoder.u64(
                    entry + 8, f"{item}.applicability_end_frame"
                ),
                "anchor_whole_frame": decoder.u64(
                    entry + 16, f"{item}.anchor_whole_frame"
                ),
                "anchor_fraction_q32": decoder.u32(
                    entry + 24, f"{item}.anchor_fraction_q32"
                ),
                "reserved_anchor": decoder.u32(
                    entry + 28, f"{item}.reserved_anchor"
                ),
                "anchor_ordinal": decoder.i64(
                    entry + 32, f"{item}.anchor_ordinal"
                ),
                "period_whole_frames": decoder.u64(
                    entry + 40, f"{item}.period_whole_frames"
                ),
                "period_fraction_q32": decoder.u32(
                    entry + 48, f"{item}.period_fraction_q32"
                ),
                "beat_count": decoder.u32(
                    entry + 52, f"{item}.beat_count"
                ),
                "nominal_tempo_millibpm": decoder.u32(
                    entry + 56, f"{item}.nominal_tempo_millibpm"
                ),
                "segment_id": decoder.u32(
                    entry + 60, f"{item}.segment_id"
                ),
                "revision_id": decoder.u32(
                    entry + 64, f"{item}.revision_id"
                ),
                "segment_flags": decoder.u32(
                    entry + 68, f"{item}.segment_flags"
                ),
                "segment_state": decoder.raw(
                    entry + 72, 1, f"{item}.segment_state"
                )[0],
                "segment_confidence": decoder.raw(
                    entry + 73, 1, f"{item}.segment_confidence"
                )[0],
                "reserved_hex": decoder.raw(
                    entry + 74, 6, f"{item}.reserved_hex"
                ).hex(),
            }
        )
    beats = []
    beat_base = base + 96 + segment_count * 80
    for index in range(beat_count):
        entry = beat_base + index * 40
        item = f"{path}.beats[{index}]"
        beats.append(
            {
                "whole_frame": decoder.u64(
                    entry, f"{item}.whole_frame"
                ),
                "fraction_q32": decoder.u32(
                    entry + 8, f"{item}.fraction_q32"
                ),
                "reserved_position": decoder.u32(
                    entry + 12, f"{item}.reserved_position"
                ),
                "ordinal": decoder.i64(
                    entry + 16, f"{item}.ordinal"
                ),
                "revision_id": decoder.u32(
                    entry + 24, f"{item}.revision_id"
                ),
                "beat_flags": decoder.u32(
                    entry + 28, f"{item}.beat_flags"
                ),
                "confidence": decoder.raw(
                    entry + 32, 1, f"{item}.confidence"
                )[0],
                "reserved_hex": decoder.raw(
                    entry + 33, 7, f"{item}.reserved_hex"
                ).hex(),
            }
        )
    return {
        "payload_version": decoder.u16(
            base, f"{path}.payload_version"
        ),
        "grid_state": decoder.raw(
            base + 2, 1, f"{path}.grid_state"
        )[0],
        "grid_confidence": decoder.raw(
            base + 3, 1, f"{path}.grid_confidence"
        )[0],
        "grid_flags": decoder.u32(base + 4, f"{path}.grid_flags"),
        "representation": decoder.u32(
            base + 8, f"{path}.representation"
        ),
        "coverage_range_count": decoder.u32(
            base + 12, f"{path}.coverage_range_count"
        ),
        "segment_count": segment_count,
        "beat_count": beat_count,
        "requested_first_frame": decoder.u64(
            base + 24, f"{path}.requested_first_frame"
        ),
        "requested_end_frame": decoder.u64(
            base + 32, f"{path}.requested_end_frame"
        ),
        "evidence_first_frame": decoder.u64(
            base + 40, f"{path}.evidence_first_frame"
        ),
        "evidence_end_frame": decoder.u64(
            base + 48, f"{path}.evidence_end_frame"
        ),
        "applicability_first_frame": decoder.u64(
            base + 56, f"{path}.applicability_first_frame"
        ),
        "applicability_end_frame": decoder.u64(
            base + 64, f"{path}.applicability_end_frame"
        ),
        "coverage_first_frame": decoder.u64(
            base + 72, f"{path}.coverage_first_frame"
        ),
        "coverage_end_frame": decoder.u64(
            base + 80, f"{path}.coverage_end_frame"
        ),
        "reserved": decoder.u64(base + 88, f"{path}.reserved"),
        "segments": segments,
        "beats": beats,
    }


def parse_revn(decoder: Decoder, base: int) -> dict[str, Any]:
    path = "sections.REVN"
    return {
        "payload_version": decoder.u16(
            base, f"{path}.payload_version"
        ),
        "revision_state": decoder.raw(
            base + 2, 1, f"{path}.revision_state"
        )[0],
        "revision_confidence": decoder.raw(
            base + 3, 1, f"{path}.revision_confidence"
        )[0],
        "revision_flags": decoder.u32(
            base + 4, f"{path}.revision_flags"
        ),
        "revision_id": decoder.u32(
            base + 8, f"{path}.revision_id"
        ),
        "previous_revision_id": decoder.u32(
            base + 12, f"{path}.previous_revision_id"
        ),
        "proposed_representation": decoder.u32(
            base + 16, f"{path}.proposed_representation"
        ),
        "proposed_segment_count": decoder.u32(
            base + 20, f"{path}.proposed_segment_count"
        ),
        "proposed_beat_count": decoder.u32(
            base + 24, f"{path}.proposed_beat_count"
        ),
        "reserved32": decoder.u32(
            base + 28, f"{path}.reserved32"
        ),
        "affected_first_frame": decoder.u64(
            base + 32, f"{path}.affected_first_frame"
        ),
        "affected_end_frame": decoder.u64(
            base + 40, f"{path}.affected_end_frame"
        ),
        "reserved_hex": decoder.raw(
            base + 48, 32, f"{path}.reserved_hex"
        ).hex(),
    }
