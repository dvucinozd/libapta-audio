// SPDX-License-Identifier: Apache-2.0
#include "apta_result_pool_layout.h"
#include "../beatgrid/apta_s6_internal.h"

#include <stdalign.h>
#include <string.h>

static int apta_pool_add_size(
    size_t left,
    size_t right,
    size_t *result_out)
{
    if (result_out == NULL || left > SIZE_MAX - right) {
        return 0;
    }
    *result_out = left + right;
    return 1;
}

static int apta_pool_multiply_size(
    size_t count,
    size_t element_size,
    size_t *result_out)
{
    if (result_out == NULL ||
        (element_size != 0u && count > SIZE_MAX / element_size)) {
        return 0;
    }
    *result_out = count * element_size;
    return 1;
}

static int apta_pool_align_size(
    size_t value,
    size_t alignment,
    size_t *result_out)
{
    size_t mask;

    if (result_out == NULL ||
        !apta_internal_is_power_of_two(alignment)) {
        return 0;
    }
    mask = alignment - 1u;
    if (value > SIZE_MAX - mask) {
        return 0;
    }
    *result_out = (value + mask) & ~mask;
    return 1;
}

static int apta_pool_append_region(
    size_t *offset,
    size_t alignment,
    size_t count,
    size_t element_size,
    size_t *region_offset_out)
{
    size_t aligned;
    size_t bytes;
    size_t end;

    if (offset == NULL || region_offset_out == NULL ||
        !apta_pool_align_size(*offset, alignment, &aligned) ||
        !apta_pool_multiply_size(count, element_size, &bytes) ||
        !apta_pool_add_size(aligned, bytes, &end)) {
        return 0;
    }

    *region_offset_out = aligned;
    *offset = end;
    return 1;
}

