// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define APTA_DIRECTORY_ENTRY_SIZE 40u
#define APTA_WDTL_HEADER_SIZE 16u
#define APTA_WDTL_TILE_SIZE 48u
#define APTA_PACKED_COLUMN_SIZE 10u
#define APTA_WAVEFORM_COLUMN_FLAG_MASK 0x1Fu
#define APTA_CONTAINER_FLAG_PARTIAL_RESULT (1u << 0)

#define APTA_PARSE_DEFAULT_MAX_WAVEFORM_COLUMNS 16777216u
#define APTA_PARSE_DEFAULT_MAX_ALLOCATION_BYTES UINT64_C(268435456)

#define APTA_WDTL_LEVEL_ID 1u
#define APTA_WDTL_FRAMES_PER_COLUMN 256u
#define APTA_WDTL_COLUMNS_PER_TILE 64u

typedef struct {
    uint64_t first;
    uint64_t end;
} apta_wdtl_interval_t;

typedef struct {
    uint32_t tile_count;
    uint32_t column_count;
    uint32_t section_count;
    uint32_t strict;
    uint32_t maximum_waveform_columns;
    uint64_t maximum_allocation_bytes;
} apta_wdtl_parse_layout_t;

APTA_API apta_status_t APTA_CALL apta_result_parse_wovr_hardened(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out);

static uint16_t apta_wdtl_get_u16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8u));
}

static uint32_t apta_wdtl_get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8u) |
           ((uint32_t)source[2] << 16u) |
           ((uint32_t)source[3] << 24u);
}

static uint64_t apta_wdtl_get_u64(const uint8_t *source)
{
    return (uint64_t)apta_wdtl_get_u32(source) |
           ((uint64_t)apta_wdtl_get_u32(source + 4u) << 32u);
}

static int16_t apta_wdtl_get_i16(const uint8_t *source)
{
    return (int16_t)apta_wdtl_get_u16(source);
}

