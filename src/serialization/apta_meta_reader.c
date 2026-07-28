// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <stdint.h>
#include <string.h>

#define APTA_DIRECTORY_ENTRY_SIZE 40u
#define APTA_META_MAX_MAP_ITEMS 64u
#define APTA_META_MAX_CBOR_DEPTH 8u
#define APTA_META_MAX_CBOR_ITEMS 256u
#define APTA_PARSE_DEFAULT_MAX_ALLOCATION_BYTES UINT64_C(268435456)

APTA_API apta_status_t APTA_CALL apta_result_parse_waveform(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out);

typedef struct {
    const uint8_t *cursor;
    const uint8_t *end;
    uint32_t visited_items;
} apta_meta_cursor_t;

static uint16_t apta_meta_get_u16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8u));
}

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

static int apta_meta_take(
    apta_meta_cursor_t *cursor,
    uint64_t size,
    const uint8_t **data_out)
{
    uint64_t remaining;

    if (cursor == NULL || data_out == NULL ||
        cursor->cursor > cursor->end) {
        return 0;
    }
    remaining = (uint64_t)(cursor->end - cursor->cursor);
    if (size > remaining || size > SIZE_MAX) {
        return 0;
    }

    *data_out = cursor->cursor;
    cursor->cursor += (size_t)size;
    return 1;
}

static int apta_meta_read_argument(
    apta_meta_cursor_t *cursor,
    uint8_t additional,
    uint64_t *value_out)
{
    const uint8_t *data;
    uint64_t value;

    if (additional < 24u) {
        *value_out = additional;
        return 1;
    }
    if (additional == 24u) {
        if (!apta_meta_take(cursor, 1u, &data) || data[0] < 24u) {
            return 0;
        }
        *value_out = data[0];
        return 1;
    }
    if (additional == 25u) {
        if (!apta_meta_take(cursor, 2u, &data)) {
            return 0;
        }
        value = ((uint64_t)data[0] << 8u) | data[1];
        if (value <= UINT8_MAX) {
            return 0;
        }
        *value_out = value;
        return 1;
    }
    if (additional == 26u) {
        if (!apta_meta_take(cursor, 4u, &data)) {
            return 0;
        }
        value = ((uint64_t)data[0] << 24u) |
                ((uint64_t)data[1] << 16u) |
                ((uint64_t)data[2] << 8u) |
                data[3];
        if (value <= UINT16_MAX) {
            return 0;
        }
        *value_out = value;
        return 1;
    }
    if (additional == 27u) {
        if (!apta_meta_take(cursor, 8u, &data)) {
            return 0;
        }
        value = ((uint64_t)data[0] << 56u) |
                ((uint64_t)data[1] << 48u) |
                ((uint64_t)data[2] << 40u) |
                ((uint64_t)data[3] << 32u) |
                ((uint64_t)data[4] << 24u) |
                ((uint64_t)data[5] << 16u) |
                ((uint64_t)data[6] << 8u) |
                data[7];
        if (value <= UINT32_MAX) {
            return 0;
        }
        *value_out = value;
        return 1;
    }
    return 0;
}

static int apta_meta_read_head(
    apta_meta_cursor_t *cursor,
    uint8_t *major_out,
    uint64_t *value_out)
{
    uint8_t initial;

    if (cursor == NULL || major_out == NULL || value_out == NULL ||
        cursor->cursor == cursor->end) {
        return 0;
    }
    initial = *cursor->cursor++;
    *major_out = initial >> 5u;
    return apta_meta_read_argument(
        cursor,
        initial & 0x1Fu,
        value_out);
}

