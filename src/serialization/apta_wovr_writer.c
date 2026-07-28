// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <limits.h>
#include <string.h>

#define APTA_CONTAINER_HEADER_SIZE 96u
#define APTA_DIRECTORY_ENTRY_SIZE 40u
#define APTA_DIRECTORY_OFFSET 96u
#define APTA_WOVR_PAYLOAD_OFFSET 136u
#define APTA_WOVR_HEADER_SIZE 48u
#define APTA_WOVR_SPAN_SIZE 32u
#define APTA_PACKED_COLUMN_SIZE 10u

#define APTA_CONTAINER_FLAG_PARTIAL_RESULT          (1u << 0)
#define APTA_CONTAINER_FLAG_SOURCE_DURATION_UNKNOWN (1u << 1)
#define APTA_SECTION_FLAG_REQUIRED                   (1u << 0)
#define APTA_WOVR_STATE_MASK                         0x7u

typedef struct {
    uint64_t total_size;
    uint64_t payload_size;
    uint32_t packed_column_count;
    uint32_t logical_column_count;
    uint32_t container_flags;
    uint32_t wovr_flags;
} apta_serialization_layout_t;

static void apta_put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void apta_put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8u) & 0xFFu);
    destination[2] = (uint8_t)((value >> 16u) & 0xFFu);
    destination[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void apta_put_u64(uint8_t *destination, uint64_t value)
{
    apta_put_u32(destination, (uint32_t)(value & UINT32_MAX));
    apta_put_u32(destination + 4u, (uint32_t)(value >> 32u));
}

static int apta_serialization_options_are_valid(
    const apta_serialize_options_t *options)
{
    uint32_t index;

    if (options == NULL) {
        return 1;
    }

    if (!apta_internal_validate_struct(
            options,
            sizeof(*options),
            options->struct_size,
            options->api_version) ||
        (options->flags & ~APTA_SERIALIZE_CANONICAL) != 0u) {
        return 0;
    }

    for (index = 0u; index < 5u; ++index) {
        if (options->reserved32[index] != 0u) {
            return 0;
        }
    }
    for (index = 0u; index < 3u; ++index) {
        if (options->reserved64[index] != 0u) {
            return 0;
        }
    }

    return 1;
}

static apta_status_t apta_serialization_calculate_layout(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    apta_serialization_layout_t *layout)
{
    const apta_waveform_overview_view_t *overview;
    uint64_t packed_columns;
    uint64_t logical_columns;
    uint64_t span_bytes;
    uint64_t column_bytes;
    uint32_t span_index;
    apta_source_frame_t previous_end;

    if (result == NULL || layout == NULL ||
        !apta_serialization_options_are_valid(options)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if ((result->info.available_features & APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    overview = &result->overview;
    if (overview->level.frames_per_column == 0u ||
        overview->span_count == 0u || overview->spans == NULL ||
        overview->state < APTA_FEATURE_PARTIAL ||
        overview->state > APTA_FEATURE_FINAL ||
        result->source_sample_rate == 0u ||
        result->source_channel_count == 0u ||
        result->source_channel_count > UINT16_MAX ||
        result->source_channel_layout > UINT16_MAX) {
        return APTA_ERROR_INTERNAL;
    }

    packed_columns = 0u;
    logical_columns = 0u;
    previous_end = 0u;

    for (span_index = 0u; span_index < overview->span_count; ++span_index) {
        const apta_waveform_span_t *span = &overview->spans[span_index];
        uint64_t logical_end;
        uint32_t column_index;

        if (span->source_range.first_frame >= span->source_range.end_frame ||
            span->column_count == 0u || span->columns == NULL ||
            (span_index != 0u && span->source_range.first_frame < previous_end) ||
            UINT32_MAX - span->first_column_index < span->column_count) {
            return APTA_ERROR_INTERNAL;
        }

        logical_end = (uint64_t)span->first_column_index + span->column_count;
        if (logical_end > logical_columns) {
            logical_columns = logical_end;
        }
        if (UINT32_MAX - packed_columns < span->column_count) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        packed_columns += span->column_count;
        previous_end = span->source_range.end_frame;

        for (column_index = 0u; column_index < span->column_count; ++column_index) {
            const apta_waveform_column_t *column = &span->columns[column_index];
            if ((column->flags & APTA_WAVEFORM_COLUMN_VALID) != 0u &&
                column->minimum > column->maximum) {
                return APTA_ERROR_INTERNAL;
            }
        }
    }

    if (result->total_source_frames != APTA_TOTAL_FRAMES_UNKNOWN) {
        uint64_t relative_frames;
        uint64_t required_columns;

        if (result->total_source_frames < overview->level.origin_frame) {
            return APTA_ERROR_INTERNAL;
        }
        relative_frames = result->total_source_frames - overview->level.origin_frame;
        required_columns = relative_frames / overview->level.frames_per_column;
        if ((relative_frames % overview->level.frames_per_column) != 0u) {
            required_columns += 1u;
        }
        if (required_columns > UINT32_MAX || logical_columns > required_columns) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        logical_columns = required_columns;
    }

    if (logical_columns == 0u || logical_columns > UINT32_MAX) {
        return APTA_ERROR_INTERNAL;
    }

    span_bytes = (uint64_t)overview->span_count * APTA_WOVR_SPAN_SIZE;
    column_bytes = packed_columns * APTA_PACKED_COLUMN_SIZE;
    if (UINT64_MAX - APTA_WOVR_HEADER_SIZE < span_bytes ||
        UINT64_MAX - (APTA_WOVR_HEADER_SIZE + span_bytes) < column_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    layout->payload_size = APTA_WOVR_HEADER_SIZE + span_bytes + column_bytes;
    if (UINT64_MAX - APTA_WOVR_PAYLOAD_OFFSET < layout->payload_size) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    layout->total_size = APTA_WOVR_PAYLOAD_OFFSET + layout->payload_size;
    layout->packed_column_count = (uint32_t)packed_columns;
    layout->logical_column_count = (uint32_t)logical_columns;
    layout->container_flags = 0u;
    layout->wovr_flags = overview->state & APTA_WOVR_STATE_MASK;

    if (overview->state != APTA_FEATURE_FINAL ||
        result->info.session_state != APTA_SESSION_COMPLETED) {
        layout->container_flags |= APTA_CONTAINER_FLAG_PARTIAL_RESULT;
    }
    if (result->total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN) {
        layout->container_flags |= APTA_CONTAINER_FLAG_PARTIAL_RESULT |
                                   APTA_CONTAINER_FLAG_SOURCE_DURATION_UNKNOWN;
    }

    if (options != NULL && options->maximum_output_bytes != 0u &&
        layout->total_size > options->maximum_output_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    return APTA_STATUS_OK;
}

void APTA_CALL apta_serialize_options_init(apta_serialize_options_t *options)
{
    if (options != NULL) {
        memset(options, 0, sizeof(*options));
        options->struct_size = (uint32_t)sizeof(*options);
        options->api_version = APTA_API_VERSION;
        options->flags = APTA_SERIALIZE_CANONICAL;
    }
}

apta_status_t APTA_CALL apta_result_query_serialized_size(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out)
{
    apta_serialization_layout_t layout;
    apta_status_t status;

    if (size_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *size_out = 0u;

    status = apta_serialization_calculate_layout(result, options, &layout);
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
    apta_serialization_layout_t layout;
    const apta_waveform_overview_view_t *overview;
    uint8_t *bytes;
    uint8_t *directory;
    uint8_t *payload;
    uint64_t column_data_offset;
    uint32_t span_index;
    uint32_t packed_offset;
    apta_status_t status;

    if (bytes_written_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *bytes_written_out = 0u;

    status = apta_serialization_calculate_layout(result, options, &layout);
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

    bytes = (uint8_t *)buffer;
    memset(bytes, 0, (size_t)layout.total_size);
    overview = &result->overview;
    directory = bytes + APTA_DIRECTORY_OFFSET;
    payload = bytes + APTA_WOVR_PAYLOAD_OFFSET;
    column_data_offset = APTA_WOVR_HEADER_SIZE +
                         (uint64_t)overview->span_count * APTA_WOVR_SPAN_SIZE;

    bytes[0] = 'A';
    bytes[1] = 'P';
    bytes[2] = 'T';
    bytes[3] = 'A';
    apta_put_u16(bytes + 4u, APTA_CONTAINER_HEADER_SIZE);
    apta_put_u16(bytes + 6u, 1u);
    apta_put_u16(bytes + 8u, (uint16_t)APTA_SPEC_VERSION_MAJOR);
    apta_put_u16(bytes + 10u, (uint16_t)APTA_SPEC_VERSION_MINOR);
    apta_put_u32(bytes + 12u, APTA_API_VERSION);
    apta_put_u32(bytes + 16u, layout.container_flags);
    apta_put_u32(bytes + 20u, 1u);
    apta_put_u64(bytes + 24u, APTA_DIRECTORY_OFFSET);
    apta_put_u64(bytes + 32u, layout.total_size);
    apta_put_u64(bytes + 40u, result->total_source_frames);
    apta_put_u32(bytes + 48u, result->source_sample_rate);
    apta_put_u16(bytes + 52u, (uint16_t)result->source_channel_count);
    apta_put_u16(bytes + 54u, (uint16_t)result->source_channel_layout);
    apta_put_u32(bytes + 88u, 0u);

    directory[0] = 'W';
    directory[1] = 'O';
    directory[2] = 'V';
    directory[3] = 'R';
    apta_put_u16(directory + 4u, 1u);
    apta_put_u16(directory + 6u, APTA_SECTION_FLAG_REQUIRED);
    apta_put_u64(directory + 8u, APTA_WOVR_PAYLOAD_OFFSET);
    apta_put_u64(directory + 16u, layout.payload_size);
    apta_put_u64(directory + 24u, layout.payload_size);

    apta_put_u32(payload + 0u, overview->level.level_id);
    apta_put_u32(payload + 4u, overview->level.frames_per_column);
    apta_put_u64(payload + 8u, overview->level.origin_frame);
    apta_put_u32(payload + 16u, layout.logical_column_count);
    apta_put_u32(payload + 20u, overview->span_count);
    apta_put_u64(payload + 24u, APTA_WOVR_HEADER_SIZE);
    apta_put_u64(payload + 32u, column_data_offset);
    apta_put_u32(payload + 40u, layout.wovr_flags);

    packed_offset = 0u;
    for (span_index = 0u; span_index < overview->span_count; ++span_index) {
        const apta_waveform_span_t *span = &overview->spans[span_index];
        uint8_t *span_output = payload + APTA_WOVR_HEADER_SIZE +
                               (size_t)span_index * APTA_WOVR_SPAN_SIZE;
        uint32_t column_index;

        apta_put_u64(span_output + 0u, span->source_range.first_frame);
        apta_put_u64(span_output + 8u, span->source_range.end_frame);
        apta_put_u32(span_output + 16u, span->first_column_index);
        apta_put_u32(span_output + 20u, span->column_count);
        apta_put_u32(span_output + 24u, packed_offset);

        for (column_index = 0u; column_index < span->column_count; ++column_index) {
            const apta_waveform_column_t *column = &span->columns[column_index];
            uint8_t *column_output = payload + (size_t)column_data_offset +
                                     (size_t)(packed_offset + column_index) *
                                         APTA_PACKED_COLUMN_SIZE;

            apta_put_u16(column_output + 0u, (uint16_t)column->minimum);
            apta_put_u16(column_output + 2u, (uint16_t)column->maximum);
            apta_put_u16(column_output + 4u, column->rms);
            column_output[6] = column->low;
            column_output[7] = column->mid;
            column_output[8] = column->high;
            column_output[9] = column->flags;
        }

        packed_offset += span->column_count;
    }

    apta_put_u32(
        directory + 32u,
        apta_internal_crc32c(payload, (size_t)layout.payload_size));
    apta_put_u32(bytes + 92u, apta_internal_crc32c(bytes, 92u));

    *bytes_written_out = (size_t)layout.total_size;
    return APTA_STATUS_OK;
}