// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <limits.h>
#include <string.h>

#define APTA_DIRECTORY_ENTRY_SIZE 40u
#define APTA_DIRECTORY_OFFSET 96u

APTA_API apta_status_t APTA_CALL
apta_result_query_serialized_size_waveform(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out);

APTA_API apta_status_t APTA_CALL
apta_result_serialize_waveform(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written_out);

typedef struct {
    uint64_t waveform_size;
    uint64_t meta_payload_size;
    uint64_t meta_payload_offset;
    uint64_t total_size;
    uint32_t has_meta;
} apta_meta_write_layout_t;

static uint32_t apta_meta_get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8u) |
           ((uint32_t)source[2] << 16u) |
           ((uint32_t)source[3] << 24u);
}

static uint64_t apta_meta_get_u64(const uint8_t *source)
{
    return (uint64_t)apta_meta_get_u32(source) |
           ((uint64_t)apta_meta_get_u32(source + 4u) << 32u);
}

static void apta_meta_put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void apta_meta_put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8u) & 0xFFu);
    destination[2] = (uint8_t)((value >> 16u) & 0xFFu);
    destination[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void apta_meta_put_u64(uint8_t *destination, uint64_t value)
{
    apta_meta_put_u32(destination, (uint32_t)(value & UINT32_MAX));
    apta_meta_put_u32(destination + 4u, (uint32_t)(value >> 32u));
}

static apta_status_t apta_meta_align8(
    uint64_t value,
    uint64_t *aligned_out)
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

static uint64_t apta_cbor_head_size(uint64_t value)
{
    if (value < 24u) {
        return 1u;
    }
    if (value <= UINT8_MAX) {
        return 2u;
    }
    if (value <= UINT16_MAX) {
        return 3u;
    }
    if (value <= UINT32_MAX) {
        return 5u;
    }
    return 9u;
}

static uint8_t *apta_cbor_write_head(
    uint8_t *output,
    uint8_t major_type,
    uint64_t value)
{
    if (value < 24u) {
        *output++ = (uint8_t)((major_type << 5u) | (uint8_t)value);
    } else if (value <= UINT8_MAX) {
        *output++ = (uint8_t)((major_type << 5u) | 24u);
        *output++ = (uint8_t)value;
    } else if (value <= UINT16_MAX) {
        *output++ = (uint8_t)((major_type << 5u) | 25u);
        *output++ = (uint8_t)(value >> 8u);
        *output++ = (uint8_t)value;
    } else if (value <= UINT32_MAX) {
        *output++ = (uint8_t)((major_type << 5u) | 26u);
        *output++ = (uint8_t)(value >> 24u);
        *output++ = (uint8_t)(value >> 16u);
        *output++ = (uint8_t)(value >> 8u);
        *output++ = (uint8_t)value;
    } else {
        *output++ = (uint8_t)((major_type << 5u) | 27u);
        *output++ = (uint8_t)(value >> 56u);
        *output++ = (uint8_t)(value >> 48u);
        *output++ = (uint8_t)(value >> 40u);
        *output++ = (uint8_t)(value >> 32u);
        *output++ = (uint8_t)(value >> 24u);
        *output++ = (uint8_t)(value >> 16u);
        *output++ = (uint8_t)(value >> 8u);
        *output++ = (uint8_t)value;
    }
    return output;
}

static int apta_meta_flag(
    const apta_metadata_view_t *metadata,
    uint32_t flag)
{
    return (metadata->flags & flag) != 0u;
}

static uint32_t apta_meta_item_count(const apta_metadata_view_t *metadata)
{
    uint32_t count = 0u;

    count += apta_meta_flag(
        metadata,
        APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT);
    count += apta_meta_flag(
        metadata,
        APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT);
    count += apta_meta_flag(
        metadata,
        APTA_METADATA_FLAG_BACKEND_NAME_PRESENT);
    count += apta_meta_flag(
        metadata,
        APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT);
    count += apta_meta_flag(
        metadata,
        APTA_METADATA_FLAG_CREATION_TIME_PRESENT);
    count += metadata->application_source_id_kind !=
             APTA_METADATA_SOURCE_ID_NONE;
    count += apta_meta_flag(
        metadata,
        APTA_METADATA_FLAG_COMMENTS_PRESENT);
    return count;
}

static uint64_t apta_meta_text_item_size(uint32_t size)
{
    return 1u + apta_cbor_head_size(size) + size;
}

static apta_status_t apta_meta_measure_payload(
    const apta_metadata_view_t *metadata,
    uint64_t *size_out)
{
    uint64_t size;
    uint32_t item_count;

    if (metadata == NULL || size_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    item_count = apta_meta_item_count(metadata);
    size = apta_cbor_head_size(item_count);

    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT)) {
        size += apta_meta_text_item_size(metadata->producer_name.size);
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT)) {
        size += apta_meta_text_item_size(
            metadata->producer_version_string.size);
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_BACKEND_NAME_PRESENT)) {
        size += apta_meta_text_item_size(metadata->backend_name.size);
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT)) {
        size += apta_meta_text_item_size(metadata->backend_version.size);
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_CREATION_TIME_PRESENT)) {
        size += 1u + apta_cbor_head_size(metadata->creation_unix_time);
    }
    if (metadata->application_source_id_kind !=
        APTA_METADATA_SOURCE_ID_NONE) {
        size += 1u +
                apta_cbor_head_size(metadata->application_source_id.size) +
                metadata->application_source_id.size;
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_COMMENTS_PRESENT)) {
        size += apta_meta_text_item_size(metadata->comments.size);
    }

    *size_out = size;
    return APTA_STATUS_OK;
}

