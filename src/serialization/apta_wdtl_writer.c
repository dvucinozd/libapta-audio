// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <limits.h>
#include <string.h>

#define APTA_CONTAINER_HEADER_SIZE 96u
#define APTA_DIRECTORY_ENTRY_SIZE 40u
#define APTA_DIRECTORY_OFFSET 96u
#define APTA_SINGLE_WOVR_PAYLOAD_OFFSET 136u
#define APTA_DUAL_WOVR_PAYLOAD_OFFSET 176u
#define APTA_WDTL_HEADER_SIZE 16u
#define APTA_WDTL_TILE_SIZE 48u
#define APTA_PACKED_COLUMN_SIZE 10u

#define APTA_CONTAINER_FLAG_PARTIAL_RESULT (1u << 0)
#define APTA_SECTION_FLAG_REQUIRED (1u << 0)
#define APTA_WAVEFORM_COLUMN_FLAG_MASK 0x1Fu

#define APTA_WDTL_LEVEL_ID 1u
#define APTA_WDTL_FRAMES_PER_COLUMN 256u
#define APTA_WDTL_COLUMNS_PER_TILE 64u

typedef struct {
    uint64_t base_size;
    uint64_t wovr_payload_size;
    uint64_t wdtl_payload_size;
    uint64_t wdtl_payload_offset;
    uint64_t total_size;
    uint32_t packed_column_count;
    uint32_t container_flags_or;
    uint32_t has_wdtl;
} apta_wdtl_layout_t;

APTA_API apta_status_t APTA_CALL
apta_result_query_serialized_size_wovr(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out);

APTA_API apta_status_t APTA_CALL
apta_result_serialize_wovr(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written_out);