apta_status_t apta_internal_result_pool_calculate_layout(
    const apta_session_config_t *config,
    apta_internal_result_pool_layout_t *layout_out)
{
    uint64_t overview_columns64;
    uint32_t overview_columns;
    uint32_t detail_tiles;
    uint32_t detail_columns;
    uint32_t tempo_candidates;
    uint32_t local_grid_coverage;
    uint32_t local_grid_segments;
    uint32_t global_grid_state;
    uint32_t global_grid_coverage;
    uint32_t global_grid_segments;
    uint32_t global_grid_beats;
    size_t slot_offset;
    size_t pool_offset;
    size_t slots_bytes;
    uint32_t slot_index;
    uint32_t frames_per_column;

    if (config == NULL || layout_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    memset(layout_out, 0, sizeof(*layout_out));

    if ((config->flags & APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) == 0u ||
        (config->flags & ~APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u ||
        (config->requested_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u ||
        config->total_frames == APTA_TOTAL_FRAMES_UNKNOWN) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    /* C2: the pool must be sized for the resolution the session will use, not
     * the compile-time default. */
    frames_per_column = config->overview_frames_per_column != 0u
                            ? config->overview_frames_per_column
                            : APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN;
    overview_columns64 = config->total_frames / frames_per_column;
    if ((config->total_frames % frames_per_column) != 0u) {
        overview_columns64 += 1u;
    }
    if (overview_columns64 > UINT32_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    overview_columns = (uint32_t)overview_columns64;

    detail_tiles =
        (config->requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) != 0u
            ? APTA_INTERNAL_MAX_DETAIL_TILES
            : 0u;
    detail_columns =
        detail_tiles * APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE;
    tempo_candidates =
        (config->requested_features & APTA_FEATURE_BPM) != 0u
            ? APTA_INTERNAL_MAX_TEMPO_CANDIDATES
            : 0u;
    local_grid_coverage =
        (config->requested_features &
         APTA_FEATURE_LOCAL_BEATGRID) != 0u
            ? 1u
            : 0u;
    local_grid_segments = local_grid_coverage;
    global_grid_state =
        (config->requested_features &
         APTA_FEATURE_GLOBAL_BEATGRID) != 0u
            ? 1u
            : 0u;
    global_grid_coverage = global_grid_state;
    global_grid_segments = global_grid_state != 0u
                               ? APTA_INTERNAL_GLOBAL_MAX_SEGMENTS
                               : 0u;
    global_grid_beats = global_grid_state != 0u
                            ? APTA_INTERNAL_GLOBAL_MAX_BEATS
                            : 0u;

    slot_offset = 0u;
    if (!apta_pool_append_region(
            &slot_offset,
            alignof(apta_result_t),
            1u,
            sizeof(apta_result_t),
            &layout_out->result_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_waveform_span_t),
            overview_columns,
            sizeof(apta_waveform_span_t),
            &layout_out->overview_spans_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_waveform_column_t),
            overview_columns,
            sizeof(apta_waveform_column_t),
            &layout_out->overview_columns_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_waveform_tile_view_t),
            detail_tiles,
            sizeof(apta_waveform_tile_view_t),
            &layout_out->detail_tiles_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_waveform_column_t),
            detail_columns,
            sizeof(apta_waveform_column_t),
            &layout_out->detail_columns_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_tempo_candidate_t),
            tempo_candidates,
            sizeof(apta_tempo_candidate_t),
            &layout_out->tempo_candidates_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_frame_range_t),
            local_grid_coverage,
            sizeof(apta_frame_range_t),
            &layout_out->local_grid_coverage_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_grid_segment_t),
            local_grid_segments,
            sizeof(apta_grid_segment_t),
            &layout_out->local_grid_segments_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_internal_s6_result_state_t),
            global_grid_state,
            sizeof(apta_internal_s6_result_state_t),
            &layout_out->global_grid_state_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_frame_range_t),
            global_grid_coverage,
            sizeof(apta_frame_range_t),
            &layout_out->global_grid_coverage_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_grid_segment_t),
            global_grid_segments,
            sizeof(apta_grid_segment_t),
            &layout_out->global_grid_segments_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(apta_beat_t),
            global_grid_beats,
            sizeof(apta_beat_t),
            &layout_out->global_grid_beats_offset) ||
        !apta_pool_append_region(
            &slot_offset,
            alignof(uint8_t),
            APTA_METADATA_MAX_TOTAL_BYTES,
            sizeof(uint8_t),
            &layout_out->metadata_offset) ||
        !apta_pool_align_size(
            slot_offset,
            APTA_INTERNAL_MAX_ALIGNMENT,
            &layout_out->slot_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    if (!apta_pool_align_size(
            sizeof(apta_internal_result_pool_control_t),
            APTA_INTERNAL_MAX_ALIGNMENT,
            &pool_offset) ||
        !apta_pool_multiply_size(
            APTA_INTERNAL_RESULT_SLOT_COUNT,
            layout_out->slot_bytes,
            &slots_bytes) ||
        !apta_pool_add_size(
            pool_offset,
            slots_bytes,
            &layout_out->total_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    for (slot_index = 0u;
         slot_index < APTA_INTERNAL_RESULT_SLOT_COUNT;
         ++slot_index) {
        size_t relative;

        if (!apta_pool_multiply_size(
                slot_index,
                layout_out->slot_bytes,
                &relative) ||
            !apta_pool_add_size(
                pool_offset,
                relative,
                &layout_out->slot_offsets[slot_index])) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
    }

    if (config->memory_budget_bytes != 0u &&
        (uint64_t)layout_out->total_bytes >
            config->memory_budget_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    layout_out->overview_span_capacity = overview_columns;
    layout_out->overview_column_capacity = overview_columns;
    layout_out->detail_tile_capacity = detail_tiles;
    layout_out->detail_column_capacity = detail_columns;
    layout_out->tempo_candidate_capacity = tempo_candidates;
    layout_out->local_grid_coverage_capacity = local_grid_coverage;
    layout_out->local_grid_segment_capacity = local_grid_segments;
    layout_out->global_grid_coverage_capacity = global_grid_coverage;
    layout_out->global_grid_segment_capacity = global_grid_segments;
    layout_out->global_grid_beat_capacity = global_grid_beats;
    layout_out->metadata_capacity = APTA_METADATA_MAX_TOTAL_BYTES;
    layout_out->slot_count = APTA_INTERNAL_RESULT_SLOT_COUNT;
    return APTA_STATUS_OK;
}