static apta_status_t apta_meta_calculate_layout(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    apta_meta_write_layout_t *layout)
{
    apta_status_t status;
    uint64_t shifted_end;

    if (result == NULL || layout == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    memset(layout, 0, sizeof(*layout));

    status = apta_result_query_serialized_size_waveform(
        result,
        options,
        &layout->waveform_size);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) {
        return status;
    }

    if (!apta_internal_metadata_is_present(&result->metadata)) {
        layout->total_size = layout->waveform_size;
        return APTA_STATUS_OK;
    }

    status = apta_meta_measure_payload(
        &result->metadata.view,
        &layout->meta_payload_size);
    if (status < 0) {
        return status;
    }

    if (layout->waveform_size > UINT64_MAX - APTA_DIRECTORY_ENTRY_SIZE) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    shifted_end = layout->waveform_size + APTA_DIRECTORY_ENTRY_SIZE;
    status = apta_meta_align8(shifted_end, &layout->meta_payload_offset);
    if (status < 0 ||
        layout->meta_payload_offset >
            UINT64_MAX - layout->meta_payload_size) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    layout->total_size =
        layout->meta_payload_offset + layout->meta_payload_size;
    layout->has_meta = 1u;

    if (options != NULL && options->maximum_output_bytes != 0u &&
        layout->total_size > options->maximum_output_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    return APTA_STATUS_OK;
}

static uint8_t *apta_meta_write_text(
    uint8_t *output,
    uint8_t key,
    const apta_utf8_view_t *text)
{
    *output++ = key;
    output = apta_cbor_write_head(output, 3u, text->size);
    if (text->size != 0u) {
        memcpy(output, text->data, text->size);
        output += text->size;
    }
    return output;
}

static uint8_t *apta_meta_write_payload(
    uint8_t *output,
    const apta_metadata_view_t *metadata)
{
    uint32_t item_count = apta_meta_item_count(metadata);

    output = apta_cbor_write_head(output, 5u, item_count);
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT)) {
        output = apta_meta_write_text(
            output,
            1u,
            &metadata->producer_name);
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT)) {
        output = apta_meta_write_text(
            output,
            2u,
            &metadata->producer_version_string);
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_BACKEND_NAME_PRESENT)) {
        output = apta_meta_write_text(
            output,
            3u,
            &metadata->backend_name);
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT)) {
        output = apta_meta_write_text(
            output,
            4u,
            &metadata->backend_version);
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_CREATION_TIME_PRESENT)) {
        *output++ = 5u;
        output = apta_cbor_write_head(
            output,
            0u,
            metadata->creation_unix_time);
    }
    if (metadata->application_source_id_kind !=
        APTA_METADATA_SOURCE_ID_NONE) {
        *output++ = 6u;
        output = apta_cbor_write_head(
            output,
            metadata->application_source_id_kind ==
                    APTA_METADATA_SOURCE_ID_TEXT
                ? 3u
                : 2u,
            metadata->application_source_id.size);
        if (metadata->application_source_id.size != 0u) {
            memcpy(
                output,
                metadata->application_source_id.data,
                metadata->application_source_id.size);
            output += metadata->application_source_id.size;
        }
    }
    if (apta_meta_flag(
            metadata,
            APTA_METADATA_FLAG_COMMENTS_PRESENT)) {
        output = apta_meta_write_text(
            output,
            7u,
            &metadata->comments);
    }
    return output;
}