static void apta_wdtl_put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void apta_wdtl_put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8u) & 0xFFu);
    destination[2] = (uint8_t)((value >> 16u) & 0xFFu);
    destination[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void apta_wdtl_put_u64(uint8_t *destination, uint64_t value)
{
    apta_wdtl_put_u32(destination, (uint32_t)(value & UINT32_MAX));
    apta_wdtl_put_u32(destination + 4u, (uint32_t)(value >> 32u));
}

static apta_status_t apta_wdtl_align8(uint64_t value, uint64_t *aligned_out)
{
    if (aligned_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (value > UINT64_MAX - 7u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    *aligned_out = (value + 7u) & ~UINT64_C(7);
    return APTA_STATUS_OK;
}

static int apta_wdtl_confidence_is_valid(apta_confidence_value_t confidence)
{
    return confidence <= APTA_CONFIDENCE_MAX ||
           confidence == APTA_CONFIDENCE_UNKNOWN;
}

static apta_status_t apta_wdtl_validate_tile(
    const apta_result_t *result,
    const apta_waveform_tile_view_t *tile,
    uint32_t previous_level,
    uint32_t previous_tile,
    int have_previous)
{
    uint64_t tile_first_column;
    uint64_t end_column;
    uint64_t expected_first;
    uint64_t expected_end;
    uint32_t column_index;

    if (tile->level_id != APTA_WDTL_LEVEL_ID) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (have_previous &&
        (tile->level_id < previous_level ||
         (tile->level_id == previous_level &&
          tile->tile_index <= previous_tile))) {
        return APTA_ERROR_INTERNAL;
    }
    if (tile->source_range.first_frame >= tile->source_range.end_frame ||
        tile->column_count == 0u || tile->columns == NULL ||
        tile->state < APTA_FEATURE_PARTIAL ||
        tile->state > APTA_FEATURE_FINAL ||
        tile->flags != 0u ||
        !apta_wdtl_confidence_is_valid(tile->confidence)) {
        return APTA_ERROR_INTERNAL;
    }
    if (tile->tile_index > UINT32_MAX / APTA_WDTL_COLUMNS_PER_TILE) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    tile_first_column =
        (uint64_t)tile->tile_index * APTA_WDTL_COLUMNS_PER_TILE;
    end_column = (uint64_t)tile->first_column_index + tile->column_count;
    if ((uint64_t)tile->first_column_index < tile_first_column ||
        end_column > tile_first_column + APTA_WDTL_COLUMNS_PER_TILE) {
        return APTA_ERROR_INTERNAL;
    }

    expected_first =
        (uint64_t)tile->first_column_index * APTA_WDTL_FRAMES_PER_COLUMN;
    expected_end = end_column * APTA_WDTL_FRAMES_PER_COLUMN;
    if (tile->source_range.first_frame != expected_first) {
        return APTA_ERROR_INTERNAL;
    }
    if (tile->source_range.end_frame != expected_end) {
        if (result->total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN ||
            tile->source_range.end_frame != result->total_source_frames ||
            expected_end <= result->total_source_frames) {
            return APTA_ERROR_INTERNAL;
        }
    }
    if (result->total_source_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        tile->source_range.end_frame > result->total_source_frames) {
        return APTA_ERROR_INTERNAL;
    }
    if (tile->state == APTA_FEATURE_FINAL &&
        result->total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN) {
        return APTA_ERROR_INTERNAL;
    }

    for (column_index = 0u; column_index < tile->column_count; ++column_index) {
        const apta_waveform_column_t *column = &tile->columns[column_index];

        if ((column->flags & APTA_WAVEFORM_COLUMN_VALID) == 0u ||
            (column->flags & ~APTA_WAVEFORM_COLUMN_FLAG_MASK) != 0u ||
            column->minimum > column->maximum ||
            ((column->flags & APTA_WAVEFORM_COLUMN_HAS_3BAND) == 0u &&
             (column->low != 0u || column->mid != 0u || column->high != 0u))) {
            return APTA_ERROR_INTERNAL;
        }
    }

    return APTA_STATUS_OK;
}

static apta_status_t apta_wdtl_calculate_layout(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    apta_wdtl_layout_t *layout)
{
    uint64_t tile_bytes;
    uint64_t column_bytes;
    uint64_t wovr_end;
    uint64_t packed_columns;
    uint32_t tile_index;
    uint32_t previous_level = 0u;
    uint32_t previous_tile = 0u;
    apta_status_t status;

    if (layout == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    memset(layout, 0, sizeof(*layout));

    status = apta_result_query_serialized_size_wovr(
        result,
        options,
        &layout->base_size);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) {
        return status;
    }
    if (layout->base_size < APTA_SINGLE_WOVR_PAYLOAD_OFFSET) {
        return APTA_ERROR_INTERNAL;
    }

    if ((result->info.available_features & APTA_FEATURE_WAVEFORM_DETAIL) == 0u ||
        result->detail_tile_count == 0u) {
        layout->total_size = layout->base_size;
        return APTA_STATUS_OK;
    }
    if (result->detail_tiles == NULL || result->detail_columns == NULL) {
        return APTA_ERROR_INTERNAL;
    }

    packed_columns = 0u;
    for (tile_index = 0u; tile_index < result->detail_tile_count; ++tile_index) {
        const apta_waveform_tile_view_t *tile = &result->detail_tiles[tile_index];

        status = apta_wdtl_validate_tile(
            result,
            tile,
            previous_level,
            previous_tile,
            tile_index != 0u);
        if (status < 0) {
            return status;
        }
        if (packed_columns > UINT32_MAX - tile->column_count) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        packed_columns += tile->column_count;
        if (tile->state != APTA_FEATURE_FINAL) {
            layout->container_flags_or |= APTA_CONTAINER_FLAG_PARTIAL_RESULT;
        }
        previous_level = tile->level_id;
        previous_tile = tile->tile_index;
    }

    tile_bytes = (uint64_t)result->detail_tile_count * APTA_WDTL_TILE_SIZE;
    column_bytes = packed_columns * APTA_PACKED_COLUMN_SIZE;
    if (UINT64_MAX - APTA_WDTL_HEADER_SIZE < tile_bytes ||
        UINT64_MAX - (APTA_WDTL_HEADER_SIZE + tile_bytes) < column_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    layout->wovr_payload_size =
        layout->base_size - APTA_SINGLE_WOVR_PAYLOAD_OFFSET;
    layout->wdtl_payload_size =
        APTA_WDTL_HEADER_SIZE + tile_bytes + column_bytes;
    layout->packed_column_count = (uint32_t)packed_columns;

    if (UINT64_MAX - APTA_DUAL_WOVR_PAYLOAD_OFFSET <
        layout->wovr_payload_size) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    wovr_end = APTA_DUAL_WOVR_PAYLOAD_OFFSET + layout->wovr_payload_size;
    status = apta_wdtl_align8(wovr_end, &layout->wdtl_payload_offset);
    if (status < 0 ||
        UINT64_MAX - layout->wdtl_payload_offset < layout->wdtl_payload_size) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    layout->total_size =
        layout->wdtl_payload_offset + layout->wdtl_payload_size;
    layout->has_wdtl = 1u;

    if (options != NULL && options->maximum_output_bytes != 0u &&
        layout->total_size > options->maximum_output_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_query_serialized_size(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out)
{
    apta_wdtl_layout_t layout;
    apta_status_t status;

    if (size_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *size_out = 0u;

    status = apta_wdtl_calculate_layout(result, options, &layout);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) {
        return status;
    }

    *size_out = layout.total_size;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_serialize(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written_out)
{
    apta_wdtl_layout_t layout;
    uint8_t *bytes;
    uint8_t *wovr_directory;
    uint8_t *wdtl_directory;
    uint8_t *wovr_payload;
    uint8_t *wdtl_payload;
    uint64_t columns_offset;
    uint32_t packed_offset;
    uint32_t tile_index;
    size_t base_written;
    apta_status_t status;

    if (bytes_written_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *bytes_written_out = 0u;

    status = apta_wdtl_calculate_layout(result, options, &layout);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) {
        return status;
    }
    if (layout.total_size > SIZE_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (buffer == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (buffer_size < (size_t)layout.total_size) {
        return APTA_ERROR_BUFFER_TOO_SMALL;
    }

    status = apta_result_serialize_wovr(
        result,
        options,
        buffer,
        buffer_size,
        &base_written);
    if (status < 0) {
        return status;
    }
    if (!layout.has_wdtl) {
        *bytes_written_out = base_written;
        return APTA_STATUS_OK;
    }
    if (base_written != (size_t)layout.base_size) {
        return APTA_ERROR_INTERNAL;
    }

    bytes = (uint8_t *)buffer;
    memmove(
        bytes + APTA_DUAL_WOVR_PAYLOAD_OFFSET,
        bytes + APTA_SINGLE_WOVR_PAYLOAD_OFFSET,
        (size_t)layout.wovr_payload_size);
    memset(
        bytes + APTA_DIRECTORY_OFFSET,
        0,
        2u * APTA_DIRECTORY_ENTRY_SIZE);
    if (layout.wdtl_payload_offset >
        APTA_DUAL_WOVR_PAYLOAD_OFFSET + layout.wovr_payload_size) {
        memset(
            bytes + (size_t)(APTA_DUAL_WOVR_PAYLOAD_OFFSET +
                             layout.wovr_payload_size),
            0,
            (size_t)(layout.wdtl_payload_offset -
                     (APTA_DUAL_WOVR_PAYLOAD_OFFSET +
                      layout.wovr_payload_size)));
    }
    memset(
        bytes + (size_t)layout.wdtl_payload_offset,
        0,
        (size_t)layout.wdtl_payload_size);

    apta_wdtl_put_u32(
        bytes + 16u,
        ((uint32_t)bytes[16] |
         ((uint32_t)bytes[17] << 8u) |
         ((uint32_t)bytes[18] << 16u) |
         ((uint32_t)bytes[19] << 24u)) |
            layout.container_flags_or);
    apta_wdtl_put_u32(bytes + 20u, 2u);
    apta_wdtl_put_u64(bytes + 32u, layout.total_size);

    wovr_directory = bytes + APTA_DIRECTORY_OFFSET;
    wdtl_directory = wovr_directory + APTA_DIRECTORY_ENTRY_SIZE;
    wovr_payload = bytes + APTA_DUAL_WOVR_PAYLOAD_OFFSET;
    wdtl_payload = bytes + (size_t)layout.wdtl_payload_offset;

    memcpy(wovr_directory, "WOVR", 4u);
    apta_wdtl_put_u16(wovr_directory + 4u, 1u);
    apta_wdtl_put_u16(wovr_directory + 6u, APTA_SECTION_FLAG_REQUIRED);
    apta_wdtl_put_u64(
        wovr_directory + 8u,
        APTA_DUAL_WOVR_PAYLOAD_OFFSET);
    apta_wdtl_put_u64(wovr_directory + 16u, layout.wovr_payload_size);
    apta_wdtl_put_u64(wovr_directory + 24u, layout.wovr_payload_size);
    apta_wdtl_put_u32(
        wovr_directory + 32u,
        apta_internal_crc32c(
            wovr_payload,
            (size_t)layout.wovr_payload_size));

    memcpy(wdtl_directory, "WDTL", 4u);
    apta_wdtl_put_u16(wdtl_directory + 4u, 1u);
    apta_wdtl_put_u16(wdtl_directory + 6u, 0u);
    apta_wdtl_put_u64(wdtl_directory + 8u, layout.wdtl_payload_offset);
    apta_wdtl_put_u64(wdtl_directory + 16u, layout.wdtl_payload_size);
    apta_wdtl_put_u64(wdtl_directory + 24u, layout.wdtl_payload_size);

    apta_wdtl_put_u32(wdtl_payload + 0u, result->detail_tile_count);
    apta_wdtl_put_u32(wdtl_payload + 4u, 0u);
    apta_wdtl_put_u64(wdtl_payload + 8u, APTA_WDTL_HEADER_SIZE);

    columns_offset = APTA_WDTL_HEADER_SIZE +
                     (uint64_t)result->detail_tile_count *
                         APTA_WDTL_TILE_SIZE;
    packed_offset = 0u;

    for (tile_index = 0u; tile_index < result->detail_tile_count; ++tile_index) {
        const apta_waveform_tile_view_t *tile = &result->detail_tiles[tile_index];
        uint8_t *descriptor = wdtl_payload + APTA_WDTL_HEADER_SIZE +
                              (size_t)tile_index * APTA_WDTL_TILE_SIZE;
        uint32_t column_index;

        apta_wdtl_put_u32(descriptor + 0u, tile->level_id);
        apta_wdtl_put_u32(descriptor + 4u, tile->tile_index);
        apta_wdtl_put_u64(descriptor + 8u, tile->source_range.first_frame);
        apta_wdtl_put_u64(descriptor + 16u, tile->source_range.end_frame);
        apta_wdtl_put_u32(descriptor + 24u, tile->first_column_index);
        apta_wdtl_put_u32(descriptor + 28u, tile->column_count);
        apta_wdtl_put_u64(
            descriptor + 32u,
            columns_offset +
                (uint64_t)packed_offset * APTA_PACKED_COLUMN_SIZE);
        apta_wdtl_put_u32(descriptor + 40u, tile->state);
        apta_wdtl_put_u16(descriptor + 44u, 0u);
        descriptor[46] = tile->confidence;

        for (column_index = 0u; column_index < tile->column_count; ++column_index) {
            const apta_waveform_column_t *column = &tile->columns[column_index];
            uint8_t *output = wdtl_payload + (size_t)columns_offset +
                              (size_t)(packed_offset + column_index) *
                                  APTA_PACKED_COLUMN_SIZE;

            apta_wdtl_put_u16(output + 0u, (uint16_t)column->minimum);
            apta_wdtl_put_u16(output + 2u, (uint16_t)column->maximum);
            apta_wdtl_put_u16(output + 4u, column->rms);
            output[6] = column->low;
            output[7] = column->mid;
            output[8] = column->high;
            output[9] = column->flags;
        }

        packed_offset += tile->column_count;
    }

    apta_wdtl_put_u32(
        wdtl_directory + 32u,
        apta_internal_crc32c(
            wdtl_payload,
            (size_t)layout.wdtl_payload_size));
    apta_wdtl_put_u32(bytes + 92u, apta_internal_crc32c(bytes, 92u));

    *bytes_written_out = (size_t)layout.total_size;
    return APTA_STATUS_OK;
}
