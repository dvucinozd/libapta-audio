// SPDX-License-Identifier: Apache-2.0
#include "../beatgrid/apta_s6_internal.h"

#include <stdint.h>

static int apta_result_allocation_add(
    uint64_t *total,
    uint64_t count,
    size_t element_size)
{
    const uint64_t size = (uint64_t)element_size;

    if (count != 0u && size > (UINT64_MAX - *total) / count) {
        return 0;
    }
    *total += count * size;
    return 1;
}

int apta_internal_result_allocation_bytes(
    const apta_result_t *result,
    uint64_t *allocation_bytes_out)
{
    uint64_t total;
    uint32_t index;

    if (result == NULL || allocation_bytes_out == NULL) {
        return 0;
    }

    total = sizeof(*result);
    if (!apta_result_allocation_add(
            &total,
            result->overview.span_count,
            sizeof(apta_waveform_span_t))) {
        return 0;
    }
    for (index = 0u; index < result->overview.span_count; ++index) {
        if (!apta_result_allocation_add(
                &total,
                result->overview.spans[index].column_count,
                sizeof(apta_waveform_column_t))) {
            return 0;
        }
    }
    if (!apta_result_allocation_add(
            &total,
            result->detail_tile_count,
            sizeof(apta_waveform_tile_view_t))) {
        return 0;
    }
    for (index = 0u; index < result->detail_tile_count; ++index) {
        if (!apta_result_allocation_add(
                &total,
                result->detail_tiles[index].column_count,
                sizeof(apta_waveform_column_t))) {
            return 0;
        }
    }
    if (!apta_result_allocation_add(
            &total,
            result->metadata.storage_size,
            sizeof(uint8_t)) ||
        !apta_result_allocation_add(
            &total,
            result->tempo.candidate_count,
            sizeof(apta_tempo_candidate_t)) ||
        !apta_result_allocation_add(
            &total,
            result->local_grid.coverage_range_count,
            sizeof(apta_frame_range_t)) ||
        !apta_result_allocation_add(
            &total,
            result->local_grid.segment_count,
            sizeof(apta_grid_segment_t)) ||
        !apta_result_allocation_add(
            &total,
            result->local_grid.beat_count,
            sizeof(apta_beat_t)) ||
        !apta_result_allocation_add(
            &total,
            result->provenance_storage_size,
            sizeof(uint8_t)) ||
        !apta_result_allocation_add(
            &total,
            result->key.candidate_count,
            sizeof(apta_key_candidate_t)) ||
        !apta_result_allocation_add(
            &total,
            result->meter.segment_count,
            sizeof(apta_meter_segment_t)) ||
        !apta_result_allocation_add(
            &total,
            result->quality_count,
            sizeof(apta_quality_view_t))) {
        return 0;
    }

    if (result->s6 != NULL) {
        if (!apta_result_allocation_add(
                &total,
                1u,
                sizeof(*result->s6)) ||
            !apta_result_allocation_add(
                &total,
                result->s6->global_grid.coverage_range_count,
                sizeof(apta_frame_range_t)) ||
            !apta_result_allocation_add(
                &total,
                result->s6->global_grid.segment_count,
                sizeof(apta_grid_segment_t)) ||
            !apta_result_allocation_add(
                &total,
                result->s6->global_grid.beat_count,
                sizeof(apta_beat_t))) {
            return 0;
        }
    }

    *allocation_bytes_out = total;
    return 1;
}

int apta_internal_result_allocation_fits(
    const apta_result_t *result,
    uint64_t additional_bytes,
    uint64_t maximum_allocation_bytes)
{
    uint64_t existing_bytes;

    return apta_internal_result_allocation_bytes(result, &existing_bytes) &&
           existing_bytes <= maximum_allocation_bytes &&
           additional_bytes <= maximum_allocation_bytes - existing_bytes;
}