static int apta_meta_skip_item(
    apta_meta_cursor_t *cursor,
    uint32_t depth)
{
    uint8_t initial;
    uint8_t major;
    uint8_t additional;
    uint64_t value;
    const uint8_t *ignored;
    uint64_t index;

    if (cursor == NULL || depth > APTA_META_MAX_CBOR_DEPTH ||
        cursor->cursor == cursor->end ||
        cursor->visited_items >= APTA_META_MAX_CBOR_ITEMS) {
        return 0;
    }
    cursor->visited_items += 1u;

    initial = *cursor->cursor++;
    major = initial >> 5u;
    additional = initial & 0x1Fu;

    if (major == 7u) {
        if (additional < 24u) {
            return 1;
        }
        if (additional == 24u) {
            if (!apta_meta_take(cursor, 1u, &ignored)) {
                return 0;
            }
            return ignored[0] >= 32u;
        }
        if (additional == 25u) {
            return apta_meta_take(cursor, 2u, &ignored);
        }
        if (additional == 26u) {
            return apta_meta_take(cursor, 4u, &ignored);
        }
        if (additional == 27u) {
            return apta_meta_take(cursor, 8u, &ignored);
        }
        return 0;
    }

    if (!apta_meta_read_argument(cursor, additional, &value)) {
        return 0;
    }

    if (major == 0u || major == 1u) {
        return 1;
    }
    if (major == 2u || major == 3u) {
        if (value > APTA_METADATA_MAX_TOTAL_BYTES) {
            return 0;
        }
        return apta_meta_take(cursor, value, &ignored);
    }
    if (major == 4u) {
        if (value > APTA_META_MAX_CBOR_ITEMS) {
            return 0;
        }
        for (index = 0u; index < value; ++index) {
            if (!apta_meta_skip_item(cursor, depth + 1u)) {
                return 0;
            }
        }
        return 1;
    }
    if (major == 5u) {
        if (value > APTA_META_MAX_CBOR_ITEMS / 2u) {
            return 0;
        }
        for (index = 0u; index < value; ++index) {
            if (!apta_meta_skip_item(cursor, depth + 1u) ||
                !apta_meta_skip_item(cursor, depth + 1u)) {
                return 0;
            }
        }
        return 1;
    }
    if (major == 6u) {
        return apta_meta_skip_item(cursor, depth + 1u);
    }
    return 0;
}

static int apta_meta_read_text(
    apta_meta_cursor_t *cursor,
    uint32_t maximum_size,
    apta_utf8_view_t *view_out)
{
    uint8_t major;
    uint64_t size;
    const uint8_t *data;

    if (!apta_meta_read_head(cursor, &major, &size) ||
        major != 3u || size > maximum_size || size > UINT32_MAX ||
        !apta_meta_take(cursor, size, &data)) {
        return 0;
    }
    view_out->data = size == 0u ? NULL : (const char *)data;
    view_out->size = (uint32_t)size;
    return 1;
}

static int apta_meta_read_source_id(
    apta_meta_cursor_t *cursor,
    apta_metadata_view_t *metadata)
{
    uint8_t major;
    uint64_t size;
    const uint8_t *data;

    if (!apta_meta_read_head(cursor, &major, &size) ||
        (major != 2u && major != 3u) ||
        size > APTA_METADATA_MAX_SOURCE_ID_BYTES ||
        size > UINT32_MAX || !apta_meta_take(cursor, size, &data)) {
        return 0;
    }

    metadata->application_source_id.data = size == 0u ? NULL : data;
    metadata->application_source_id.size = (uint32_t)size;
    metadata->application_source_id_kind =
        major == 3u ? APTA_METADATA_SOURCE_ID_TEXT
                    : APTA_METADATA_SOURCE_ID_BYTES;
    return 1;
}

static int apta_meta_parse_payload(
    const uint8_t *payload,
    size_t payload_size,
    apta_metadata_view_t *metadata_out)
{
    apta_meta_cursor_t cursor;
    uint8_t major;
    uint64_t map_count;
    uint64_t previous_key = 0u;
    uint64_t index;
    int have_previous = 0;

    cursor.cursor = payload;
    cursor.end = payload + payload_size;
    cursor.visited_items = 0u;
    apta_metadata_view_init(metadata_out);

    if (!apta_meta_read_head(&cursor, &major, &map_count) ||
        major != 5u || map_count > APTA_META_MAX_MAP_ITEMS) {
        return 0;
    }

    for (index = 0u; index < map_count; ++index) {
        uint64_t key;

        if (!apta_meta_read_head(&cursor, &major, &key) || major != 0u ||
            (have_previous && key <= previous_key)) {
            return 0;
        }
        previous_key = key;
        have_previous = 1;

        switch (key) {
        case 1u:
            if (!apta_meta_read_text(
                    &cursor,
                    APTA_METADATA_MAX_PRODUCER_NAME_BYTES,
                    &metadata_out->producer_name)) {
                return 0;
            }
            metadata_out->flags |=
                APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
            break;
        case 2u:
            if (!apta_meta_read_text(
                    &cursor,
                    APTA_METADATA_MAX_VERSION_STRING_BYTES,
                    &metadata_out->producer_version_string)) {
                return 0;
            }
            metadata_out->flags |=
                APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT;
            break;
        case 3u:
            if (!apta_meta_read_text(
                    &cursor,
                    APTA_METADATA_MAX_BACKEND_NAME_BYTES,
                    &metadata_out->backend_name)) {
                return 0;
            }
            metadata_out->flags |=
                APTA_METADATA_FLAG_BACKEND_NAME_PRESENT;
            break;
        case 4u:
            if (!apta_meta_read_text(
                    &cursor,
                    APTA_METADATA_MAX_VERSION_STRING_BYTES,
                    &metadata_out->backend_version)) {
                return 0;
            }
            metadata_out->flags |=
                APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT;
            break;
        case 5u:
            if (!apta_meta_read_head(
                    &cursor,
                    &major,
                    &metadata_out->creation_unix_time) ||
                major != 0u) {
                return 0;
            }
            metadata_out->flags |=
                APTA_METADATA_FLAG_CREATION_TIME_PRESENT;
            break;
        case 6u:
            if (!apta_meta_read_source_id(&cursor, metadata_out)) {
                return 0;
            }
            break;
        case 7u:
            if (!apta_meta_read_text(
                    &cursor,
                    APTA_METADATA_MAX_COMMENTS_BYTES,
                    &metadata_out->comments)) {
                return 0;
            }
            metadata_out->flags |=
                APTA_METADATA_FLAG_COMMENTS_PRESENT;
            break;
        default:
            if (!apta_meta_skip_item(&cursor, 1u)) {
                return 0;
            }
            break;
        }
    }

    return cursor.cursor == cursor.end;
}