apta_status_t APTA_CALL apta_result_query_serialized_size(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out)
{
    apta_meta_write_layout_t layout;
    apta_status_t status;

    if (size_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *size_out = 0u;

    status = apta_meta_calculate_layout(result, options, &layout);
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
    apta_meta_write_layout_t layout;
    uint8_t *bytes;
    uint8_t *directory;
    uint8_t *meta_directory;
    uint8_t *meta_payload;
    uint64_t first_payload_offset;
    uint32_t section_count;
    uint32_t index;
    size_t waveform_written;
    uint8_t *meta_end;
    apta_status_t status;

    if (bytes_written_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *bytes_written_out = 0u;

    status = apta_meta_calculate_layout(result, options, &layout);
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

    status = apta_result_serialize_waveform(
        result,
        options,
        buffer,
        buffer_size,
        &waveform_written);
    if (status < 0) {
        return status;
    }
    if (!layout.has_meta) {
        *bytes_written_out = waveform_written;
        return APTA_STATUS_OK;
    }
    if (waveform_written != (size_t)layout.waveform_size) {
        return APTA_ERROR_INTERNAL;
    }

    bytes = (uint8_t *)buffer;
    section_count = apta_meta_get_u32(bytes + 20u);
    if (section_count == 0u ||
        section_count > UINT32_MAX - 1u ||
        apta_meta_get_u64(bytes + 24u) != APTA_DIRECTORY_OFFSET) {
        return APTA_ERROR_INTERNAL;
    }

    directory = bytes + APTA_DIRECTORY_OFFSET;
    first_payload_offset = UINT64_MAX;
    for (index = 0u; index < section_count; ++index) {
        uint8_t *entry = directory +
                         (size_t)index * APTA_DIRECTORY_ENTRY_SIZE;
        uint64_t section_offset = apta_meta_get_u64(entry + 8u);
        if (section_offset < first_payload_offset) {
            first_payload_offset = section_offset;
        }
    }
    if (first_payload_offset == UINT64_MAX ||
        first_payload_offset > layout.waveform_size ||
        first_payload_offset <
            APTA_DIRECTORY_OFFSET +
                (uint64_t)section_count * APTA_DIRECTORY_ENTRY_SIZE) {
        return APTA_ERROR_INTERNAL;
    }

    memmove(
        bytes + (size_t)first_payload_offset + APTA_DIRECTORY_ENTRY_SIZE,
        bytes + (size_t)first_payload_offset,
        (size_t)(layout.waveform_size - first_payload_offset));

    for (index = 0u; index < section_count; ++index) {
        uint8_t *entry = directory +
                         (size_t)index * APTA_DIRECTORY_ENTRY_SIZE;
        uint64_t section_offset = apta_meta_get_u64(entry + 8u);
        apta_meta_put_u64(
            entry + 8u,
            section_offset + APTA_DIRECTORY_ENTRY_SIZE);
    }

    meta_directory = directory +
                     (size_t)section_count * APTA_DIRECTORY_ENTRY_SIZE;
    memset(meta_directory, 0, APTA_DIRECTORY_ENTRY_SIZE);

    if (layout.meta_payload_offset >
        layout.waveform_size + APTA_DIRECTORY_ENTRY_SIZE) {
        memset(
            bytes + (size_t)(layout.waveform_size +
                             APTA_DIRECTORY_ENTRY_SIZE),
            0,
            (size_t)(layout.meta_payload_offset -
                     (layout.waveform_size +
                      APTA_DIRECTORY_ENTRY_SIZE)));
    }
    meta_payload = bytes + (size_t)layout.meta_payload_offset;
    memset(meta_payload, 0, (size_t)layout.meta_payload_size);
    meta_end = apta_meta_write_payload(
        meta_payload,
        &result->metadata.view);
    if ((uint64_t)(meta_end - meta_payload) != layout.meta_payload_size) {
        return APTA_ERROR_INTERNAL;
    }

    memcpy(meta_directory, "META", 4u);
    apta_meta_put_u16(meta_directory + 4u, 1u);
    apta_meta_put_u16(meta_directory + 6u, 0u);
    apta_meta_put_u64(meta_directory + 8u, layout.meta_payload_offset);
    apta_meta_put_u64(meta_directory + 16u, layout.meta_payload_size);
    apta_meta_put_u64(meta_directory + 24u, layout.meta_payload_size);
    apta_meta_put_u32(
        meta_directory + 32u,
        apta_internal_crc32c(
            meta_payload,
            (size_t)layout.meta_payload_size));

    apta_meta_put_u32(bytes + 20u, section_count + 1u);
    apta_meta_put_u64(bytes + 32u, layout.total_size);
    apta_meta_put_u32(bytes + 92u, apta_internal_crc32c(bytes, 92u));

    *bytes_written_out = (size_t)layout.total_size;
    return APTA_STATUS_OK;
}