static int apta_wdtl_range_fits(
    uint64_t offset,
    uint64_t size,
    uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static int apta_wdtl_ranges_overlap(
    uint64_t first_offset,
    uint64_t first_size,
    uint64_t second_offset,
    uint64_t second_size)
{
    if (first_size == 0u || second_size == 0u) {
        return 0;
    }
    return first_offset < second_offset + second_size &&
           second_offset < first_offset + first_size;
}

static int apta_wdtl_confidence_is_valid(uint8_t confidence)
{
    return confidence <= APTA_CONFIDENCE_MAX ||
           confidence == APTA_CONFIDENCE_UNKNOWN;
}

static int apta_wdtl_compare_intervals(const void *left, const void *right)
{
    const apta_wdtl_interval_t *left_interval =
        (const apta_wdtl_interval_t *)left;
    const apta_wdtl_interval_t *right_interval =
        (const apta_wdtl_interval_t *)right;

    if (left_interval->first < right_interval->first) {
        return -1;
    }
    if (left_interval->first > right_interval->first) {
        return 1;
    }
    if (left_interval->end < right_interval->end) {
        return -1;
    }
    if (left_interval->end > right_interval->end) {
        return 1;
    }
    return 0;
}

static int apta_wdtl_compare_tiles(const void *left, const void *right)
{
    const apta_waveform_tile_view_t *left_tile =
        (const apta_waveform_tile_view_t *)left;
    const apta_waveform_tile_view_t *right_tile =
        (const apta_waveform_tile_view_t *)right;

    if (left_tile->level_id < right_tile->level_id) {
        return -1;
    }
    if (left_tile->level_id > right_tile->level_id) {
        return 1;
    }
    if (left_tile->tile_index < right_tile->tile_index) {
        return -1;
    }
    if (left_tile->tile_index > right_tile->tile_index) {
        return 1;
    }
    return 0;
}

static void apta_wdtl_resolve_options(
    const apta_parse_options_t *options,
    apta_wdtl_parse_layout_t *layout)
{
    layout->strict = options == NULL ||
                     (options->flags & APTA_PARSE_STRICT) != 0u;
    layout->maximum_waveform_columns =
        options != NULL && options->maximum_waveform_columns != 0u
            ? options->maximum_waveform_columns
            : APTA_PARSE_DEFAULT_MAX_WAVEFORM_COLUMNS;
    layout->maximum_allocation_bytes =
        options != NULL && options->maximum_allocation_bytes != 0u
            ? options->maximum_allocation_bytes
            : APTA_PARSE_DEFAULT_MAX_ALLOCATION_BYTES;
}

static apta_status_t apta_wdtl_validate_descriptor(
    const uint8_t *payload,
    uint64_t payload_size,
    uint64_t section_file_offset,
    uint64_t tile_directory_offset,
    uint64_t tile_directory_size,
    const uint8_t *descriptor,
    uint64_t total_source_frames,
    uint32_t container_flags,
    uint32_t strict,
    apta_wdtl_interval_t *interval_out)
{
    uint32_t level_id = apta_wdtl_get_u32(descriptor + 0u);
    uint32_t tile_index = apta_wdtl_get_u32(descriptor + 4u);
    uint64_t first_frame = apta_wdtl_get_u64(descriptor + 8u);
    uint64_t end_frame = apta_wdtl_get_u64(descriptor + 16u);
    uint32_t first_column = apta_wdtl_get_u32(descriptor + 24u);
    uint32_t column_count = apta_wdtl_get_u32(descriptor + 28u);
    uint64_t columns_offset = apta_wdtl_get_u64(descriptor + 32u);
    uint32_t feature_state = apta_wdtl_get_u32(descriptor + 40u);
    uint16_t flags = apta_wdtl_get_u16(descriptor + 44u);
    uint8_t confidence = descriptor[46];
    uint8_t reserved = descriptor[47];
    uint64_t tile_first_column;
    uint64_t end_column;
    uint64_t expected_first;
    uint64_t expected_end;
    uint64_t column_bytes;
    uint32_t column_index;

    if (level_id != APTA_WDTL_LEVEL_ID) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (tile_index > UINT32_MAX / APTA_WDTL_COLUMNS_PER_TILE) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (first_frame >= end_frame || column_count == 0u ||
        feature_state < APTA_FEATURE_PARTIAL ||
        feature_state > APTA_FEATURE_FINAL ||
        !apta_wdtl_confidence_is_valid(confidence) ||
        (strict && (flags != 0u || reserved != 0u))) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (feature_state != APTA_FEATURE_FINAL &&
        (container_flags & APTA_CONTAINER_FLAG_PARTIAL_RESULT) == 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    tile_first_column =
        (uint64_t)tile_index * APTA_WDTL_COLUMNS_PER_TILE;
    end_column = (uint64_t)first_column + column_count;
    if ((uint64_t)first_column < tile_first_column ||
        end_column > tile_first_column + APTA_WDTL_COLUMNS_PER_TILE) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    expected_first = (uint64_t)first_column * APTA_WDTL_FRAMES_PER_COLUMN;
    expected_end = end_column * APTA_WDTL_FRAMES_PER_COLUMN;
    if (first_frame != expected_first) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (end_frame != expected_end) {
        if (total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN ||
            end_frame != total_source_frames ||
            expected_end <= total_source_frames) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    }
    if (total_source_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        end_frame > total_source_frames) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    column_bytes = (uint64_t)column_count * APTA_PACKED_COLUMN_SIZE;
    if (!apta_wdtl_range_fits(columns_offset, column_bytes, payload_size) ||
        apta_wdtl_ranges_overlap(
            columns_offset,
            column_bytes,
            0u,
            APTA_WDTL_HEADER_SIZE) ||
        apta_wdtl_ranges_overlap(
            columns_offset,
            column_bytes,
            tile_directory_offset,
            tile_directory_size)) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    for (column_index = 0u; column_index < column_count; ++column_index) {
        const uint8_t *column = payload + (size_t)columns_offset +
                                (size_t)column_index * APTA_PACKED_COLUMN_SIZE;
        int16_t minimum = apta_wdtl_get_i16(column + 0u);
        int16_t maximum = apta_wdtl_get_i16(column + 2u);
        uint8_t column_flags = column[9];

        if ((column_flags & APTA_WAVEFORM_COLUMN_VALID) == 0u ||
            minimum > maximum ||
            (strict &&
             (column_flags & ~APTA_WAVEFORM_COLUMN_FLAG_MASK) != 0u) ||
            (strict &&
             (column_flags & APTA_WAVEFORM_COLUMN_HAS_3BAND) == 0u &&
             (column[6] != 0u || column[7] != 0u || column[8] != 0u))) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    }

    interval_out->first = section_file_offset + columns_offset;
    interval_out->end = interval_out->first + column_bytes;
    return APTA_STATUS_OK;
}

static apta_status_t apta_wdtl_measure(
    const uint8_t *bytes,
    size_t buffer_size,
    const apta_parse_options_t *options,
    apta_wdtl_parse_layout_t *layout,
    apta_wdtl_interval_t *intervals)
{
    uint32_t container_flags = apta_wdtl_get_u32(bytes + 16u);
    uint32_t section_count = apta_wdtl_get_u32(bytes + 20u);
    uint64_t directory_offset = apta_wdtl_get_u64(bytes + 24u);
    uint64_t total_source_frames = apta_wdtl_get_u64(bytes + 40u);
    uint32_t interval_index = 0u;
    uint32_t section_index;

    (void)buffer_size;
    memset(layout, 0, sizeof(*layout));
    apta_wdtl_resolve_options(options, layout);

    for (section_index = 0u; section_index < section_count; ++section_index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)section_index * APTA_DIRECTORY_ENTRY_SIZE;
        uint16_t version;
        uint64_t section_offset;
        uint64_t payload_size;
        const uint8_t *payload;
        uint32_t tile_count;
        uint64_t tile_directory_offset;
        uint64_t tile_directory_size;
        uint32_t tile_index;

        if (memcmp(entry, "WDTL", 4u) != 0) {
            continue;
        }

        version = apta_wdtl_get_u16(entry + 4u);
        if (version != 1u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        section_offset = apta_wdtl_get_u64(entry + 8u);
        payload_size = apta_wdtl_get_u64(entry + 16u);
        payload = bytes + (size_t)section_offset;
        if (payload_size < APTA_WDTL_HEADER_SIZE) {
            return APTA_ERROR_CORRUPT_DATA;
        }

        tile_count = apta_wdtl_get_u32(payload + 0u);
        tile_directory_offset = apta_wdtl_get_u64(payload + 8u);
        if (tile_count == 0u ||
            (layout->strict && apta_wdtl_get_u32(payload + 4u) != 0u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        tile_directory_size = (uint64_t)tile_count * APTA_WDTL_TILE_SIZE;
        if (tile_directory_offset < APTA_WDTL_HEADER_SIZE ||
            !apta_wdtl_range_fits(
                tile_directory_offset,
                tile_directory_size,
                payload_size)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        if (layout->tile_count > UINT32_MAX - tile_count) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }

        for (tile_index = 0u; tile_index < tile_count; ++tile_index) {
            const uint8_t *descriptor =
                payload + (size_t)tile_directory_offset +
                (size_t)tile_index * APTA_WDTL_TILE_SIZE;
            uint32_t column_count = apta_wdtl_get_u32(descriptor + 28u);
            apta_wdtl_interval_t ignored_interval;
            apta_wdtl_interval_t *interval =
                intervals != NULL ? &intervals[interval_index]
                                  : &ignored_interval;
            apta_status_t status = apta_wdtl_validate_descriptor(
                payload,
                payload_size,
                section_offset,
                tile_directory_offset,
                tile_directory_size,
                descriptor,
                total_source_frames,
                container_flags,
                layout->strict,
                interval);

            if (status < 0) {
                return status;
            }
            if (layout->column_count > UINT32_MAX - column_count) {
                return APTA_ERROR_LIMIT_EXCEEDED;
            }
            layout->column_count += column_count;
            if (layout->column_count > layout->maximum_waveform_columns) {
                return APTA_ERROR_LIMIT_EXCEEDED;
            }
            interval_index += 1u;
        }

        layout->tile_count += tile_count;
        if (layout->tile_count > layout->maximum_waveform_columns) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        layout->section_count += 1u;
    }

    return APTA_STATUS_OK;
}

static apta_status_t apta_wdtl_validate_intervals(
    apta_wdtl_interval_t *intervals,
    uint32_t count)
{
    uint32_t index;

    qsort(intervals, count, sizeof(*intervals), apta_wdtl_compare_intervals);
    for (index = 1u; index < count; ++index) {
        if (intervals[index].first < intervals[index - 1u].end) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_wdtl_copy(
    apta_result_t *result,
    const uint8_t *bytes,
    const apta_wdtl_parse_layout_t *layout)
{
    uint32_t section_count = apta_wdtl_get_u32(bytes + 20u);
    uint64_t directory_offset = apta_wdtl_get_u64(bytes + 24u);
    uint32_t output_tile = 0u;
    uint32_t output_column = 0u;
    uint32_t section_index;

    result->detail_tiles =
        (apta_waveform_tile_view_t *)apta_internal_context_allocate(
            result->context,
            (size_t)layout->tile_count * sizeof(*result->detail_tiles),
            alignof(apta_waveform_tile_view_t),
            APTA_MEMORY_PERSISTENT);
    if (result->detail_tiles == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    result->detail_columns =
        (apta_waveform_column_t *)apta_internal_context_allocate(
            result->context,
            (size_t)layout->column_count * sizeof(*result->detail_columns),
            alignof(apta_waveform_column_t),
            APTA_MEMORY_PERSISTENT);
    if (result->detail_columns == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(
        result->detail_tiles,
        0,
        (size_t)layout->tile_count * sizeof(*result->detail_tiles));
    memset(
        result->detail_columns,
        0,
        (size_t)layout->column_count * sizeof(*result->detail_columns));

    for (section_index = 0u; section_index < section_count; ++section_index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)section_index * APTA_DIRECTORY_ENTRY_SIZE;
        uint64_t section_offset;
        const uint8_t *payload;
        uint32_t tile_count;
        uint64_t tile_directory_offset;
        uint32_t tile_index;

        if (memcmp(entry, "WDTL", 4u) != 0) {
            continue;
        }

        section_offset = apta_wdtl_get_u64(entry + 8u);
        payload = bytes + (size_t)section_offset;
        tile_count = apta_wdtl_get_u32(payload + 0u);
        tile_directory_offset = apta_wdtl_get_u64(payload + 8u);

        for (tile_index = 0u; tile_index < tile_count; ++tile_index) {
            const uint8_t *descriptor =
                payload + (size_t)tile_directory_offset +
                (size_t)tile_index * APTA_WDTL_TILE_SIZE;
            apta_waveform_tile_view_t *tile =
                &result->detail_tiles[output_tile];
            uint32_t column_count = apta_wdtl_get_u32(descriptor + 28u);
            uint64_t columns_offset = apta_wdtl_get_u64(descriptor + 32u);
            uint32_t column_index;

            tile->struct_size = (uint32_t)sizeof(*tile);
            tile->api_version = APTA_API_VERSION;
            tile->level_id = apta_wdtl_get_u32(descriptor + 0u);
            tile->tile_index = apta_wdtl_get_u32(descriptor + 4u);
            tile->source_range.struct_size =
                (uint32_t)sizeof(tile->source_range);
            tile->source_range.api_version = APTA_API_VERSION;
            tile->source_range.first_frame =
                apta_wdtl_get_u64(descriptor + 8u);
            tile->source_range.end_frame =
                apta_wdtl_get_u64(descriptor + 16u);
            tile->first_column_index =
                apta_wdtl_get_u32(descriptor + 24u);
            tile->column_count = column_count;
            tile->columns = &result->detail_columns[output_column];
            tile->state = apta_wdtl_get_u32(descriptor + 40u);
            tile->confidence = descriptor[46];

            for (column_index = 0u; column_index < column_count; ++column_index) {
                const uint8_t *input =
                    payload + (size_t)columns_offset +
                    (size_t)column_index * APTA_PACKED_COLUMN_SIZE;
                apta_waveform_column_t *output =
                    &result->detail_columns[output_column + column_index];

                output->minimum = apta_wdtl_get_i16(input + 0u);
                output->maximum = apta_wdtl_get_i16(input + 2u);
                output->rms = apta_wdtl_get_u16(input + 4u);
                output->low = input[6];
                output->mid = input[7];
                output->high = input[8];
                output->flags = input[9];
            }

            output_column += column_count;
            output_tile += 1u;
        }
    }

    qsort(
        result->detail_tiles,
        layout->tile_count,
        sizeof(*result->detail_tiles),
        apta_wdtl_compare_tiles);
    for (output_tile = 1u; output_tile < layout->tile_count; ++output_tile) {
        const apta_waveform_tile_view_t *previous =
            &result->detail_tiles[output_tile - 1u];
        const apta_waveform_tile_view_t *current =
            &result->detail_tiles[output_tile];

        if (previous->level_id == current->level_id &&
            previous->tile_index == current->tile_index) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    }

    result->detail_tile_count = layout->tile_count;
    result->info.available_features |= APTA_FEATURE_WAVEFORM_DETAIL;
    result->info.changed_features |= APTA_FEATURE_WAVEFORM_DETAIL;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_parse(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out)
{
    const apta_result_t *parsed_const = NULL;
    apta_result_t *parsed;
    apta_wdtl_parse_layout_t layout;
    apta_wdtl_interval_t *intervals = NULL;
    uint64_t allocation_bytes;
    apta_status_t status;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;

    status = apta_result_parse_wovr_hardened(
        context,
        options,
        buffer,
        buffer_size,
        &parsed_const);
    if (status < 0) {
        return status;
    }

    parsed = (apta_result_t *)parsed_const;
    status = apta_wdtl_measure(
        (const uint8_t *)buffer,
        buffer_size,
        options,
        &layout,
        NULL);
    if (status < 0) {
        apta_result_release(parsed_const);
        return status;
    }
    if (layout.section_count == 0u) {
        *result_out = parsed_const;
        return APTA_STATUS_OK;
    }

    allocation_bytes =
        (uint64_t)layout.tile_count *
            (sizeof(apta_waveform_tile_view_t) +
             sizeof(apta_wdtl_interval_t)) +
        (uint64_t)layout.column_count *
            sizeof(apta_waveform_column_t);
    if (!apta_internal_result_allocation_fits(
            parsed,
            allocation_bytes,
            layout.maximum_allocation_bytes) ||
        !apta_internal_size_array_fits(
            0u,
            layout.tile_count,
            sizeof(*intervals))) {
        apta_result_release(parsed_const);
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    intervals = (apta_wdtl_interval_t *)apta_internal_context_allocate(
        context,
        (size_t)layout.tile_count * sizeof(*intervals),
        alignof(apta_wdtl_interval_t),
        APTA_MEMORY_TEMPORARY);
    if (intervals == NULL) {
        apta_result_release(parsed_const);
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    status = apta_wdtl_measure(
        (const uint8_t *)buffer,
        buffer_size,
        options,
        &layout,
        intervals);
    if (status == APTA_STATUS_OK) {
        status = apta_wdtl_validate_intervals(intervals, layout.tile_count);
    }
    apta_internal_context_deallocate(context, intervals);
    if (status < 0) {
        apta_result_release(parsed_const);
        return status;
    }

    status = apta_wdtl_copy(
        parsed,
        (const uint8_t *)buffer,
        &layout);
    if (status < 0) {
        apta_result_release(parsed_const);
        return status;
    }

    *result_out = parsed_const;
    return APTA_STATUS_OK;
}