static apta_status_t apta_meta_find_section(
    const uint8_t *bytes,
    size_t buffer_size,
    const uint8_t **payload_out,
    size_t *payload_size_out)
{
    uint32_t section_count = apta_meta_get_u32(bytes + 20u);
    uint64_t directory_offset = apta_meta_get_u64(bytes + 24u);
    uint32_t index;
    int found = 0;

    *payload_out = NULL;
    *payload_size_out = 0u;

    for (index = 0u; index < section_count; ++index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)index * APTA_DIRECTORY_ENTRY_SIZE;
        uint64_t offset;
        uint64_t size;

        if (memcmp(entry, "META", 4u) != 0) {
            continue;
        }
        if (found) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        found = 1;
        if (apta_meta_get_u16(entry + 4u) != 1u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        offset = apta_meta_get_u64(entry + 8u);
        size = apta_meta_get_u64(entry + 16u);
        if (offset > buffer_size || size > buffer_size - offset ||
            size > SIZE_MAX) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        *payload_out = bytes + (size_t)offset;
        *payload_size_out = (size_t)size;
    }

    return APTA_STATUS_OK;
}

static uint64_t apta_meta_existing_result_bytes(const apta_result_t *result)
{
    uint64_t total = sizeof(*result);
    uint32_t index;

    total += (uint64_t)result->overview.span_count *
             sizeof(apta_waveform_span_t);
    for (index = 0u; index < result->overview.span_count; ++index) {
        total += (uint64_t)result->overview.spans[index].column_count *
                 sizeof(apta_waveform_column_t);
    }
    total += (uint64_t)result->detail_tile_count *
             sizeof(apta_waveform_tile_view_t);
    for (index = 0u; index < result->detail_tile_count; ++index) {
        total += (uint64_t)result->detail_tiles[index].column_count *
                 sizeof(apta_waveform_column_t);
    }
    return total;
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
    const uint8_t *meta_payload;
    size_t meta_payload_size;
    apta_metadata_view_t metadata;
    uint64_t maximum_allocation_bytes =
        options != NULL && options->maximum_allocation_bytes != 0u
            ? options->maximum_allocation_bytes
            : APTA_PARSE_DEFAULT_MAX_ALLOCATION_BYTES;
    uint64_t existing_bytes;
    uint64_t metadata_bytes;
    apta_status_t status;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;

    status = apta_result_parse_waveform(
        context,
        options,
        buffer,
        buffer_size,
        &parsed_const);
    if (status < 0) {
        return status;
    }

    status = apta_meta_find_section(
        (const uint8_t *)buffer,
        buffer_size,
        &meta_payload,
        &meta_payload_size);
    if (status < 0) {
        apta_result_release(parsed_const);
        return status;
    }
    if (meta_payload == NULL) {
        *result_out = parsed_const;
        return APTA_STATUS_OK;
    }

    if (!apta_meta_parse_payload(
            meta_payload,
            meta_payload_size,
            &metadata)) {
        apta_result_release(parsed_const);
        return APTA_ERROR_CORRUPT_DATA;
    }

    metadata_bytes = (uint64_t)metadata.producer_name.size +
                     metadata.producer_version_string.size +
                     metadata.backend_name.size +
                     metadata.backend_version.size +
                     metadata.application_source_id.size +
                     metadata.comments.size;
    existing_bytes = apta_meta_existing_result_bytes(parsed_const);
    if (existing_bytes > maximum_allocation_bytes ||
        metadata_bytes > maximum_allocation_bytes - existing_bytes) {
        apta_result_release(parsed_const);
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    parsed = (apta_result_t *)parsed_const;
    status = apta_internal_metadata_copy_from_view(
        context,
        &metadata,
        &parsed->metadata);
    if (status < 0) {
        apta_result_release(parsed_const);
        return status;
    }

    *result_out = parsed_const;
    return APTA_STATUS_OK;
}
