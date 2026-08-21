// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_result_builder_internal.h"
#include "../beatgrid/apta_s6_internal.h"

#include <stdint.h>
#include <string.h>

#define APTA_STREAM_HEADER_SIZE 96u
#define APTA_STREAM_ENTRY_SIZE 40u
#define APTA_STREAM_MAX_SECTIONS 10u
#define APTA_STREAM_FLAG_PARTIAL (1u << 0)
#define APTA_STREAM_FLAG_DURATION_UNKNOWN (1u << 1)

typedef enum {
    APTA_STREAM_SECTION_WOVR,
    APTA_STREAM_SECTION_WDTL,
    APTA_STREAM_SECTION_META,
    APTA_STREAM_SECTION_TEMP,
    APTA_STREAM_SECTION_LGRD,
    APTA_STREAM_SECTION_GGRD,
    APTA_STREAM_SECTION_REVN,
    APTA_STREAM_SECTION_MKEY,
    APTA_STREAM_SECTION_MTRD,
    APTA_STREAM_SECTION_CONF
} apta_stream_section_kind_t;

typedef struct {
    char id[4];
    apta_stream_section_kind_t kind;
    uint64_t offset;
    uint64_t size;
    uint32_t crc;
    uint16_t flags;
} apta_stream_output_section_t;

typedef struct {
    const apta_output_stream_t *stream;
    uint32_t crc;
    int write;
} apta_stream_emitter_t;

typedef struct {
    char id[4];
    uint64_t offset;
    uint64_t size;
    uint32_t crc;
    uint16_t flags;
    uint16_t version;
    int present;
} apta_stream_input_section_t;

typedef struct {
    uint32_t flags;
    uint32_t maximum_section_count;
    uint32_t maximum_overview_spans;
    uint32_t maximum_waveform_columns;
    apta_feature_mask_t requested_features;
    uint64_t maximum_input_bytes;
    uint64_t maximum_section_bytes;
    uint64_t maximum_allocation_bytes;
    uint8_t *scratch;
    uint64_t scratch_size;
    int owns_scratch;
} apta_stream_effective_options_t;

typedef struct {
    uint8_t header[APTA_STREAM_HEADER_SIZE];
    uint64_t file_size;
    uint64_t directory_offset;
    uint32_t section_count;
    uint32_t container_flags;
    apta_stream_input_section_t known[APTA_STREAM_MAX_SECTIONS];
} apta_stream_input_layout_t;

static apta_status_t apta_stream_validation_status(apta_status_t status)
{
    if (status >= 0 || status == APTA_ERROR_UNSUPPORTED ||
        status == APTA_ERROR_LIMIT_EXCEEDED) {
        return status;
    }
    return APTA_ERROR_CORRUPT_DATA;
}

static apta_status_t apta_stream_validate_materialized_result(
    const apta_stream_effective_options_t *options, const apta_result_t *result)
{
    apta_result_builder_t validator;
    apta_waveform_detail_input_t detail;
    uint32_t ignored_count;
    uint32_t index;
    apta_status_t status;

    memset(&validator, 0, sizeof(validator));
    apta_result_builder_options_init(&validator.options);
    validator.options.maximum_overview_spans = options->maximum_overview_spans;
    validator.options.maximum_waveform_columns =
        options->maximum_waveform_columns;
    validator.options.maximum_allocation_bytes =
        options->maximum_allocation_bytes;
    validator.has_source = 1u;
    validator.source = result->source_info;

    if ((result->info.available_features & APTA_FEATURE_WAVEFORM_OVERVIEW) !=
        0u) {
        status = apta_builder_validate_overview(&validator, &result->overview,
                                                &ignored_count);
        if (status < 0)
            return apta_stream_validation_status(status);
    }
    if ((result->info.available_features & APTA_FEATURE_WAVEFORM_DETAIL) !=
        0u) {
        apta_waveform_detail_input_init(&detail);
        detail.tile_count = result->detail_tile_count;
        detail.tiles = result->detail_tiles;
        status =
            apta_builder_validate_detail(&validator, &detail, &ignored_count);
        if (status < 0)
            return apta_stream_validation_status(status);
    }
    if ((result->info.available_features & APTA_FEATURE_BPM) != 0u) {
        status = apta_builder_validate_tempo(&validator, &result->tempo);
        if (status < 0)
            return apta_stream_validation_status(status);
    }
    if ((result->info.available_features & APTA_FEATURE_LOCAL_BEATGRID) != 0u) {
        status = apta_builder_validate_grid(&validator, &result->local_grid);
        if (status >= 0) {
            status = apta_builder_validate_grid_modifiers(
                APTA_FEATURE_LOCAL_BEATGRID, &result->local_grid);
        }
        if (status < 0)
            return apta_stream_validation_status(status);
    }
    if ((result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) !=
        0u) {
        validator.global_grid.view = result->s6->global_grid;
        status =
            apta_builder_validate_grid(&validator, &result->s6->global_grid);
        if (status >= 0) {
            status = apta_builder_validate_grid_modifiers(
                APTA_FEATURE_GLOBAL_BEATGRID, &result->s6->global_grid);
        }
        if (status >= 0) {
            status = apta_builder_validate_grid_revision(&validator,
                                                         &result->s6->revision);
        }
        if (status < 0)
            return apta_stream_validation_status(status);
    }
    if ((result->info.available_features & APTA_FEATURE_MUSICAL_KEY) != 0u) {
        status = apta_builder_validate_key(&validator, &result->key);
        if (status < 0)
            return apta_stream_validation_status(status);
    }
    if ((result->info.available_features & APTA_FEATURE_METER_DOWNBEAT) != 0u) {
        status = apta_builder_validate_meter(&validator, &result->meter);
        if (status < 0)
            return apta_stream_validation_status(status);
    }
    for (index = 0u; index < result->quality_count; ++index) {
        status = apta_builder_validate_quality(&result->quality[index]);
        if (status < 0)
            return apta_stream_validation_status(status);
    }
    return APTA_STATUS_OK;
}

static int apta_stream_state_allowed(uint8_t state, uint32_t container_flags);
static int apta_stream_confidence_valid(uint8_t confidence);

static void apta_stream_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void apta_stream_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static void apta_stream_put_u64(uint8_t *p, uint64_t value)
{
    apta_stream_put_u32(p, (uint32_t)value);
    apta_stream_put_u32(p + 4u, (uint32_t)(value >> 32u));
}

static uint16_t apta_stream_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t apta_stream_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static uint64_t apta_stream_get_u64(const uint8_t *p)
{
    return (uint64_t)apta_stream_get_u32(p) |
           ((uint64_t)apta_stream_get_u32(p + 4u) << 32u);
}

static int64_t apta_stream_get_i64(const uint8_t *p)
{
    return (int64_t)apta_stream_get_u64(p);
}

static int16_t apta_stream_get_i16(const uint8_t *p)
{
    return (int16_t)apta_stream_get_u16(p);
}

static apta_status_t apta_stream_align8(uint64_t value, uint64_t *out)
{
    if (out == NULL || value > UINT64_MAX - 7u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    *out = (value + 7u) & ~UINT64_C(7);
    return APTA_STATUS_OK;
}

static int apta_stream_reserved_zero(const uint64_t *values, uint32_t count)
{
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        if (values[index] != 0u)
            return 0;
    }
    return 1;
}

static int apta_stream_utf8_valid(const uint8_t *bytes, uint32_t size)
{
    uint32_t index = 0u;
    if (size != 0u && bytes == NULL)
        return 0;
    while (index < size) {
        uint8_t first = bytes[index];
        uint32_t continuation_count;
        uint8_t second_min = UINT8_C(0x80);
        uint8_t second_max = UINT8_C(0xBF);
        uint32_t tail;
        if (first <= UINT8_C(0x7F)) {
            ++index;
            continue;
        }
        if (first >= UINT8_C(0xC2) && first <= UINT8_C(0xDF)) {
            continuation_count = 1u;
        } else if (first >= UINT8_C(0xE0) && first <= UINT8_C(0xEF)) {
            continuation_count = 2u;
            if (first == UINT8_C(0xE0))
                second_min = UINT8_C(0xA0);
            if (first == UINT8_C(0xED))
                second_max = UINT8_C(0x9F);
        } else if (first >= UINT8_C(0xF0) && first <= UINT8_C(0xF4)) {
            continuation_count = 3u;
            if (first == UINT8_C(0xF0))
                second_min = UINT8_C(0x90);
            if (first == UINT8_C(0xF4))
                second_max = UINT8_C(0x8F);
        } else {
            return 0;
        }
        if (continuation_count > size - index - 1u ||
            bytes[index + 1u] < second_min || bytes[index + 1u] > second_max) {
            return 0;
        }
        for (tail = 2u; tail <= continuation_count; ++tail) {
            if (bytes[index + tail] < UINT8_C(0x80) ||
                bytes[index + tail] > UINT8_C(0xBF)) {
                return 0;
            }
        }
        index += continuation_count + 1u;
    }
    return 1;
}

static int apta_output_stream_is_valid(const apta_output_stream_t *stream)
{
    return stream != NULL &&
           apta_internal_validate_struct(stream, sizeof(*stream),
                                         stream->struct_size,
                                         stream->api_version) &&
           stream->write != NULL && stream->seek != NULL &&
           stream->flush != NULL &&
           apta_stream_reserved_zero(stream->reserved64, 4u);
}

static int apta_input_stream_is_valid(const apta_input_stream_t *stream)
{
    return stream != NULL &&
           apta_internal_validate_struct(stream, sizeof(*stream),
                                         stream->struct_size,
                                         stream->api_version) &&
           stream->read_at != NULL && stream->get_size != NULL &&
           apta_stream_reserved_zero(stream->reserved64, 4u);
}

static apta_status_t apta_stream_write_exact(const apta_output_stream_t *stream,
                                             const uint8_t *data, uint64_t size)
{
    uint64_t offset = 0u;
    while (offset < size) {
        uint64_t written = 0u;
        apta_status_t status = stream->write(
            stream->user_data, data + (size_t)offset, size - offset, &written);
        if (status < 0)
            return status;
        if (status != APTA_STATUS_OK || written == 0u ||
            written > size - offset) {
            return APTA_ERROR_SOURCE;
        }
        offset += written;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_emit(apta_stream_emitter_t *emitter,
                                      const uint8_t *data, uint64_t size)
{
    uint64_t index;
    for (index = 0u; index < size; ++index) {
        uint32_t bit;
        emitter->crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(emitter->crc & 1u);
            emitter->crc = (emitter->crc >> 1u) ^ (UINT32_C(0x82F63B78) & mask);
        }
    }
    return emitter->write ? apta_stream_write_exact(emitter->stream, data, size)
                          : APTA_STATUS_OK;
}

static apta_status_t apta_stream_emit_wovr(apta_stream_emitter_t *emitter,
                                           const apta_result_t *result)
{
    const apta_waveform_overview_view_t *overview = &result->overview;
    uint8_t record[48];
    uint64_t logical_columns = 0u;
    uint64_t column_offset;
    uint32_t packed_offset = 0u;
    uint32_t span_index;
    apta_status_t status;

    for (span_index = 0u; span_index < overview->span_count; ++span_index) {
        uint64_t end =
            (uint64_t)overview->spans[span_index].first_column_index +
            overview->spans[span_index].column_count;
        if (end > logical_columns)
            logical_columns = end;
    }
    if (result->total_source_frames != APTA_TOTAL_FRAMES_UNKNOWN) {
        uint64_t relative =
            result->total_source_frames - overview->level.origin_frame;
        logical_columns =
            relative / overview->level.frames_per_column +
            ((relative % overview->level.frames_per_column) != 0u);
    }
    column_offset = 48u + (uint64_t)overview->span_count * 32u;
    memset(record, 0, sizeof(record));
    apta_stream_put_u32(record, overview->level.level_id);
    apta_stream_put_u32(record + 4u, overview->level.frames_per_column);
    apta_stream_put_u64(record + 8u, overview->level.origin_frame);
    apta_stream_put_u32(record + 16u, (uint32_t)logical_columns);
    apta_stream_put_u32(record + 20u, overview->span_count);
    apta_stream_put_u64(record + 24u, 48u);
    apta_stream_put_u64(record + 32u, column_offset);
    apta_stream_put_u32(record + 40u, overview->state & 0x7u);
    status = apta_stream_emit(emitter, record, sizeof(record));
    if (status < 0)
        return status;

    for (span_index = 0u; span_index < overview->span_count; ++span_index) {
        const apta_waveform_span_t *span = &overview->spans[span_index];
        memset(record, 0, 32u);
        apta_stream_put_u64(record, span->source_range.first_frame);
        apta_stream_put_u64(record + 8u, span->source_range.end_frame);
        apta_stream_put_u32(record + 16u, span->first_column_index);
        apta_stream_put_u32(record + 20u, span->column_count);
        apta_stream_put_u32(record + 24u, packed_offset);
        status = apta_stream_emit(emitter, record, 32u);
        if (status < 0)
            return status;
        packed_offset += span->column_count;
    }
    for (span_index = 0u; span_index < overview->span_count; ++span_index) {
        const apta_waveform_span_t *span = &overview->spans[span_index];
        uint32_t column_index;
        for (column_index = 0u; column_index < span->column_count;
             ++column_index) {
            const apta_waveform_column_t *column = &span->columns[column_index];
            memset(record, 0, 10u);
            apta_stream_put_u16(record, (uint16_t)column->minimum);
            apta_stream_put_u16(record + 2u, (uint16_t)column->maximum);
            apta_stream_put_u16(record + 4u, column->rms);
            record[6] = column->low;
            record[7] = column->mid;
            record[8] = column->high;
            record[9] = column->flags;
            status = apta_stream_emit(emitter, record, 10u);
            if (status < 0)
                return status;
        }
    }
    return APTA_STATUS_OK;
}

static apta_status_t
apta_stream_emit_column(apta_stream_emitter_t *emitter,
                        const apta_waveform_column_t *column)
{
    uint8_t record[10];
    memset(record, 0, sizeof(record));
    apta_stream_put_u16(record, (uint16_t)column->minimum);
    apta_stream_put_u16(record + 2u, (uint16_t)column->maximum);
    apta_stream_put_u16(record + 4u, column->rms);
    record[6] = column->low;
    record[7] = column->mid;
    record[8] = column->high;
    record[9] = column->flags;
    return apta_stream_emit(emitter, record, sizeof(record));
}

static apta_status_t apta_stream_emit_wdtl(apta_stream_emitter_t *emitter,
                                           const apta_result_t *result)
{
    uint8_t record[48];
    uint64_t columns_offset = 16u + (uint64_t)result->detail_tile_count * 48u;
    uint32_t packed_offset = 0u;
    uint32_t tile_index;
    apta_status_t status;
    memset(record, 0, 16u);
    apta_stream_put_u32(record, result->detail_tile_count);
    apta_stream_put_u64(record + 8u, 16u);
    status = apta_stream_emit(emitter, record, 16u);
    if (status < 0)
        return status;
    for (tile_index = 0u; tile_index < result->detail_tile_count;
         ++tile_index) {
        const apta_waveform_tile_view_t *tile =
            &result->detail_tiles[tile_index];
        memset(record, 0, sizeof(record));
        apta_stream_put_u32(record, tile->level_id);
        apta_stream_put_u32(record + 4u, tile->tile_index);
        apta_stream_put_u64(record + 8u, tile->source_range.first_frame);
        apta_stream_put_u64(record + 16u, tile->source_range.end_frame);
        apta_stream_put_u32(record + 24u, tile->first_column_index);
        apta_stream_put_u32(record + 28u, tile->column_count);
        apta_stream_put_u64(record + 32u,
                            columns_offset + (uint64_t)packed_offset * 10u);
        apta_stream_put_u32(record + 40u, tile->state);
        record[46] = tile->confidence;
        status = apta_stream_emit(emitter, record, sizeof(record));
        if (status < 0)
            return status;
        packed_offset += tile->column_count;
    }
    for (tile_index = 0u; tile_index < result->detail_tile_count;
         ++tile_index) {
        const apta_waveform_tile_view_t *tile =
            &result->detail_tiles[tile_index];
        uint32_t column_index;
        for (column_index = 0u; column_index < tile->column_count;
             ++column_index) {
            status =
                apta_stream_emit_column(emitter, &tile->columns[column_index]);
            if (status < 0)
                return status;
        }
    }
    return APTA_STATUS_OK;
}

static uint64_t apta_stream_cbor_head_size(uint64_t value)
{
    return value < 24u           ? 1u
           : value <= UINT8_MAX  ? 2u
           : value <= UINT16_MAX ? 3u
           : value <= UINT32_MAX ? 5u
                                 : 9u;
}

static apta_status_t apta_stream_emit_cbor_head(apta_stream_emitter_t *emitter,
                                                uint8_t major, uint64_t value)
{
    uint8_t bytes[9];
    uint32_t size;
    if (value < 24u) {
        bytes[0] = (uint8_t)((major << 5u) | value);
        size = 1u;
    } else if (value <= UINT8_MAX) {
        bytes[0] = (uint8_t)((major << 5u) | 24u);
        bytes[1] = (uint8_t)value;
        size = 2u;
    } else if (value <= UINT16_MAX) {
        bytes[0] = (uint8_t)((major << 5u) | 25u);
        bytes[1] = (uint8_t)(value >> 8u);
        bytes[2] = (uint8_t)value;
        size = 3u;
    } else if (value <= UINT32_MAX) {
        bytes[0] = (uint8_t)((major << 5u) | 26u);
        bytes[1] = (uint8_t)(value >> 24u);
        bytes[2] = (uint8_t)(value >> 16u);
        bytes[3] = (uint8_t)(value >> 8u);
        bytes[4] = (uint8_t)value;
        size = 5u;
    } else {
        uint32_t index;
        bytes[0] = (uint8_t)((major << 5u) | 27u);
        for (index = 0u; index < 8u; ++index)
            bytes[index + 1u] = (uint8_t)(value >> (56u - index * 8u));
        size = 9u;
    }
    return apta_stream_emit(emitter, bytes, size);
}

static int apta_stream_meta_flag(const apta_metadata_view_t *metadata,
                                 uint32_t flag)
{
    return (metadata->flags & flag) != 0u;
}

static uint32_t apta_stream_meta_count(const apta_metadata_view_t *metadata)
{
    uint32_t count = 0u;
    count += apta_stream_meta_flag(metadata,
                                   APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT);
    count += apta_stream_meta_flag(metadata,
                                   APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT);
    count += apta_stream_meta_flag(metadata,
                                   APTA_METADATA_FLAG_BACKEND_NAME_PRESENT);
    count += apta_stream_meta_flag(metadata,
                                   APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT);
    count += apta_stream_meta_flag(metadata,
                                   APTA_METADATA_FLAG_CREATION_TIME_PRESENT);
    count +=
        metadata->application_source_id_kind != APTA_METADATA_SOURCE_ID_NONE;
    count +=
        apta_stream_meta_flag(metadata, APTA_METADATA_FLAG_COMMENTS_PRESENT);
    return count;
}

static uint64_t apta_stream_meta_size(const apta_metadata_view_t *metadata)
{
    uint64_t size =
        apta_stream_cbor_head_size(apta_stream_meta_count(metadata));
#define APTA_META_TEXT_SIZE(flag_, field_)                                     \
    do {                                                                       \
        if (apta_stream_meta_flag(metadata, (flag_)))                          \
            size += 1u + apta_stream_cbor_head_size(metadata->field_.size) +   \
                    metadata->field_.size;                                     \
    } while (0)
    APTA_META_TEXT_SIZE(APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT,
                        producer_name);
    APTA_META_TEXT_SIZE(APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT,
                        producer_version_string);
    APTA_META_TEXT_SIZE(APTA_METADATA_FLAG_BACKEND_NAME_PRESENT, backend_name);
    APTA_META_TEXT_SIZE(APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT,
                        backend_version);
    if (apta_stream_meta_flag(metadata,
                              APTA_METADATA_FLAG_CREATION_TIME_PRESENT))
        size += 1u + apta_stream_cbor_head_size(metadata->creation_unix_time);
    if (metadata->application_source_id_kind != APTA_METADATA_SOURCE_ID_NONE)
        size +=
            1u +
            apta_stream_cbor_head_size(metadata->application_source_id.size) +
            metadata->application_source_id.size;
    APTA_META_TEXT_SIZE(APTA_METADATA_FLAG_COMMENTS_PRESENT, comments);
#undef APTA_META_TEXT_SIZE
    return size;
}

static apta_status_t apta_stream_emit_meta_text(apta_stream_emitter_t *emitter,
                                                uint8_t key,
                                                const apta_utf8_view_t *text)
{
    apta_status_t status = apta_stream_emit(emitter, &key, 1u);
    if (status < 0)
        return status;
    status = apta_stream_emit_cbor_head(emitter, 3u, text->size);
    return status < 0 ? status
                      : apta_stream_emit(emitter, (const uint8_t *)text->data,
                                         text->size);
}

static apta_status_t apta_stream_emit_meta(apta_stream_emitter_t *emitter,
                                           const apta_metadata_view_t *metadata)
{
    apta_status_t status = apta_stream_emit_cbor_head(
        emitter, 5u, apta_stream_meta_count(metadata));
#define APTA_EMIT_META_TEXT(flag_, key_, field_)                               \
    do {                                                                       \
        if (status >= 0 && apta_stream_meta_flag(metadata, (flag_)))           \
            status = apta_stream_emit_meta_text(emitter, (key_),               \
                                                &metadata->field_);            \
    } while (0)
    APTA_EMIT_META_TEXT(APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT, 1u,
                        producer_name);
    APTA_EMIT_META_TEXT(APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT, 2u,
                        producer_version_string);
    APTA_EMIT_META_TEXT(APTA_METADATA_FLAG_BACKEND_NAME_PRESENT, 3u,
                        backend_name);
    APTA_EMIT_META_TEXT(APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT, 4u,
                        backend_version);
    if (status >= 0 &&
        apta_stream_meta_flag(metadata,
                              APTA_METADATA_FLAG_CREATION_TIME_PRESENT)) {
        const uint8_t key = 5u;
        status = apta_stream_emit(emitter, &key, 1u);
        if (status >= 0)
            status = apta_stream_emit_cbor_head(emitter, 0u,
                                                metadata->creation_unix_time);
    }
    if (status >= 0 &&
        metadata->application_source_id_kind != APTA_METADATA_SOURCE_ID_NONE) {
        const uint8_t key = 6u;
        status = apta_stream_emit(emitter, &key, 1u);
        if (status >= 0)
            status = apta_stream_emit_cbor_head(
                emitter,
                metadata->application_source_id_kind ==
                        APTA_METADATA_SOURCE_ID_TEXT
                    ? 3u
                    : 2u,
                metadata->application_source_id.size);
        if (status >= 0)
            status =
                apta_stream_emit(emitter, metadata->application_source_id.data,
                                 metadata->application_source_id.size);
    }
    APTA_EMIT_META_TEXT(APTA_METADATA_FLAG_COMMENTS_PRESENT, 7u, comments);
#undef APTA_EMIT_META_TEXT
    return status;
}

static apta_status_t apta_stream_emit_temp(apta_stream_emitter_t *emitter,
                                           const apta_tempo_view_t *tempo)
{
    uint8_t record[56];
    uint32_t index;
    apta_status_t status;
    memset(record, 0, sizeof(record));
    apta_stream_put_u16(record, 1u);
    record[2] = (uint8_t)tempo->selected.state;
    record[3] = tempo->selected.confidence;
    apta_stream_put_u32(record + 4u, tempo->selected.flags);
    apta_stream_put_u32(record + 8u, tempo->selected.tempo_millibpm);
    apta_stream_put_u32(record + 12u, tempo->selected.candidate_set_id);
    apta_stream_put_u64(record + 16u,
                        tempo->selected.evidence_range.first_frame);
    apta_stream_put_u64(record + 24u, tempo->selected.evidence_range.end_frame);
    apta_stream_put_u64(record + 32u,
                        tempo->selected.applicability_range.first_frame);
    apta_stream_put_u64(record + 40u,
                        tempo->selected.applicability_range.end_frame);
    apta_stream_put_u32(record + 48u, tempo->candidate_count);
    status = apta_stream_emit(emitter, record, sizeof(record));
    if (status < 0)
        return status;
    for (index = 0u; index < tempo->candidate_count; ++index) {
        const apta_tempo_candidate_t *candidate = &tempo->candidates[index];
        memset(record, 0, 16u);
        apta_stream_put_u32(record, candidate->tempo_millibpm);
        apta_stream_put_u16(record + 4u, candidate->score);
        record[6] = candidate->confidence;
        record[7] = (uint8_t)candidate->relation_to_selected;
        apta_stream_put_u32(record + 8u, candidate->flags);
        status = apta_stream_emit(emitter, record, 16u);
        if (status < 0)
            return status;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_emit_lgrd(apta_stream_emitter_t *emitter,
                                           const apta_grid_view_t *grid)
{
    const apta_grid_segment_t *segment = &grid->segments[0];
    uint8_t p[144];
    memset(p, 0, sizeof(p));
    apta_stream_put_u16(p, 1u);
    p[2] = (uint8_t)grid->state;
    p[3] = grid->confidence;
    apta_stream_put_u32(p + 4u, grid->flags);
    apta_stream_put_u32(p + 8u, grid->representation);
    apta_stream_put_u32(p + 12u, 1u);
    apta_stream_put_u64(p + 16u, grid->requested_range.first_frame);
    apta_stream_put_u64(p + 24u, grid->requested_range.end_frame);
    apta_stream_put_u64(p + 32u, grid->evidence_range.first_frame);
    apta_stream_put_u64(p + 40u, grid->evidence_range.end_frame);
    apta_stream_put_u64(p + 48u, grid->applicability_range.first_frame);
    apta_stream_put_u64(p + 56u, grid->applicability_range.end_frame);
    apta_stream_put_u64(p + 64u, grid->coverage_ranges[0].first_frame);
    apta_stream_put_u64(p + 72u, grid->coverage_ranges[0].end_frame);
    apta_stream_put_u64(p + 80u, segment->anchor_position.whole_frame);
    apta_stream_put_u32(p + 88u, segment->anchor_position.fraction_q32);
    apta_stream_put_u64(p + 96u, (uint64_t)segment->anchor_ordinal);
    apta_stream_put_u64(p + 104u, segment->frames_per_beat.whole_frames);
    apta_stream_put_u32(p + 112u, segment->frames_per_beat.fraction_q32);
    apta_stream_put_u32(p + 116u, segment->beat_count);
    apta_stream_put_u32(p + 120u, segment->nominal_tempo_millibpm);
    apta_stream_put_u32(p + 124u, segment->segment_id);
    apta_stream_put_u32(p + 128u, segment->revision);
    apta_stream_put_u32(p + 132u, segment->flags);
    p[136] = (uint8_t)segment->state;
    p[137] = segment->confidence;
    return apta_stream_emit(emitter, p, sizeof(p));
}

static apta_status_t apta_stream_emit_ggrd(apta_stream_emitter_t *emitter,
                                           const apta_grid_view_t *grid)
{
    uint8_t p[96];
    uint32_t index;
    apta_status_t status;
    memset(p, 0, sizeof(p));
    apta_stream_put_u16(p, 1u);
    p[2] = (uint8_t)grid->state;
    p[3] = grid->confidence;
    apta_stream_put_u32(p + 4u, grid->flags);
    apta_stream_put_u32(p + 8u, grid->representation);
    apta_stream_put_u32(p + 12u, 1u);
    apta_stream_put_u32(p + 16u, grid->segment_count);
    apta_stream_put_u32(p + 20u, grid->beat_count);
    apta_stream_put_u64(p + 24u, grid->requested_range.first_frame);
    apta_stream_put_u64(p + 32u, grid->requested_range.end_frame);
    apta_stream_put_u64(p + 40u, grid->evidence_range.first_frame);
    apta_stream_put_u64(p + 48u, grid->evidence_range.end_frame);
    apta_stream_put_u64(p + 56u, grid->applicability_range.first_frame);
    apta_stream_put_u64(p + 64u, grid->applicability_range.end_frame);
    apta_stream_put_u64(p + 72u, grid->coverage_ranges[0].first_frame);
    apta_stream_put_u64(p + 80u, grid->coverage_ranges[0].end_frame);
    status = apta_stream_emit(emitter, p, sizeof(p));
    if (status < 0)
        return status;
    for (index = 0u; index < grid->segment_count; ++index) {
        const apta_grid_segment_t *s = &grid->segments[index];
        memset(p, 0, 80u);
        apta_stream_put_u64(p, s->applicability_range.first_frame);
        apta_stream_put_u64(p + 8u, s->applicability_range.end_frame);
        apta_stream_put_u64(p + 16u, s->anchor_position.whole_frame);
        apta_stream_put_u32(p + 24u, s->anchor_position.fraction_q32);
        apta_stream_put_u64(p + 32u, (uint64_t)s->anchor_ordinal);
        apta_stream_put_u64(p + 40u, s->frames_per_beat.whole_frames);
        apta_stream_put_u32(p + 48u, s->frames_per_beat.fraction_q32);
        apta_stream_put_u32(p + 52u, s->beat_count);
        apta_stream_put_u32(p + 56u, s->nominal_tempo_millibpm);
        apta_stream_put_u32(p + 60u, s->segment_id);
        apta_stream_put_u32(p + 64u, s->revision);
        apta_stream_put_u32(p + 68u, s->flags);
        p[72] = (uint8_t)s->state;
        p[73] = s->confidence;
        status = apta_stream_emit(emitter, p, 80u);
        if (status < 0)
            return status;
    }
    for (index = 0u; index < grid->beat_count; ++index) {
        const apta_beat_t *b = &grid->beats[index];
        memset(p, 0, 40u);
        apta_stream_put_u64(p, b->position.whole_frame);
        apta_stream_put_u32(p + 8u, b->position.fraction_q32);
        apta_stream_put_u64(p + 16u, (uint64_t)b->ordinal);
        apta_stream_put_u32(p + 24u, b->revision);
        apta_stream_put_u32(p + 28u, b->flags);
        p[32] = b->confidence;
        status = apta_stream_emit(emitter, p, 40u);
        if (status < 0)
            return status;
    }
    return APTA_STATUS_OK;
}

static apta_status_t
apta_stream_emit_revn(apta_stream_emitter_t *emitter,
                      const apta_grid_revision_view_t *revision)
{
    uint8_t p[80];
    memset(p, 0, sizeof(p));
    apta_stream_put_u16(p, 1u);
    p[2] = (uint8_t)revision->state;
    p[3] = revision->confidence;
    apta_stream_put_u32(p + 4u, revision->flags);
    apta_stream_put_u32(p + 8u, revision->revision_id);
    apta_stream_put_u32(p + 12u, revision->previous_revision_id);
    apta_stream_put_u32(p + 16u, revision->proposed_representation);
    apta_stream_put_u32(p + 20u, revision->proposed_segment_count);
    apta_stream_put_u32(p + 24u, revision->proposed_beat_count);
    apta_stream_put_u64(p + 32u, revision->affected_range.first_frame);
    apta_stream_put_u64(p + 40u, revision->affected_range.end_frame);
    return apta_stream_emit(emitter, p, sizeof(p));
}

static apta_status_t apta_stream_emit_key(apta_stream_emitter_t *emitter,
                                          const apta_key_view_t *key)
{
    uint8_t record[40];
    uint32_t index;
    apta_status_t status;
    memset(record, 0, sizeof(record));
    apta_stream_put_u16(record, 1u);
    record[2] = (uint8_t)key->state;
    record[3] = key->confidence;
    record[4] = key->tonic;
    record[5] = (uint8_t)key->mode;
    apta_stream_put_u16(record + 6u, (uint16_t)key->tuning_offset_cents);
    apta_stream_put_u32(record + 8u, key->flags);
    apta_stream_put_u32(record + 12u, key->candidate_count);
    apta_stream_put_u64(record + 16u, key->applicability_range.first_frame);
    apta_stream_put_u64(record + 24u, key->applicability_range.end_frame);
    apta_stream_put_u32(record + 32u, 40u);
    status = apta_stream_emit(emitter, record, sizeof(record));
    if (status < 0)
        return status;
    for (index = 0u; index < key->candidate_count; ++index) {
        const apta_key_candidate_t *candidate = &key->candidates[index];
        memset(record, 0, 16u);
        record[0] = candidate->tonic;
        record[1] = (uint8_t)candidate->mode;
        apta_stream_put_u16(record + 2u,
                            (uint16_t)candidate->tuning_offset_cents);
        apta_stream_put_u16(record + 4u, candidate->score);
        record[6] = candidate->confidence;
        apta_stream_put_u32(record + 8u, candidate->flags);
        status = apta_stream_emit(emitter, record, 16u);
        if (status < 0)
            return status;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_emit_meter(apta_stream_emitter_t *emitter,
                                            const apta_meter_view_t *meter)
{
    uint8_t record[56];
    uint32_t index;
    apta_status_t status;
    memset(record, 0, 48u);
    apta_stream_put_u16(record, 1u);
    record[2] = (uint8_t)meter->state;
    record[3] = meter->confidence;
    apta_stream_put_u16(record + 4u, meter->numerator);
    apta_stream_put_u16(record + 6u, meter->denominator);
    apta_stream_put_u32(record + 8u, meter->flags);
    apta_stream_put_u32(record + 12u, meter->segment_count);
    apta_stream_put_u64(record + 16u, meter->downbeat_frame);
    apta_stream_put_u64(record + 24u, (uint64_t)meter->downbeat_ordinal);
    apta_stream_put_u32(record + 32u, 48u);
    status = apta_stream_emit(emitter, record, 48u);
    if (status < 0)
        return status;
    for (index = 0u; index < meter->segment_count; ++index) {
        const apta_meter_segment_t *segment = &meter->segments[index];
        memset(record, 0, sizeof(record));
        apta_stream_put_u64(record, segment->applicability_range.first_frame);
        apta_stream_put_u64(record + 8u,
                            segment->applicability_range.end_frame);
        apta_stream_put_u64(record + 16u, segment->downbeat_frame);
        apta_stream_put_u64(record + 24u, (uint64_t)segment->downbeat_ordinal);
        apta_stream_put_u16(record + 32u, segment->numerator);
        apta_stream_put_u16(record + 34u, segment->denominator);
        record[36] = (uint8_t)segment->state;
        record[37] = segment->confidence;
        apta_stream_put_u32(record + 40u, segment->flags);
        apta_stream_put_u32(record + 44u, segment->segment_id);
        status = apta_stream_emit(emitter, record, sizeof(record));
        if (status < 0)
            return status;
    }
    return APTA_STATUS_OK;
}

static const apta_quality_view_t *
apta_stream_quality_by_feature(const apta_result_t *result,
                               apta_feature_mask_t feature)
{
    uint32_t index;
    for (index = 0u; index < result->quality_count; ++index) {
        if (result->quality[index].feature == feature) {
            return &result->quality[index];
        }
    }
    return NULL;
}

static apta_status_t apta_stream_emit_quality(apta_stream_emitter_t *emitter,
                                              const apta_result_t *result)
{
    uint8_t record[32];
    apta_feature_mask_t feature;
    apta_status_t status;
    memset(record, 0, 16u);
    apta_stream_put_u16(record, 1u);
    apta_stream_put_u16(record + 2u, 32u);
    apta_stream_put_u32(record + 4u, result->quality_count);
    apta_stream_put_u32(record + 8u, 16u);
    status = apta_stream_emit(emitter, record, 16u);
    if (status < 0)
        return status;
    for (feature = 1u; feature <= APTA_FEATURE_METER_DOWNBEAT; feature <<= 1u) {
        const apta_quality_view_t *quality =
            apta_stream_quality_by_feature(result, feature);
        if (quality == NULL)
            continue;
        memset(record, 0, sizeof(record));
        apta_stream_put_u64(record, quality->feature);
        apta_stream_put_u32(record + 8u, quality->calibration_model_id);
        apta_stream_put_u16(record + 12u, quality->evidence_coverage_permille);
        record[14] = quality->confidence;
        record[15] = (uint8_t)quality->state;
        apta_stream_put_u32(record + 16u, quality->flags);
        status = apta_stream_emit(emitter, record, sizeof(record));
        if (status < 0)
            return status;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_emit_section(apta_stream_emitter_t *emitter,
                                              const apta_result_t *result,
                                              apta_stream_section_kind_t kind)
{
    switch (kind) {
    case APTA_STREAM_SECTION_WOVR:
        return apta_stream_emit_wovr(emitter, result);
    case APTA_STREAM_SECTION_WDTL:
        return apta_stream_emit_wdtl(emitter, result);
    case APTA_STREAM_SECTION_META:
        return apta_stream_emit_meta(emitter, &result->metadata.view);
    case APTA_STREAM_SECTION_TEMP:
        return apta_stream_emit_temp(emitter, &result->tempo);
    case APTA_STREAM_SECTION_LGRD:
        return apta_stream_emit_lgrd(emitter, &result->local_grid);
    case APTA_STREAM_SECTION_GGRD:
        return apta_stream_emit_ggrd(emitter, &result->s6->global_grid);
    case APTA_STREAM_SECTION_REVN:
        return apta_stream_emit_revn(emitter, &result->s6->revision);
    case APTA_STREAM_SECTION_MKEY:
        return apta_stream_emit_key(emitter, &result->key);
    case APTA_STREAM_SECTION_MTRD:
        return apta_stream_emit_meter(emitter, &result->meter);
    case APTA_STREAM_SECTION_CONF:
        return apta_stream_emit_quality(emitter, result);
    default:
        return APTA_ERROR_INTERNAL;
    }
}

static void apta_stream_add_section(apta_stream_output_section_t *section,
                                    const char id[4],
                                    apta_stream_section_kind_t kind,
                                    uint64_t size, uint16_t flags)
{
    memcpy(section->id, id, 4u);
    section->kind = kind;
    section->size = size;
    section->flags = flags;
}

static apta_status_t apta_stream_build_basic_layout(
    const apta_result_t *result, const apta_serialize_options_t *options,
    apta_stream_output_section_t sections[APTA_STREAM_MAX_SECTIONS],
    uint32_t *count_out, uint64_t *total_out, uint32_t *flags_out)
{
    uint64_t legacy_size = 0u;
    uint64_t packed_columns = 0u;
    uint64_t packed_detail_columns = 0u;
    uint64_t cursor;
    uint32_t count = 0u;
    uint32_t index;
    apta_status_t status =
        apta_result_query_serialized_size(result, options, &legacy_size);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE)
        return status;
    for (index = 0u; index < result->overview.span_count; ++index) {
        packed_columns += result->overview.spans[index].column_count;
    }
    apta_stream_add_section(&sections[count++], "WOVR",
                            APTA_STREAM_SECTION_WOVR,
                            48u + (uint64_t)result->overview.span_count * 32u +
                                packed_columns * 10u,
                            1u);
    if ((result->info.available_features & APTA_FEATURE_WAVEFORM_DETAIL) !=
            0u &&
        result->detail_tile_count != 0u) {
        for (index = 0u; index < result->detail_tile_count; ++index) {
            packed_detail_columns += result->detail_tiles[index].column_count;
        }
        apta_stream_add_section(
            &sections[count++], "WDTL", APTA_STREAM_SECTION_WDTL,
            16u + (uint64_t)result->detail_tile_count * 48u +
                packed_detail_columns * 10u,
            0u);
    }
    if (apta_internal_metadata_is_present(&result->metadata)) {
        apta_stream_add_section(
            &sections[count++], "META", APTA_STREAM_SECTION_META,
            apta_stream_meta_size(&result->metadata.view), 0u);
    }
    if ((result->info.available_features & APTA_FEATURE_BPM) != 0u) {
        apta_stream_add_section(
            &sections[count++], "TEMP", APTA_STREAM_SECTION_TEMP,
            56u + (uint64_t)result->tempo.candidate_count * 16u, 0u);
    }
    if ((result->info.available_features & APTA_FEATURE_LOCAL_BEATGRID) != 0u) {
        apta_stream_add_section(&sections[count++], "LGRD",
                                APTA_STREAM_SECTION_LGRD, 144u, 0u);
    }
    if ((result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) !=
        0u) {
        const apta_grid_view_t *grid = &result->s6->global_grid;
        apta_stream_add_section(&sections[count++], "GGRD",
                                APTA_STREAM_SECTION_GGRD,
                                96u + (uint64_t)grid->segment_count * 80u +
                                    (uint64_t)grid->beat_count * 40u,
                                0u);
        apta_stream_add_section(&sections[count++], "REVN",
                                APTA_STREAM_SECTION_REVN, 80u, 0u);
    }
    if ((result->info.available_features & APTA_FEATURE_MUSICAL_KEY) != 0u) {
        apta_stream_add_section(
            &sections[count++], "MKEY", APTA_STREAM_SECTION_MKEY,
            40u + (uint64_t)result->key.candidate_count * 16u, 0u);
    }
    if ((result->info.available_features & APTA_FEATURE_METER_DOWNBEAT) != 0u) {
        apta_stream_add_section(
            &sections[count++], "MTRD", APTA_STREAM_SECTION_MTRD,
            48u + (uint64_t)result->meter.segment_count * 56u, 0u);
    }
    if ((result->info.available_features & APTA_FEATURE_CALIBRATED_QUALITY) !=
        0u) {
        apta_stream_add_section(
            &sections[count++], "CONF", APTA_STREAM_SECTION_CONF,
            16u + (uint64_t)result->quality_count * 32u, 0u);
    }
    status = apta_stream_align8(APTA_STREAM_HEADER_SIZE +
                                    (uint64_t)count * APTA_STREAM_ENTRY_SIZE,
                                &cursor);
    if (status < 0)
        return status;
    for (index = 0u; index < count; ++index) {
        sections[index].offset = cursor;
        if (cursor > UINT64_MAX - sections[index].size) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        cursor += sections[index].size;
        if (index + 1u < count) {
            status = apta_stream_align8(cursor, &cursor);
            if (status < 0)
                return status;
        }
    }
    if (cursor != legacy_size)
        return APTA_ERROR_INTERNAL;
    *flags_out = 0u;
    if (result->overview.state != APTA_FEATURE_FINAL ||
        result->info.session_state != APTA_SESSION_COMPLETED) {
        *flags_out |= APTA_STREAM_FLAG_PARTIAL;
    }
    if (result->total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN) {
        *flags_out |=
            APTA_STREAM_FLAG_PARTIAL | APTA_STREAM_FLAG_DURATION_UNKNOWN;
    }
    if (((result->info.available_features & APTA_FEATURE_MUSICAL_KEY) != 0u &&
         result->key.state != APTA_FEATURE_FINAL) ||
        ((result->info.available_features & APTA_FEATURE_METER_DOWNBEAT) !=
             0u &&
         result->meter.state != APTA_FEATURE_FINAL)) {
        *flags_out |= APTA_STREAM_FLAG_PARTIAL;
    }
    for (index = 0u; index < result->meter.segment_count; ++index) {
        if (result->meter.segments[index].state != APTA_FEATURE_FINAL) {
            *flags_out |= APTA_STREAM_FLAG_PARTIAL;
        }
    }
    for (index = 0u; index < result->quality_count; ++index) {
        if (result->quality[index].state != APTA_FEATURE_FINAL) {
            *flags_out |= APTA_STREAM_FLAG_PARTIAL;
        }
    }
    *count_out = count;
    *total_out = cursor;
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_read_exact(const apta_input_stream_t *stream,
                                            uint64_t absolute_offset,
                                            uint8_t *data, uint64_t size,
                                            uint64_t maximum_request)
{
    uint64_t offset = 0u;
    if (maximum_request == 0u)
        return APTA_ERROR_INVALID_ARGUMENT;
    while (offset < size) {
        uint64_t requested = size - offset;
        uint64_t read = 0u;
        apta_status_t status;
        if (requested > maximum_request)
            requested = maximum_request;
        if (absolute_offset > UINT64_MAX - offset) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        status = stream->read_at(stream->user_data, absolute_offset + offset,
                                 data + (size_t)offset, requested, &read);
        if (status < 0)
            return status;
        if (status != APTA_STATUS_OK || read == 0u || read > requested) {
            return status == APTA_STATUS_END_OF_INPUT ? APTA_ERROR_CORRUPT_DATA
                                                      : APTA_ERROR_SOURCE;
        }
        offset += read;
    }
    return APTA_STATUS_OK;
}

static int apta_stream_bytes_zero(const uint8_t *bytes, uint64_t size)
{
    uint64_t index;
    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u)
            return 0;
    }
    return 1;
}

static int apta_stream_ranges_overlap(uint64_t first_offset,
                                      uint64_t first_size,
                                      uint64_t second_offset,
                                      uint64_t second_size)
{
    if (first_size == 0u || second_size == 0u)
        return 0;
    return first_offset < second_offset + second_size &&
           second_offset < first_offset + first_size;
}

static int apta_stream_known_index(const uint8_t id[4])
{
    static const char ids[APTA_STREAM_MAX_SECTIONS][4] = {
        {'M', 'E', 'T', 'A'}, {'W', 'O', 'V', 'R'}, {'W', 'D', 'T', 'L'},
        {'T', 'E', 'M', 'P'}, {'L', 'G', 'R', 'D'}, {'G', 'G', 'R', 'D'},
        {'R', 'E', 'V', 'N'}, {'M', 'K', 'E', 'Y'}, {'M', 'T', 'R', 'D'},
        {'C', 'O', 'N', 'F'}};
    uint32_t index;
    for (index = 0u; index < APTA_STREAM_MAX_SECTIONS; ++index) {
        if (memcmp(id, ids[index], 4u) == 0)
            return (int)index;
    }
    return -1;
}

static apta_status_t
apta_stream_resolve_options(apta_context_t *context,
                            const apta_stream_parse_options_t *options,
                            apta_stream_effective_options_t *effective)
{
    memset(effective, 0, sizeof(*effective));
    effective->flags = APTA_PARSE_STRICT;
    effective->maximum_section_count = 64u;
    effective->maximum_overview_spans = 65536u;
    effective->maximum_waveform_columns = 16777216u;
    effective->requested_features = APTA_FEATURE_ALL_KNOWN;
    effective->maximum_input_bytes = UINT64_C(268435456);
    effective->maximum_section_bytes = UINT64_C(268435456);
    effective->maximum_allocation_bytes = UINT64_C(268435456);
    effective->scratch_size = UINT64_C(65536);
    if (options != NULL) {
        if (!apta_internal_validate_struct(options, sizeof(*options),
                                           options->struct_size,
                                           options->api_version) ||
            (options->flags & ~APTA_PARSE_STRICT) != 0u ||
            (options->requested_features & ~APTA_FEATURE_ALL_KNOWN) != 0u ||
            !apta_stream_reserved_zero(options->reserved64, 3u)) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        effective->flags = options->flags;
        if (options->maximum_section_count != 0u) {
            effective->maximum_section_count = options->maximum_section_count;
        }
        if (options->maximum_overview_spans != 0u) {
            effective->maximum_overview_spans = options->maximum_overview_spans;
        }
        if (options->maximum_waveform_columns != 0u) {
            effective->maximum_waveform_columns =
                options->maximum_waveform_columns;
        }
        effective->requested_features = options->requested_features;
        if (options->maximum_input_bytes != 0u) {
            effective->maximum_input_bytes = options->maximum_input_bytes;
        }
        if (options->maximum_section_bytes != 0u) {
            effective->maximum_section_bytes = options->maximum_section_bytes;
        }
        if (options->maximum_allocation_bytes != 0u) {
            effective->maximum_allocation_bytes =
                options->maximum_allocation_bytes;
        }
        if (options->maximum_scratch_bytes != 0u) {
            effective->scratch_size = options->maximum_scratch_bytes;
        }
        if (options->scratch_buffer != NULL) {
            if (options->scratch_buffer_size == 0u ||
                options->scratch_buffer_size < effective->scratch_size) {
                return APTA_ERROR_INVALID_ARGUMENT;
            }
            effective->scratch = (uint8_t *)options->scratch_buffer;
        } else if (options->scratch_buffer_size != 0u) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
    }
    if (effective->scratch_size == 0u || effective->scratch_size > SIZE_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (effective->scratch == NULL) {
        effective->scratch = (uint8_t *)apta_internal_context_allocate(
            context, (size_t)effective->scratch_size, 1u,
            APTA_MEMORY_TEMPORARY);
        if (effective->scratch == NULL)
            return APTA_ERROR_OUT_OF_MEMORY;
        effective->owns_scratch = 1;
    }
    return APTA_STATUS_OK;
}

static void
apta_stream_release_options(apta_context_t *context,
                            apta_stream_effective_options_t *effective)
{
    if (effective->owns_scratch) {
        apta_internal_context_deallocate(context, effective->scratch);
    }
    effective->scratch = NULL;
}

static apta_status_t
apta_stream_read_entry(const apta_input_stream_t *stream,
                       const apta_stream_effective_options_t *options,
                       const apta_stream_input_layout_t *layout, uint32_t index,
                       uint8_t entry[APTA_STREAM_ENTRY_SIZE])
{
    uint64_t relative = (uint64_t)index * APTA_STREAM_ENTRY_SIZE;
    if (layout->directory_offset > UINT64_MAX - relative) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    return apta_stream_read_exact(stream, layout->directory_offset + relative,
                                  entry, APTA_STREAM_ENTRY_SIZE,
                                  options->scratch_size);
}

static apta_status_t
apta_stream_crc_section(const apta_input_stream_t *stream,
                        const apta_stream_effective_options_t *options,
                        uint64_t offset, uint64_t size, uint32_t expected)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint64_t done = 0u;
    while (done < size) {
        uint64_t amount = size - done;
        uint64_t index;
        apta_status_t status;
        if (amount > options->scratch_size)
            amount = options->scratch_size;
        status = apta_stream_read_exact(stream, offset + done, options->scratch,
                                        amount, options->scratch_size);
        if (status < 0)
            return status;
        for (index = 0u; index < amount; ++index) {
            uint32_t bit;
            crc ^= options->scratch[index];
            for (bit = 0u; bit < 8u; ++bit) {
                uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
                crc = (crc >> 1u) ^ (UINT32_C(0x82F63B78) & mask);
            }
        }
        done += amount;
    }
    return (crc ^ UINT32_C(0xFFFFFFFF)) == expected ? APTA_STATUS_OK
                                                    : APTA_ERROR_CORRUPT_DATA;
}

static apta_status_t
apta_stream_validate_container(const apta_input_stream_t *stream,
                               const apta_stream_effective_options_t *options,
                               apta_stream_input_layout_t *layout)
{
    uint64_t directory_size;
    uint32_t index;
    apta_status_t status =
        stream->get_size(stream->user_data, &layout->file_size);
    if (status != APTA_STATUS_OK) {
        return status < 0 ? status : APTA_ERROR_SOURCE;
    }
    if (layout->file_size < APTA_STREAM_HEADER_SIZE) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (layout->file_size > options->maximum_input_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    status =
        apta_stream_read_exact(stream, 0u, layout->header,
                               APTA_STREAM_HEADER_SIZE, options->scratch_size);
    if (status < 0)
        return status;
    if (memcmp(layout->header, "APTA", 4u) != 0 ||
        apta_stream_get_u16(layout->header + 4u) != APTA_STREAM_HEADER_SIZE ||
        apta_stream_get_u16(layout->header + 6u) != 1u ||
        apta_stream_get_u16(layout->header + 8u) != APTA_SPEC_VERSION_MAJOR ||
        apta_stream_get_u16(layout->header + 10u) > APTA_SPEC_VERSION_MINOR) {
        return APTA_ERROR_UNSUPPORTED;
    }
    layout->container_flags = apta_stream_get_u32(layout->header + 16u);
    layout->section_count = apta_stream_get_u32(layout->header + 20u);
    layout->directory_offset = apta_stream_get_u64(layout->header + 24u);
    if ((layout->container_flags & ~UINT32_C(7)) != 0u ||
        layout->section_count == 0u ||
        apta_stream_get_u64(layout->header + 32u) != layout->file_size ||
        apta_internal_crc32c(layout->header, 92u) !=
            apta_stream_get_u32(layout->header + 92u) ||
        apta_stream_get_u32(layout->header + 48u) == 0u ||
        apta_stream_get_u16(layout->header + 52u) == 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (layout->section_count > options->maximum_section_count) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    directory_size = (uint64_t)layout->section_count * APTA_STREAM_ENTRY_SIZE;
    if (layout->directory_offset < APTA_STREAM_HEADER_SIZE ||
        (layout->directory_offset & 7u) != 0u ||
        layout->directory_offset > layout->file_size ||
        directory_size > layout->file_size - layout->directory_offset) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    memset(layout->known, 0, sizeof(layout->known));
    for (index = 0u; index < layout->section_count; ++index) {
        uint8_t entry[APTA_STREAM_ENTRY_SIZE];
        uint64_t offset;
        uint64_t size;
        uint16_t flags;
        int known;
        uint32_t prior_index;
        status = apta_stream_read_entry(stream, options, layout, index, entry);
        if (status < 0)
            return status;
        known = apta_stream_known_index(entry);
        flags = apta_stream_get_u16(entry + 6u);
        offset = apta_stream_get_u64(entry + 8u);
        size = apta_stream_get_u64(entry + 16u);
        if ((flags & UINT16_C(6)) != 0u)
            return APTA_ERROR_UNSUPPORTED;
        if ((flags & ~UINT16_C(7)) != 0u ||
            (((options->flags & APTA_PARSE_STRICT) != 0u) &&
             apta_stream_get_u32(entry + 36u) != 0u) ||
            (offset & 7u) != 0u || size != apta_stream_get_u64(entry + 24u) ||
            size > options->maximum_section_bytes ||
            offset > layout->file_size || size > layout->file_size - offset ||
            apta_stream_ranges_overlap(offset, size, 0u,
                                       APTA_STREAM_HEADER_SIZE) ||
            apta_stream_ranges_overlap(offset, size, layout->directory_offset,
                                       directory_size)) {
            return size > options->maximum_section_bytes
                       ? APTA_ERROR_LIMIT_EXCEEDED
                       : APTA_ERROR_CORRUPT_DATA;
        }
        for (prior_index = 0u; prior_index < index; ++prior_index) {
            uint8_t prior[APTA_STREAM_ENTRY_SIZE];
            uint64_t prior_offset;
            uint64_t prior_size;
            status = apta_stream_read_entry(stream, options, layout,
                                            prior_index, prior);
            if (status < 0)
                return status;
            prior_offset = apta_stream_get_u64(prior + 8u);
            prior_size = apta_stream_get_u64(prior + 16u);
            if (apta_stream_ranges_overlap(offset, size, prior_offset,
                                           prior_size)) {
                return APTA_ERROR_CORRUPT_DATA;
            }
        }
        if (known >= 0) {
            apta_stream_input_section_t *section = &layout->known[known];
            if (section->present)
                return APTA_ERROR_CORRUPT_DATA;
            if (apta_stream_get_u16(entry + 4u) != 1u) {
                return APTA_ERROR_UNSUPPORTED;
            }
            if ((known == 1 && (flags & 1u) == 0u) ||
                (known != 1 && (flags & 1u) != 0u)) {
                return APTA_ERROR_CORRUPT_DATA;
            }
            memcpy(section->id, entry, 4u);
            section->offset = offset;
            section->size = size;
            section->crc = apta_stream_get_u32(entry + 32u);
            section->flags = flags;
            section->version = 1u;
            section->present = 1;
        } else if ((flags & 1u) != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        status = apta_stream_crc_section(stream, options, offset, size,
                                         apta_stream_get_u32(entry + 32u));
        if (status < 0)
            return status;
    }
    if (!layout->known[1].present)
        return APTA_ERROR_CORRUPT_DATA;
    return APTA_STATUS_OK;
}

static apta_status_t
apta_stream_read_section(const apta_input_stream_t *stream,
                         const apta_stream_effective_options_t *options,
                         const apta_stream_input_section_t *section,
                         uint64_t relative_offset, uint8_t *data, uint64_t size)
{
    if (!section->present || relative_offset > section->size ||
        size > section->size - relative_offset ||
        section->offset > UINT64_MAX - relative_offset) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    return apta_stream_read_exact(stream, section->offset + relative_offset,
                                  data, size, options->scratch_size);
}

static apta_status_t apta_stream_create_result(
    apta_context_t *context, const apta_stream_effective_options_t *options,
    const apta_stream_input_layout_t *layout, apta_result_t **result_out)
{
    apta_result_t *result;
    if (sizeof(*result) > options->maximum_allocation_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    result = (apta_result_t *)apta_internal_context_allocate(
        context, sizeof(*result), alignof(apta_result_t),
        APTA_MEMORY_PERSISTENT);
    if (result == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    memset(result, 0, sizeof(*result));
    result->context = context;
    atomic_init(&result->reference_count, 1u);
    apta_internal_result_init_absent_views(result);
    apta_result_info_init(&result->info);
    result->info.specification_major = apta_stream_get_u16(layout->header + 8u);
    result->info.specification_minor =
        apta_stream_get_u16(layout->header + 10u);
    result->info.producer_api_version =
        apta_stream_get_u32(layout->header + 12u);
    result->info.container_version = 1u;
    result->info.generation = 1u;
    result->info.session_state =
        (layout->container_flags & APTA_STREAM_FLAG_PARTIAL) != 0u
            ? APTA_SESSION_ACTIVE
            : APTA_SESSION_COMPLETED;
    apta_source_info_init(&result->source_info);
    result->source_info.total_frames =
        apta_stream_get_u64(layout->header + 40u);
    result->source_info.sample_rate = apta_stream_get_u32(layout->header + 48u);
    result->source_info.channel_count =
        apta_stream_get_u16(layout->header + 52u);
    result->source_info.channel_layout =
        apta_stream_get_u16(layout->header + 54u);
    memcpy(result->source_info.fingerprint, layout->header + 56u,
           APTA_SOURCE_FINGERPRINT_SIZE);
    result->source_info.fingerprint_kind =
        apta_stream_get_u32(layout->header + 88u);
    result->total_source_frames = result->source_info.total_frames;
    result->source_sample_rate = result->source_info.sample_rate;
    result->source_channel_count = result->source_info.channel_count;
    result->source_channel_layout = result->source_info.channel_layout;
    (void)atomic_fetch_add_explicit(&context->result_count, 1u,
                                    memory_order_acq_rel);
    *result_out = result;
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_parse_wovr(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_section_t *section, apta_result_t *result)
{
    uint8_t header[48];
    uint64_t span_offset;
    uint64_t column_offset;
    uint64_t expected_size;
    uint64_t packed_count = 0u;
    uint32_t span_count;
    uint32_t logical_count;
    uint32_t index;
    apta_status_t status = apta_stream_read_section(stream, options, section,
                                                    0u, header, sizeof(header));
    if (status < 0)
        return status;
    span_count = apta_stream_get_u32(header + 20u);
    logical_count = apta_stream_get_u32(header + 16u);
    span_offset = apta_stream_get_u64(header + 24u);
    column_offset = apta_stream_get_u64(header + 32u);
    if (apta_stream_get_u32(header + 4u) == 0u || logical_count == 0u ||
        span_count == 0u || span_count > options->maximum_overview_spans ||
        logical_count > options->maximum_waveform_columns ||
        span_offset != 48u ||
        column_offset != 48u + (uint64_t)span_count * 32u ||
        (apta_stream_get_u32(header + 40u) & ~UINT32_C(7)) != 0u ||
        !apta_stream_bytes_zero(header + 44u, 4u)) {
        return (span_count > options->maximum_overview_spans ||
                logical_count > options->maximum_waveform_columns)
                   ? APTA_ERROR_LIMIT_EXCEEDED
                   : APTA_ERROR_CORRUPT_DATA;
    }
    for (index = 0u; index < span_count; ++index) {
        uint8_t record[32];
        uint32_t columns;
        status = apta_stream_read_section(stream, options, section,
                                          span_offset + (uint64_t)index * 32u,
                                          record, sizeof(record));
        if (status < 0)
            return status;
        columns = apta_stream_get_u32(record + 20u);
        if (apta_stream_get_u64(record) >= apta_stream_get_u64(record + 8u) ||
            columns == 0u || !apta_stream_bytes_zero(record + 28u, 4u) ||
            packed_count > UINT32_MAX - columns) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        packed_count += columns;
    }
    expected_size = column_offset + packed_count * 10u;
    if (expected_size != section->size ||
        packed_count > options->maximum_waveform_columns) {
        return packed_count > options->maximum_waveform_columns
                   ? APTA_ERROR_LIMIT_EXCEEDED
                   : APTA_ERROR_CORRUPT_DATA;
    }
    if (!apta_internal_result_allocation_fits(
            result,
            (uint64_t)span_count * sizeof(apta_waveform_span_t) +
                packed_count * sizeof(apta_waveform_column_t),
            options->maximum_allocation_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    result->overview_spans =
        (apta_waveform_span_t *)apta_internal_context_allocate(
            context, (size_t)span_count * sizeof(apta_waveform_span_t),
            alignof(apta_waveform_span_t), APTA_MEMORY_PERSISTENT);
    if (result->overview_spans == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    result->overview_columns =
        (apta_waveform_column_t *)apta_internal_context_allocate(
            context, (size_t)packed_count * sizeof(apta_waveform_column_t),
            alignof(apta_waveform_column_t), APTA_MEMORY_PERSISTENT);
    if (result->overview_columns == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    memset(result->overview_spans, 0,
           (size_t)span_count * sizeof(apta_waveform_span_t));
    memset(result->overview_columns, 0,
           (size_t)packed_count * sizeof(apta_waveform_column_t));
    apta_waveform_overview_view_init(&result->overview);
    result->overview.level.level_id = apta_stream_get_u32(header);
    result->overview.level.frames_per_column = apta_stream_get_u32(header + 4u);
    result->overview.level.origin_frame = apta_stream_get_u64(header + 8u);
    result->overview.state = apta_stream_get_u32(header + 40u) & 7u;
    result->overview.confidence = APTA_CONFIDENCE_UNKNOWN;
    result->overview.span_count = span_count;
    result->overview.spans = result->overview_spans;
    packed_count = 0u;
    for (index = 0u; index < span_count; ++index) {
        uint8_t record[32];
        apta_waveform_span_t *span = &result->overview_spans[index];
        uint32_t columns;
        uint32_t column_index;
        status = apta_stream_read_section(stream, options, section,
                                          span_offset + (uint64_t)index * 32u,
                                          record, sizeof(record));
        if (status < 0)
            return status;
        columns = apta_stream_get_u32(record + 20u);
        apta_frame_range_init(&span->source_range);
        span->source_range.first_frame = apta_stream_get_u64(record);
        span->source_range.end_frame = apta_stream_get_u64(record + 8u);
        span->first_column_index = apta_stream_get_u32(record + 16u);
        span->column_count = columns;
        span->columns = &result->overview_columns[packed_count];
        for (column_index = 0u; column_index < columns; ++column_index) {
            uint8_t packed[10];
            uint64_t source_column =
                apta_stream_get_u32(record + 24u) + column_index;
            apta_waveform_column_t *column =
                &result->overview_columns[packed_count + column_index];
            status = apta_stream_read_section(
                stream, options, section, column_offset + source_column * 10u,
                packed, sizeof(packed));
            if (status < 0)
                return status;
            column->minimum = apta_stream_get_i16(packed);
            column->maximum = apta_stream_get_i16(packed + 2u);
            column->rms = apta_stream_get_u16(packed + 4u);
            column->low = packed[6];
            column->mid = packed[7];
            column->high = packed[8];
            column->flags = packed[9];
            if (column->minimum > column->maximum ||
                (column->flags & ~UINT8_C(0x1F)) != 0u) {
                return APTA_ERROR_CORRUPT_DATA;
            }
            if ((column->flags & APTA_WAVEFORM_COLUMN_HAS_3BAND) != 0u &&
                (options->requested_features & APTA_FEATURE_WAVEFORM_3BAND) !=
                    0u) {
                result->info.available_features |= APTA_FEATURE_WAVEFORM_3BAND;
                result->info.changed_features |= APTA_FEATURE_WAVEFORM_3BAND;
            }
        }
        packed_count += columns;
    }
    result->info.available_features |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    result->info.changed_features |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_parse_wdtl(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_section_t *section, apta_result_t *result)
{
    uint8_t header[16];
    uint64_t packed = 0u;
    uint32_t count;
    uint32_t index;
    apta_status_t status = apta_stream_read_section(stream, options, section,
                                                    0u, header, sizeof(header));
    if (status < 0)
        return status;
    count = apta_stream_get_u32(header);
    if (count == 0u || count > 65536u ||
        apta_stream_get_u32(header + 4u) != 0u ||
        apta_stream_get_u64(header + 8u) != 16u ||
        section->size < 16u + (uint64_t)count * 48u)
        return APTA_ERROR_CORRUPT_DATA;
    for (index = 0u; index < count; ++index) {
        uint8_t d[48];
        uint32_t columns;
        status =
            apta_stream_read_section(stream, options, section,
                                     16u + (uint64_t)index * 48u, d, sizeof(d));
        if (status < 0)
            return status;
        columns = apta_stream_get_u32(d + 28u);
        if (columns == 0u || apta_stream_get_u32(d) != 1u ||
            apta_stream_get_u64(d + 8u) >= apta_stream_get_u64(d + 16u) ||
            apta_stream_get_u64(d + 32u) < 16u + (uint64_t)count * 48u ||
            apta_stream_get_u64(d + 32u) > section->size ||
            (uint64_t)columns * 10u >
                section->size - apta_stream_get_u64(d + 32u) ||
            !apta_stream_state_allowed(apta_stream_get_u32(d + 40u), 0u) ||
            apta_stream_get_u16(d + 44u) != 0u ||
            !apta_stream_confidence_valid(d[46]) || d[47] != 0u ||
            packed > UINT32_MAX - columns)
            return APTA_ERROR_CORRUPT_DATA;
        packed += columns;
    }
    if (packed > options->maximum_waveform_columns ||
        !apta_internal_result_allocation_fits(
            result,
            (uint64_t)count * sizeof(apta_waveform_tile_view_t) +
                packed * sizeof(apta_waveform_column_t),
            options->maximum_allocation_bytes))
        return APTA_ERROR_LIMIT_EXCEEDED;
    result->detail_tiles =
        (apta_waveform_tile_view_t *)apta_internal_context_allocate(
            context, (size_t)count * sizeof(*result->detail_tiles),
            alignof(apta_waveform_tile_view_t), APTA_MEMORY_PERSISTENT);
    result->detail_columns =
        (apta_waveform_column_t *)apta_internal_context_allocate(
            context, (size_t)packed * sizeof(*result->detail_columns),
            alignof(apta_waveform_column_t), APTA_MEMORY_PERSISTENT);
    if (result->detail_tiles == NULL || result->detail_columns == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    memset(result->detail_tiles, 0,
           (size_t)count * sizeof(*result->detail_tiles));
    memset(result->detail_columns, 0,
           (size_t)packed * sizeof(*result->detail_columns));
    packed = 0u;
    for (index = 0u; index < count; ++index) {
        uint8_t d[48];
        uint32_t j, columns;
        apta_waveform_tile_view_t *t = &result->detail_tiles[index];
        status =
            apta_stream_read_section(stream, options, section,
                                     16u + (uint64_t)index * 48u, d, sizeof(d));
        if (status < 0)
            return status;
        columns = apta_stream_get_u32(d + 28u);
        t->struct_size = sizeof(*t);
        t->api_version = APTA_API_VERSION;
        t->level_id = apta_stream_get_u32(d);
        t->tile_index = apta_stream_get_u32(d + 4u);
        apta_frame_range_init(&t->source_range);
        t->source_range.first_frame = apta_stream_get_u64(d + 8u);
        t->source_range.end_frame = apta_stream_get_u64(d + 16u);
        t->first_column_index = apta_stream_get_u32(d + 24u);
        t->column_count = columns;
        t->columns = &result->detail_columns[packed];
        t->state = apta_stream_get_u32(d + 40u);
        t->confidence = d[46];
        for (j = 0u; j < columns; ++j) {
            uint8_t p[10];
            apta_waveform_column_t *c = &result->detail_columns[packed + j];
            status = apta_stream_read_section(
                stream, options, section,
                apta_stream_get_u64(d + 32u) + (uint64_t)j * 10u, p, sizeof(p));
            if (status < 0)
                return status;
            c->minimum = apta_stream_get_i16(p);
            c->maximum = apta_stream_get_i16(p + 2u);
            c->rms = apta_stream_get_u16(p + 4u);
            c->low = p[6];
            c->mid = p[7];
            c->high = p[8];
            c->flags = p[9];
            if (c->minimum > c->maximum)
                return APTA_ERROR_CORRUPT_DATA;
        }
        packed += columns;
    }
    result->detail_tile_count = count;
    result->info.available_features |= APTA_FEATURE_WAVEFORM_DETAIL;
    result->info.changed_features |= APTA_FEATURE_WAVEFORM_DETAIL;
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_parse_temp(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_layout_t *layout, apta_result_t *result)
{
    const apta_stream_input_section_t *s = &layout->known[3];
    uint8_t h[56];
    uint32_t count, index;
    apta_status_t status =
        apta_stream_read_section(stream, options, s, 0u, h, sizeof(h));
    if (status < 0)
        return status;
    count = apta_stream_get_u32(h + 48u);
    if (apta_stream_get_u16(h) != 1u ||
        !apta_stream_state_allowed(h[2], layout->container_flags) ||
        !apta_stream_confidence_valid(h[3]) || count == 0u ||
        count > APTA_INTERNAL_MAX_TEMPO_CANDIDATES ||
        s->size != 56u + (uint64_t)count * 16u ||
        apta_stream_get_u32(h + 52u) != 0u)
        return APTA_ERROR_CORRUPT_DATA;
    if (!apta_internal_result_allocation_fits(
            result, (uint64_t)count * sizeof(apta_tempo_candidate_t),
            options->maximum_allocation_bytes))
        return APTA_ERROR_LIMIT_EXCEEDED;
    result->tempo_candidates =
        (apta_tempo_candidate_t *)apta_internal_context_allocate(
            context, (size_t)count * sizeof(*result->tempo_candidates),
            alignof(apta_tempo_candidate_t), APTA_MEMORY_PERSISTENT);
    if (result->tempo_candidates == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    memset(result->tempo_candidates, 0,
           (size_t)count * sizeof(*result->tempo_candidates));
    apta_tempo_view_init(&result->tempo);
    result->tempo.selected.state = h[2];
    result->tempo.selected.confidence = h[3];
    result->tempo.selected.flags = apta_stream_get_u32(h + 4u);
    result->tempo.selected.tempo_millibpm = apta_stream_get_u32(h + 8u);
    result->tempo.selected.candidate_set_id = apta_stream_get_u32(h + 12u);
    result->tempo.selected.evidence_range.first_frame =
        apta_stream_get_u64(h + 16u);
    result->tempo.selected.evidence_range.end_frame =
        apta_stream_get_u64(h + 24u);
    result->tempo.selected.applicability_range.first_frame =
        apta_stream_get_u64(h + 32u);
    result->tempo.selected.applicability_range.end_frame =
        apta_stream_get_u64(h + 40u);
    for (index = 0u; index < count; ++index) {
        uint8_t p[16];
        apta_tempo_candidate_t *c = &result->tempo_candidates[index];
        status = apta_stream_read_section(
            stream, options, s, 56u + (uint64_t)index * 16u, p, sizeof(p));
        if (status < 0)
            return status;
        c->tempo_millibpm = apta_stream_get_u32(p);
        c->score = apta_stream_get_u16(p + 4u);
        c->confidence = p[6];
        c->relation_to_selected = p[7];
        c->flags = apta_stream_get_u32(p + 8u);
        if (!apta_stream_confidence_valid(c->confidence) ||
            apta_stream_get_u32(p + 12u) != 0u)
            return APTA_ERROR_CORRUPT_DATA;
    }
    result->tempo.candidate_count = count;
    result->tempo.candidates = result->tempo_candidates;
    result->info.available_features |=
        APTA_FEATURE_BPM | APTA_FEATURE_CONFIDENCE;
    result->info.changed_features |= APTA_FEATURE_BPM | APTA_FEATURE_CONFIDENCE;
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_parse_lgrd(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_layout_t *layout, apta_result_t *result)
{
    const apta_stream_input_section_t *s = &layout->known[4];
    uint8_t p[144];
    apta_grid_segment_t *segment;
    apta_status_t status =
        apta_stream_read_section(stream, options, s, 0u, p, sizeof(p));
    if (status < 0)
        return status;
    if (s->size != 144u || apta_stream_get_u16(p) != 1u ||
        !apta_stream_state_allowed(p[2], layout->container_flags) ||
        !apta_stream_confidence_valid(p[3]) ||
        apta_stream_get_u32(p + 8u) != APTA_GRID_REPRESENTATION_SEGMENTS ||
        apta_stream_get_u32(p + 12u) != 1u ||
        apta_stream_get_u64(p + 104u) == 0u ||
        !apta_stream_state_allowed(p[136], layout->container_flags) ||
        !apta_stream_confidence_valid(p[137]) ||
        !apta_stream_bytes_zero(p + 138u, 6u))
        return APTA_ERROR_CORRUPT_DATA;
    if (!apta_internal_result_allocation_fits(
            result, sizeof(apta_frame_range_t) + sizeof(apta_grid_segment_t),
            options->maximum_allocation_bytes))
        return APTA_ERROR_LIMIT_EXCEEDED;
    result->local_grid_coverage =
        (apta_frame_range_t *)apta_internal_context_allocate(
            context, sizeof(*result->local_grid_coverage),
            alignof(apta_frame_range_t), APTA_MEMORY_PERSISTENT);
    result->local_grid_segments =
        (apta_grid_segment_t *)apta_internal_context_allocate(
            context, sizeof(*result->local_grid_segments),
            alignof(apta_grid_segment_t), APTA_MEMORY_PERSISTENT);
    if (result->local_grid_coverage == NULL ||
        result->local_grid_segments == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    apta_frame_range_init(result->local_grid_coverage);
    result->local_grid_coverage->first_frame = apta_stream_get_u64(p + 64u);
    result->local_grid_coverage->end_frame = apta_stream_get_u64(p + 72u);
    segment = result->local_grid_segments;
    memset(segment, 0, sizeof(*segment));
    segment->struct_size = sizeof(*segment);
    segment->api_version = APTA_API_VERSION;
    apta_frame_range_init(&segment->applicability_range);
    segment->applicability_range.first_frame = apta_stream_get_u64(p + 48u);
    segment->applicability_range.end_frame = apta_stream_get_u64(p + 56u);
    segment->anchor_position.whole_frame = apta_stream_get_u64(p + 80u);
    segment->anchor_position.fraction_q32 = apta_stream_get_u32(p + 88u);
    segment->anchor_ordinal = apta_stream_get_i64(p + 96u);
    segment->frames_per_beat.whole_frames = apta_stream_get_u64(p + 104u);
    segment->frames_per_beat.fraction_q32 = apta_stream_get_u32(p + 112u);
    segment->beat_count = apta_stream_get_u32(p + 116u);
    segment->nominal_tempo_millibpm = apta_stream_get_u32(p + 120u);
    segment->segment_id = apta_stream_get_u32(p + 124u);
    segment->revision = apta_stream_get_u32(p + 128u);
    segment->flags = apta_stream_get_u32(p + 132u);
    segment->state = p[136];
    segment->confidence = p[137];
    apta_grid_view_init(&result->local_grid);
    result->local_grid.requested_range.first_frame =
        apta_stream_get_u64(p + 16u);
    result->local_grid.requested_range.end_frame = apta_stream_get_u64(p + 24u);
    result->local_grid.evidence_range.first_frame =
        apta_stream_get_u64(p + 32u);
    result->local_grid.evidence_range.end_frame = apta_stream_get_u64(p + 40u);
    result->local_grid.applicability_range = segment->applicability_range;
    result->local_grid.representation = APTA_GRID_REPRESENTATION_SEGMENTS;
    result->local_grid.state = p[2];
    result->local_grid.confidence = p[3];
    result->local_grid.coverage_range_count = 1u;
    result->local_grid.coverage_ranges = result->local_grid_coverage;
    result->local_grid.segment_count = 1u;
    result->local_grid.segments = segment;
    result->local_grid.flags = apta_stream_get_u32(p + 4u);
    result->info.available_features |= APTA_FEATURE_LOCAL_BEATGRID;
    if ((result->local_grid.flags & APTA_GRID_FLAG_LOCKED) != 0u)
        result->info.available_features |= APTA_FEATURE_GRID_LOCKING;
    result->info.changed_features |= result->info.available_features;
    return APTA_STATUS_OK;
}

typedef struct {
    uint8_t key;
    uint8_t major;
    uint64_t offset;
    uint64_t value;
} apta_stream_meta_item_t;

static apta_status_t
apta_stream_read_cbor_head(const apta_input_stream_t *stream,
                           const apta_stream_effective_options_t *options,
                           const apta_stream_input_section_t *section,
                           uint64_t *cursor, uint8_t *major_out,
                           uint64_t *value_out)
{
    uint8_t first;
    uint8_t tail[8];
    uint32_t count = 0u, index;
    apta_status_t status;
    status =
        apta_stream_read_section(stream, options, section, *cursor, &first, 1u);
    if (status < 0)
        return status;
    *cursor += 1u;
    *major_out = first >> 5u;
    first &= 31u;
    if (first < 24u) {
        *value_out = first;
        return APTA_STATUS_OK;
    }
    if (first == 24u)
        count = 1u;
    else if (first == 25u)
        count = 2u;
    else if (first == 26u)
        count = 4u;
    else if (first == 27u)
        count = 8u;
    else
        return APTA_ERROR_CORRUPT_DATA;
    status = apta_stream_read_section(stream, options, section, *cursor, tail,
                                      count);
    if (status < 0)
        return status;
    *cursor += count;
    *value_out = 0u;
    for (index = 0u; index < count; ++index)
        *value_out = (*value_out << 8u) | tail[index];
    if ((*value_out < 24u) || (count > 1u && *value_out <= UINT8_MAX) ||
        (count > 2u && *value_out <= UINT16_MAX) ||
        (count > 4u && *value_out <= UINT32_MAX))
        return APTA_ERROR_CORRUPT_DATA;
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_parse_meta(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_section_t *section, apta_result_t *result)
{
    apta_stream_meta_item_t items[7];
    uint64_t cursor = 0u, count64, total = 0u;
    uint32_t count, index;
    uint8_t major, last_key = 0u;
    apta_status_t status;
    status = apta_stream_read_cbor_head(stream, options, section, &cursor,
                                        &major, &count64);
    if (status < 0)
        return status;
    if (major != 5u || count64 > 7u)
        return APTA_ERROR_CORRUPT_DATA;
    count = (uint32_t)count64;
    memset(items, 0, sizeof(items));
    for (index = 0u; index < count; ++index) {
        uint64_t key, value;
        status = apta_stream_read_cbor_head(stream, options, section, &cursor,
                                            &major, &key);
        if (status < 0)
            return status;
        if (major != 0u || key < 1u || key > 7u || key <= last_key) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        last_key = (uint8_t)key;
        items[index].key = (uint8_t)key;
        status = apta_stream_read_cbor_head(stream, options, section, &cursor,
                                            &items[index].major, &value);
        if (status < 0)
            return status;
        items[index].value = value;
        if (key == 5u) {
            if (items[index].major != 0u)
                return APTA_ERROR_CORRUPT_DATA;
            continue;
        }
        if ((key == 6u && items[index].major != 2u &&
             items[index].major != 3u) ||
            (key != 6u && items[index].major != 3u) || value > UINT32_MAX ||
            cursor > section->size || value > section->size - cursor ||
            total > UINT64_MAX - value ||
            (key == 1u && value > APTA_METADATA_MAX_PRODUCER_NAME_BYTES) ||
            ((key == 2u || key == 4u) &&
             value > APTA_METADATA_MAX_VERSION_STRING_BYTES) ||
            (key == 3u && value > APTA_METADATA_MAX_BACKEND_NAME_BYTES) ||
            (key == 6u && value > APTA_METADATA_MAX_SOURCE_ID_BYTES) ||
            (key == 7u && value > APTA_METADATA_MAX_COMMENTS_BYTES)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        items[index].offset = cursor;
        cursor += value;
        total += value;
    }
    if (cursor != section->size)
        return APTA_ERROR_CORRUPT_DATA;
    if (total > APTA_METADATA_MAX_TOTAL_BYTES) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (!apta_internal_result_allocation_fits(
            result, total, options->maximum_allocation_bytes))
        return APTA_ERROR_LIMIT_EXCEEDED;
    memset(&result->metadata, 0, sizeof(result->metadata));
    apta_metadata_view_init(&result->metadata.view);
    if (total != 0u) {
        result->metadata.storage = (uint8_t *)apta_internal_context_allocate(
            context, (size_t)total, alignof(uint8_t), APTA_MEMORY_PERSISTENT);
        if (result->metadata.storage == NULL)
            return APTA_ERROR_OUT_OF_MEMORY;
    }
    result->metadata.storage_size = (size_t)total;
    result->metadata.present = 1u;
    total = 0u;
    for (index = 0u; index < count; ++index) {
        apta_stream_meta_item_t *i = &items[index];
        uint8_t *destination = result->metadata.storage != NULL
                                   ? result->metadata.storage + (size_t)total
                                   : NULL;
        if (i->key == 5u) {
            result->metadata.view.flags |=
                APTA_METADATA_FLAG_CREATION_TIME_PRESENT;
            result->metadata.view.creation_unix_time = i->value;
            continue;
        }
        if (i->value != 0u) {
            status = apta_stream_read_section(stream, options, section,
                                              i->offset, destination, i->value);
            if (status < 0)
                return status;
        }
        if (i->major == 3u &&
            !apta_stream_utf8_valid(destination, (uint32_t)i->value)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
#define APTA_SET_META_TEXT(field_, flag_)                                      \
    do {                                                                       \
        result->metadata.view.field_.data = (const char *)destination;         \
        result->metadata.view.field_.size = (uint32_t)i->value;                \
        result->metadata.view.flags |= (flag_);                                \
    } while (0)
        switch (i->key) {
        case 1u:
            APTA_SET_META_TEXT(producer_name,
                               APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT);
            break;
        case 2u:
            APTA_SET_META_TEXT(producer_version_string,
                               APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT);
            break;
        case 3u:
            APTA_SET_META_TEXT(backend_name,
                               APTA_METADATA_FLAG_BACKEND_NAME_PRESENT);
            break;
        case 4u:
            APTA_SET_META_TEXT(backend_version,
                               APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT);
            break;
        case 6u:
            result->metadata.view.application_source_id.data = destination;
            result->metadata.view.application_source_id.size =
                (uint32_t)i->value;
            result->metadata.view.application_source_id_kind =
                i->major == 3u ? APTA_METADATA_SOURCE_ID_TEXT
                               : APTA_METADATA_SOURCE_ID_BYTES;
            break;
        case 7u:
            APTA_SET_META_TEXT(comments, APTA_METADATA_FLAG_COMMENTS_PRESENT);
            break;
        default:
            return APTA_ERROR_CORRUPT_DATA;
        }
#undef APTA_SET_META_TEXT
        total += i->value;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_parse_ggrd_revn(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_layout_t *layout, apta_result_t *result)
{
    const apta_stream_input_section_t *g = &layout->known[5],
                                      *r = &layout->known[6];
    apta_internal_s6_result_state_t *state;
    apta_grid_view_t *grid;
    uint8_t h[96], rev[80];
    uint32_t segments, beats, index, representation;
    uint64_t bytes;
    apta_status_t status;
    if (!g->present || !r->present)
        return APTA_ERROR_CORRUPT_DATA;
    status = apta_stream_read_section(stream, options, g, 0u, h, sizeof(h));
    if (status < 0)
        return status;
    representation = apta_stream_get_u32(h + 8u);
    segments = apta_stream_get_u32(h + 16u);
    beats = apta_stream_get_u32(h + 20u);
    if (apta_stream_get_u16(h) != 1u ||
        !apta_stream_state_allowed(h[2], layout->container_flags) ||
        !apta_stream_confidence_valid(h[3]) ||
        representation < APTA_GRID_REPRESENTATION_SEGMENTS ||
        representation > APTA_GRID_REPRESENTATION_HYBRID ||
        apta_stream_get_u32(h + 12u) != 1u ||
        segments > APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS ||
        beats > APTA_REFERENCE_GLOBAL_GRID_MAX_BEATS ||
        g->size != 96u + (uint64_t)segments * 80u + (uint64_t)beats * 40u ||
        !apta_stream_bytes_zero(h + 88u, 8u))
        return APTA_ERROR_CORRUPT_DATA;
    bytes = sizeof(*state) + sizeof(apta_frame_range_t) +
            (uint64_t)segments * sizeof(apta_grid_segment_t) +
            (uint64_t)beats * sizeof(apta_beat_t);
    if (!apta_internal_result_allocation_fits(
            result, bytes, options->maximum_allocation_bytes))
        return APTA_ERROR_LIMIT_EXCEEDED;
    state = (apta_internal_s6_result_state_t *)apta_internal_context_allocate(
        context, sizeof(*state), alignof(apta_internal_s6_result_state_t),
        APTA_MEMORY_PERSISTENT);
    if (state == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    memset(state, 0, sizeof(*state));
    result->s6 = state;
    state->coverage_ranges =
        (apta_frame_range_t *)apta_internal_context_allocate(
            context, sizeof(*state->coverage_ranges),
            alignof(apta_frame_range_t), APTA_MEMORY_PERSISTENT);
    if (segments != 0u)
        state->segments = (apta_grid_segment_t *)apta_internal_context_allocate(
            context, (size_t)segments * sizeof(*state->segments),
            alignof(apta_grid_segment_t), APTA_MEMORY_PERSISTENT);
    if (beats != 0u)
        state->beats = (apta_beat_t *)apta_internal_context_allocate(
            context, (size_t)beats * sizeof(*state->beats),
            alignof(apta_beat_t), APTA_MEMORY_PERSISTENT);
    if (state->coverage_ranges == NULL ||
        (segments != 0u && state->segments == NULL) ||
        (beats != 0u && state->beats == NULL))
        return APTA_ERROR_OUT_OF_MEMORY;
    apta_frame_range_init(state->coverage_ranges);
    state->coverage_ranges->first_frame = apta_stream_get_u64(h + 72u);
    state->coverage_ranges->end_frame = apta_stream_get_u64(h + 80u);
    grid = &state->global_grid;
    apta_grid_view_init(grid);
    grid->requested_range.first_frame = apta_stream_get_u64(h + 24u);
    grid->requested_range.end_frame = apta_stream_get_u64(h + 32u);
    grid->evidence_range.first_frame = apta_stream_get_u64(h + 40u);
    grid->evidence_range.end_frame = apta_stream_get_u64(h + 48u);
    grid->applicability_range.first_frame = apta_stream_get_u64(h + 56u);
    grid->applicability_range.end_frame = apta_stream_get_u64(h + 64u);
    grid->representation = representation;
    grid->state = h[2];
    grid->confidence = h[3];
    grid->coverage_range_count = 1u;
    grid->coverage_ranges = state->coverage_ranges;
    grid->segment_count = segments;
    grid->segments = state->segments;
    grid->beat_count = beats;
    grid->beats = state->beats;
    grid->flags = apta_stream_get_u32(h + 4u);
    for (index = 0u; index < segments; ++index) {
        uint8_t p[80];
        apta_grid_segment_t *s = &state->segments[index];
        status = apta_stream_read_section(
            stream, options, g, 96u + (uint64_t)index * 80u, p, sizeof(p));
        if (status < 0)
            return status;
        memset(s, 0, sizeof(*s));
        s->struct_size = sizeof(*s);
        s->api_version = APTA_API_VERSION;
        apta_frame_range_init(&s->applicability_range);
        s->applicability_range.first_frame = apta_stream_get_u64(p);
        s->applicability_range.end_frame = apta_stream_get_u64(p + 8u);
        s->anchor_position.whole_frame = apta_stream_get_u64(p + 16u);
        s->anchor_position.fraction_q32 = apta_stream_get_u32(p + 24u);
        s->anchor_ordinal = apta_stream_get_i64(p + 32u);
        s->frames_per_beat.whole_frames = apta_stream_get_u64(p + 40u);
        s->frames_per_beat.fraction_q32 = apta_stream_get_u32(p + 48u);
        s->beat_count = apta_stream_get_u32(p + 52u);
        s->nominal_tempo_millibpm = apta_stream_get_u32(p + 56u);
        s->segment_id = apta_stream_get_u32(p + 60u);
        s->revision = apta_stream_get_u32(p + 64u);
        s->flags = apta_stream_get_u32(p + 68u);
        s->state = p[72];
        s->confidence = p[73];
        if (s->applicability_range.first_frame >=
                s->applicability_range.end_frame ||
            s->frames_per_beat.whole_frames == 0u ||
            !apta_stream_state_allowed(p[72], layout->container_flags) ||
            !apta_stream_confidence_valid(p[73]) ||
            !apta_stream_bytes_zero(p + 74u, 6u))
            return APTA_ERROR_CORRUPT_DATA;
    }
    for (index = 0u; index < beats; ++index) {
        uint8_t p[40];
        apta_beat_t *b = &state->beats[index];
        status = apta_stream_read_section(stream, options, g,
                                          96u + (uint64_t)segments * 80u +
                                              (uint64_t)index * 40u,
                                          p, sizeof(p));
        if (status < 0)
            return status;
        memset(b, 0, sizeof(*b));
        b->position.whole_frame = apta_stream_get_u64(p);
        b->position.fraction_q32 = apta_stream_get_u32(p + 8u);
        b->ordinal = apta_stream_get_i64(p + 16u);
        b->revision = apta_stream_get_u32(p + 24u);
        b->flags = apta_stream_get_u32(p + 28u);
        b->confidence = p[32];
        if (!apta_stream_confidence_valid(p[32]) ||
            !apta_stream_bytes_zero(p + 33u, 7u))
            return APTA_ERROR_CORRUPT_DATA;
    }
    status = apta_stream_read_section(stream, options, r, 0u, rev, sizeof(rev));
    if (status < 0)
        return status;
    if (r->size != 80u || apta_stream_get_u16(rev) != 1u ||
        (rev[2] != APTA_GRID_REVISION_PENDING &&
         rev[2] != APTA_GRID_REVISION_APPLIED) ||
        !apta_stream_confidence_valid(rev[3]) ||
        apta_stream_get_u32(rev + 8u) == 0u ||
        apta_stream_get_u32(rev + 16u) != representation ||
        apta_stream_get_u32(rev + 20u) != segments ||
        apta_stream_get_u32(rev + 24u) != beats ||
        !apta_stream_bytes_zero(rev + 48u, 32u))
        return APTA_ERROR_CORRUPT_DATA;
    apta_grid_revision_view_init(&state->revision);
    state->revision.state = rev[2];
    state->revision.confidence = rev[3];
    state->revision.flags = apta_stream_get_u32(rev + 4u);
    state->revision.revision_id = apta_stream_get_u32(rev + 8u);
    state->revision.previous_revision_id = apta_stream_get_u32(rev + 12u);
    state->revision.proposed_representation = representation;
    state->revision.proposed_segment_count = segments;
    state->revision.proposed_beat_count = beats;
    state->revision.affected_range.first_frame = apta_stream_get_u64(rev + 32u);
    state->revision.affected_range.end_frame = apta_stream_get_u64(rev + 40u);
    result->info.available_features |= APTA_FEATURE_GLOBAL_BEATGRID;
    if ((grid->flags & APTA_GRID_FLAG_DYNAMIC_TEMPO) != 0u)
        result->info.available_features |= APTA_FEATURE_DYNAMIC_TEMPO;
    if (grid->confidence != APTA_CONFIDENCE_UNKNOWN)
        result->info.available_features |= APTA_FEATURE_CONFIDENCE;
    result->info.changed_features |= result->info.available_features;
    return APTA_STATUS_OK;
}

static int apta_stream_state_allowed(uint8_t state, uint32_t container_flags)
{
    return state >= APTA_FEATURE_PROVISIONAL && state <= APTA_FEATURE_FINAL &&
           (((container_flags & APTA_STREAM_FLAG_PARTIAL) != 0u) ||
            state == APTA_FEATURE_FINAL);
}

static int apta_stream_confidence_valid(uint8_t confidence)
{
    return confidence <= APTA_CONFIDENCE_MAX ||
           confidence == APTA_CONFIDENCE_UNKNOWN;
}

static apta_status_t apta_stream_parse_key(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_layout_t *layout, apta_result_t *result)
{
    const apta_stream_input_section_t *section = &layout->known[7];
    uint8_t header[40];
    uint32_t count;
    uint32_t index;
    uint16_t previous_score = UINT16_MAX;
    int selected_found = 0;
    apta_status_t status = apta_stream_read_section(stream, options, section,
                                                    0u, header, sizeof(header));
    if (status < 0)
        return status;
    count = apta_stream_get_u32(header + 12u);
    if (apta_stream_get_u16(header) != 1u ||
        !apta_stream_state_allowed(header[2], layout->container_flags) ||
        !apta_stream_confidence_valid(header[3]) || header[4] > 11u ||
        (header[5] != APTA_KEY_MODE_MAJOR &&
         header[5] != APTA_KEY_MODE_MINOR) ||
        apta_stream_get_i16(header + 6u) < -100 ||
        apta_stream_get_i16(header + 6u) > 100 ||
        apta_stream_get_u32(header + 8u) != 0u || count > 24u ||
        apta_stream_get_u64(header + 16u) >=
            apta_stream_get_u64(header + 24u) ||
        apta_stream_get_u32(header + 32u) != 40u ||
        !apta_stream_bytes_zero(header + 36u, 4u) ||
        section->size != 40u + (uint64_t)count * 16u) {
        return count > 24u ? APTA_ERROR_LIMIT_EXCEEDED
                           : APTA_ERROR_CORRUPT_DATA;
    }
    for (index = 0u; index < count; ++index) {
        uint8_t record[16];
        status = apta_stream_read_section(stream, options, section,
                                          40u + (uint64_t)index * 16u, record,
                                          sizeof(record));
        if (status < 0)
            return status;
        if (record[0] > 11u ||
            (record[1] != APTA_KEY_MODE_MAJOR &&
             record[1] != APTA_KEY_MODE_MINOR) ||
            apta_stream_get_i16(record + 2u) < -100 ||
            apta_stream_get_i16(record + 2u) > 100 ||
            !apta_stream_confidence_valid(record[6]) || record[7] != 0u ||
            !apta_stream_bytes_zero(record + 8u, 8u) ||
            (index != 0u &&
             apta_stream_get_u16(record + 4u) >= previous_score)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        if (record[0] == header[4] && record[1] == header[5] &&
            apta_stream_get_i16(record + 2u) ==
                apta_stream_get_i16(header + 6u)) {
            selected_found = 1;
        }
        previous_score = apta_stream_get_u16(record + 4u);
    }
    if (count != 0u && !selected_found)
        return APTA_ERROR_CORRUPT_DATA;
    if (!apta_internal_result_allocation_fits(
            result, (uint64_t)count * sizeof(apta_key_candidate_t),
            options->maximum_allocation_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (count != 0u) {
        result->key_candidates =
            (apta_key_candidate_t *)apta_internal_context_allocate(
                context, (size_t)count * sizeof(apta_key_candidate_t),
                alignof(apta_key_candidate_t), APTA_MEMORY_PERSISTENT);
        if (result->key_candidates == NULL)
            return APTA_ERROR_OUT_OF_MEMORY;
        memset(result->key_candidates, 0,
               (size_t)count * sizeof(apta_key_candidate_t));
    }
    apta_key_view_init(&result->key);
    result->key.state = header[2];
    result->key.confidence = header[3];
    result->key.tonic = header[4];
    result->key.mode = header[5];
    result->key.tuning_offset_cents = apta_stream_get_i16(header + 6u);
    result->key.applicability_range.first_frame =
        apta_stream_get_u64(header + 16u);
    result->key.applicability_range.end_frame =
        apta_stream_get_u64(header + 24u);
    result->key.candidate_count = count;
    result->key.candidates = result->key_candidates;
    for (index = 0u; index < count; ++index) {
        uint8_t record[16];
        apta_key_candidate_t *candidate = &result->key_candidates[index];
        status = apta_stream_read_section(stream, options, section,
                                          40u + (uint64_t)index * 16u, record,
                                          sizeof(record));
        if (status < 0)
            return status;
        candidate->tonic = record[0];
        candidate->mode = record[1];
        candidate->tuning_offset_cents = apta_stream_get_i16(record + 2u);
        candidate->score = apta_stream_get_u16(record + 4u);
        candidate->confidence = record[6];
    }
    result->info.available_features |= APTA_FEATURE_MUSICAL_KEY;
    result->info.changed_features |= APTA_FEATURE_MUSICAL_KEY;
    return APTA_STATUS_OK;
}

static int apta_stream_meter_value_valid(uint16_t n, uint16_t d)
{
    return n >= 1u && n <= 32u && d >= 1u && d <= 32u && (d & (d - 1u)) == 0u;
}

static apta_status_t apta_stream_parse_meter(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_layout_t *layout, apta_result_t *result)
{
    const apta_stream_input_section_t *section = &layout->known[8];
    uint8_t header[48];
    uint32_t count;
    uint32_t index;
    uint64_t previous_end = 0u;
    int64_t previous_ordinal = INT64_MIN;
    uint32_t previous_id = 0u;
    apta_status_t status = apta_stream_read_section(stream, options, section,
                                                    0u, header, sizeof(header));
    if (status < 0)
        return status;
    count = apta_stream_get_u32(header + 12u);
    if (apta_stream_get_u16(header) != 1u ||
        !apta_stream_state_allowed(header[2], layout->container_flags) ||
        !apta_stream_confidence_valid(header[3]) ||
        !apta_stream_meter_value_valid(apta_stream_get_u16(header + 4u),
                                       apta_stream_get_u16(header + 6u)) ||
        apta_stream_get_u32(header + 8u) != 0u || count == 0u ||
        count > 65536u || apta_stream_get_u32(header + 32u) != 48u ||
        !apta_stream_bytes_zero(header + 36u, 12u) ||
        section->size != 48u + (uint64_t)count * 56u) {
        return count > 65536u ? APTA_ERROR_LIMIT_EXCEEDED
                              : APTA_ERROR_CORRUPT_DATA;
    }
    if (!apta_internal_result_allocation_fits(
            result, (uint64_t)count * sizeof(apta_meter_segment_t),
            options->maximum_allocation_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    result->meter_segments =
        (apta_meter_segment_t *)apta_internal_context_allocate(
            context, (size_t)count * sizeof(apta_meter_segment_t),
            alignof(apta_meter_segment_t), APTA_MEMORY_PERSISTENT);
    if (result->meter_segments == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    memset(result->meter_segments, 0,
           (size_t)count * sizeof(apta_meter_segment_t));
    apta_meter_view_init(&result->meter);
    result->meter.state = header[2];
    result->meter.confidence = header[3];
    result->meter.numerator = apta_stream_get_u16(header + 4u);
    result->meter.denominator = apta_stream_get_u16(header + 6u);
    result->meter.downbeat_frame = apta_stream_get_u64(header + 16u);
    result->meter.downbeat_ordinal = apta_stream_get_i64(header + 24u);
    result->meter.segment_count = count;
    result->meter.segments = result->meter_segments;
    for (index = 0u; index < count; ++index) {
        uint8_t record[56];
        apta_meter_segment_t *segment = &result->meter_segments[index];
        uint64_t first;
        uint64_t end;
        uint64_t downbeat;
        int64_t ordinal;
        uint32_t id;
        status = apta_stream_read_section(stream, options, section,
                                          48u + (uint64_t)index * 56u, record,
                                          sizeof(record));
        if (status < 0)
            return status;
        first = apta_stream_get_u64(record);
        end = apta_stream_get_u64(record + 8u);
        downbeat = apta_stream_get_u64(record + 16u);
        ordinal = apta_stream_get_i64(record + 24u);
        id = apta_stream_get_u32(record + 44u);
        if (first >= end || downbeat < first || downbeat >= end ||
            !apta_stream_meter_value_valid(apta_stream_get_u16(record + 32u),
                                           apta_stream_get_u16(record + 34u)) ||
            !apta_stream_state_allowed(record[36], layout->container_flags) ||
            record[36] < header[2] ||
            !apta_stream_confidence_valid(record[37]) ||
            !apta_stream_bytes_zero(record + 38u, 2u) ||
            apta_stream_get_u32(record + 40u) != 0u || id == 0u ||
            !apta_stream_bytes_zero(record + 48u, 8u) ||
            (index != 0u &&
             (previous_end > first || previous_ordinal >= ordinal ||
              previous_id >= id))) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        segment->struct_size = sizeof(*segment);
        segment->api_version = APTA_API_VERSION;
        apta_frame_range_init(&segment->applicability_range);
        segment->applicability_range.first_frame = first;
        segment->applicability_range.end_frame = end;
        segment->downbeat_frame = downbeat;
        segment->downbeat_ordinal = ordinal;
        segment->numerator = apta_stream_get_u16(record + 32u);
        segment->denominator = apta_stream_get_u16(record + 34u);
        segment->state = record[36];
        segment->confidence = record[37];
        segment->segment_id = id;
        previous_end = end;
        previous_ordinal = ordinal;
        previous_id = id;
    }
    if (result->meter.segments[0].downbeat_frame !=
            result->meter.downbeat_frame ||
        result->meter.segments[0].downbeat_ordinal !=
            result->meter.downbeat_ordinal ||
        result->meter.segments[0].numerator != result->meter.numerator ||
        result->meter.segments[0].denominator != result->meter.denominator) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    result->info.available_features |= APTA_FEATURE_METER_DOWNBEAT;
    result->info.changed_features |= APTA_FEATURE_METER_DOWNBEAT;
    return APTA_STATUS_OK;
}

static apta_status_t apta_stream_parse_quality(
    apta_context_t *context, const apta_input_stream_t *stream,
    const apta_stream_effective_options_t *options,
    const apta_stream_input_layout_t *layout, apta_result_t *result)
{
    const apta_stream_input_section_t *section = &layout->known[9];
    uint8_t header[16];
    uint32_t wire_count;
    uint32_t selected_count = 0u;
    uint32_t index;
    uint64_t previous_feature = 0u;
    apta_status_t status = apta_stream_read_section(stream, options, section,
                                                    0u, header, sizeof(header));
    if (status < 0)
        return status;
    wire_count = apta_stream_get_u32(header + 4u);
    if (apta_stream_get_u16(header) != 1u ||
        apta_stream_get_u16(header + 2u) != 32u || wire_count == 0u ||
        wire_count > 11u || apta_stream_get_u32(header + 8u) != 16u ||
        apta_stream_get_u32(header + 12u) != 0u ||
        section->size != 16u + (uint64_t)wire_count * 32u) {
        return wire_count > 11u ? APTA_ERROR_LIMIT_EXCEEDED
                                : APTA_ERROR_CORRUPT_DATA;
    }
    for (index = 0u; index < wire_count; ++index) {
        uint8_t record[32];
        uint64_t feature;
        uint16_t coverage;
        status = apta_stream_read_section(stream, options, section,
                                          16u + (uint64_t)index * 32u, record,
                                          sizeof(record));
        if (status < 0)
            return status;
        feature = apta_stream_get_u64(record);
        coverage = apta_stream_get_u16(record + 12u);
        if (feature == 0u || (feature & (feature - 1u)) != 0u ||
            feature <= previous_feature ||
            (coverage > APTA_EVIDENCE_COVERAGE_MAX &&
             coverage != APTA_EVIDENCE_COVERAGE_UNKNOWN) ||
            !apta_stream_confidence_valid(record[14]) ||
            !apta_stream_state_allowed(record[15], layout->container_flags) ||
            (apta_stream_get_u32(record + 16u) & ~UINT32_C(15)) != 0u ||
            !apta_stream_bytes_zero(record + 20u, 12u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        if ((options->requested_features & feature) != 0u &&
            (result->info.available_features & feature) != 0u) {
            ++selected_count;
        }
        previous_feature = feature;
    }
    if (selected_count == 0u)
        return APTA_STATUS_NOT_AVAILABLE;
    if (!apta_internal_result_allocation_fits(
            result, (uint64_t)selected_count * sizeof(apta_quality_view_t),
            options->maximum_allocation_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    result->quality = (apta_quality_view_t *)apta_internal_context_allocate(
        context, (size_t)selected_count * sizeof(apta_quality_view_t),
        alignof(apta_quality_view_t), APTA_MEMORY_PERSISTENT);
    if (result->quality == NULL)
        return APTA_ERROR_OUT_OF_MEMORY;
    memset(result->quality, 0,
           (size_t)selected_count * sizeof(apta_quality_view_t));
    selected_count = 0u;
    for (index = 0u; index < wire_count; ++index) {
        uint8_t record[32];
        uint64_t feature;
        status = apta_stream_read_section(stream, options, section,
                                          16u + (uint64_t)index * 32u, record,
                                          sizeof(record));
        if (status < 0)
            return status;
        feature = apta_stream_get_u64(record);
        if ((options->requested_features & feature) != 0u &&
            (result->info.available_features & feature) != 0u) {
            apta_quality_view_t *quality = &result->quality[selected_count++];
            apta_quality_view_init(quality);
            quality->feature = feature;
            quality->calibration_model_id = apta_stream_get_u32(record + 8u);
            quality->evidence_coverage_permille =
                apta_stream_get_u16(record + 12u);
            quality->confidence = record[14];
            quality->state = record[15];
            quality->flags = apta_stream_get_u32(record + 16u);
        }
    }
    result->quality_count = selected_count;
    result->info.available_features |= APTA_FEATURE_CALIBRATED_QUALITY;
    result->info.changed_features |= APTA_FEATURE_CALIBRATED_QUALITY;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_serialize_to_stream(
    const apta_result_t *result, const apta_serialize_options_t *options,
    const apta_output_stream_t *stream, uint64_t *bytes_written_out)
{
    apta_stream_output_section_t sections[APTA_STREAM_MAX_SECTIONS];
    uint8_t header[APTA_STREAM_HEADER_SIZE];
    uint8_t entry[APTA_STREAM_ENTRY_SIZE];
    uint8_t padding[8] = {0};
    uint64_t total_size;
    uint64_t position;
    uint32_t container_flags;
    uint32_t count;
    uint32_t index;
    apta_status_t status;
    if (bytes_written_out == NULL)
        return APTA_ERROR_INVALID_ARGUMENT;
    *bytes_written_out = 0u;
    if (result == NULL || !apta_output_stream_is_valid(stream)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    status = apta_stream_build_basic_layout(result, options, sections, &count,
                                            &total_size, &container_flags);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE)
        return status;
    for (index = 0u; index < count; ++index) {
        apta_stream_emitter_t emitter = {NULL, UINT32_C(0xFFFFFFFF), 0};
        status =
            apta_stream_emit_section(&emitter, result, sections[index].kind);
        if (status < 0)
            return status;
        sections[index].crc = emitter.crc ^ UINT32_C(0xFFFFFFFF);
    }
    status = stream->seek(stream->user_data, sections[0].offset);
    if (status != APTA_STATUS_OK)
        return status < 0 ? status : APTA_ERROR_SOURCE;
    position = sections[0].offset;
    for (index = 0u; index < count; ++index) {
        apta_stream_emitter_t emitter = {stream, UINT32_C(0xFFFFFFFF), 1};
        if (position < sections[index].offset) {
            status = apta_stream_write_exact(stream, padding,
                                             sections[index].offset - position);
            if (status < 0)
                return status;
            position = sections[index].offset;
        }
        status =
            apta_stream_emit_section(&emitter, result, sections[index].kind);
        if (status < 0)
            return status;
        if ((emitter.crc ^ UINT32_C(0xFFFFFFFF)) != sections[index].crc) {
            return APTA_ERROR_INTERNAL;
        }
        position += sections[index].size;
    }
    memset(header, 0, sizeof(header));
    memcpy(header, "APTA", 4u);
    apta_stream_put_u16(header + 4u, APTA_STREAM_HEADER_SIZE);
    apta_stream_put_u16(header + 6u, 1u);
    apta_stream_put_u16(header + 8u, (uint16_t)APTA_SPEC_VERSION_MAJOR);
    apta_stream_put_u16(header + 10u, (uint16_t)APTA_SPEC_VERSION_MINOR);
    apta_stream_put_u32(header + 12u, APTA_API_VERSION);
    apta_stream_put_u32(header + 16u, container_flags);
    apta_stream_put_u32(header + 20u, count);
    apta_stream_put_u64(header + 24u, APTA_STREAM_HEADER_SIZE);
    apta_stream_put_u64(header + 32u, total_size);
    apta_stream_put_u64(header + 40u, result->total_source_frames);
    apta_stream_put_u32(header + 48u, result->source_sample_rate);
    apta_stream_put_u16(header + 52u, (uint16_t)result->source_channel_count);
    apta_stream_put_u16(header + 54u, (uint16_t)result->source_channel_layout);
    memcpy(header + 56u, result->source_info.fingerprint,
           APTA_SOURCE_FINGERPRINT_SIZE);
    apta_stream_put_u32(header + 88u, result->source_info.fingerprint_kind);
    apta_stream_put_u32(header + 92u, apta_internal_crc32c(header, 92u));
    status = stream->seek(stream->user_data, 0u);
    if (status != APTA_STATUS_OK)
        return status < 0 ? status : APTA_ERROR_SOURCE;
    status = apta_stream_write_exact(stream, header, sizeof(header));
    if (status < 0)
        return status;
    for (index = 0u; index < count; ++index) {
        memset(entry, 0, sizeof(entry));
        memcpy(entry, sections[index].id, 4u);
        apta_stream_put_u16(entry + 4u, 1u);
        apta_stream_put_u16(entry + 6u, sections[index].flags);
        apta_stream_put_u64(entry + 8u, sections[index].offset);
        apta_stream_put_u64(entry + 16u, sections[index].size);
        apta_stream_put_u64(entry + 24u, sections[index].size);
        apta_stream_put_u32(entry + 32u, sections[index].crc);
        status = apta_stream_write_exact(stream, entry, sizeof(entry));
        if (status < 0)
            return status;
    }
    status = stream->flush(stream->user_data);
    if (status != APTA_STATUS_OK)
        return status < 0 ? status : APTA_ERROR_SOURCE;
    *bytes_written_out = total_size;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_parse_from_stream(
    apta_context_t *context, const apta_stream_parse_options_t *options,
    const apta_input_stream_t *stream, const apta_result_t **result_out)
{
    apta_stream_effective_options_t effective;
    apta_stream_input_layout_t layout;
    apta_result_t *result = NULL;
    uint64_t final_size = 0u;
    apta_status_t status;
    if (result_out == NULL)
        return APTA_ERROR_INVALID_ARGUMENT;
    *result_out = NULL;
    if (context == NULL || !apta_input_stream_is_valid(stream)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    memset(&layout, 0, sizeof(layout));
    status = apta_stream_resolve_options(context, options, &effective);
    if (status < 0)
        return status;
    status = apta_stream_validate_container(stream, &effective, &layout);
    if (status < 0)
        goto cleanup;
    if (apta_stream_get_u64(layout.header + 40u) == APTA_TOTAL_FRAMES_UNKNOWN) {
        if ((layout.container_flags &
             (APTA_STREAM_FLAG_PARTIAL | APTA_STREAM_FLAG_DURATION_UNKNOWN)) !=
            (APTA_STREAM_FLAG_PARTIAL | APTA_STREAM_FLAG_DURATION_UNKNOWN)) {
            status = APTA_ERROR_CORRUPT_DATA;
            goto cleanup;
        }
    } else if ((layout.container_flags & APTA_STREAM_FLAG_DURATION_UNKNOWN) !=
               0u) {
        status = APTA_ERROR_CORRUPT_DATA;
        goto cleanup;
    }
    if (!apta_internal_source_fingerprint_is_valid(
            apta_stream_get_u32(layout.header + 88u), layout.header + 56u)) {
        status = APTA_ERROR_UNSUPPORTED;
        goto cleanup;
    }
    status = apta_stream_create_result(context, &effective, &layout, &result);
    if (status < 0)
        goto cleanup;
    if (layout.known[0].present) {
        status = apta_stream_parse_meta(context, stream, &effective,
                                        &layout.known[0], result);
        if (status < 0)
            goto cleanup;
    }
    if ((effective.requested_features & (APTA_FEATURE_WAVEFORM_OVERVIEW |
                                         APTA_FEATURE_WAVEFORM_3BAND)) != 0u) {
        status = apta_stream_parse_wovr(context, stream, &effective,
                                        &layout.known[1], result);
        if (status < 0)
            goto cleanup;
    }
    if ((effective.requested_features & APTA_FEATURE_WAVEFORM_DETAIL) != 0u &&
        layout.known[2].present) {
        status = apta_stream_parse_wdtl(context, stream, &effective,
                                        &layout.known[2], result);
        if (status < 0)
            goto cleanup;
    }
    if ((effective.requested_features &
         (APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID |
          APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO |
          APTA_FEATURE_GRID_LOCKING)) != 0u &&
        layout.known[3].present) {
        status = apta_stream_parse_temp(context, stream, &effective, &layout,
                                        result);
        if (status < 0)
            goto cleanup;
    }
    if ((effective.requested_features &
         (APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GRID_LOCKING)) != 0u &&
        layout.known[4].present) {
        if ((result->info.available_features & APTA_FEATURE_BPM) == 0u) {
            status = APTA_ERROR_INVALID_ARGUMENT;
            goto cleanup;
        }
        status = apta_stream_parse_lgrd(context, stream, &effective, &layout,
                                        result);
        if (status < 0)
            goto cleanup;
    }
    if ((effective.requested_features &
         (APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO |
          APTA_FEATURE_GRID_LOCKING)) != 0u &&
        (layout.known[5].present || layout.known[6].present)) {
        if ((result->info.available_features & APTA_FEATURE_BPM) == 0u) {
            status = APTA_ERROR_INVALID_ARGUMENT;
            goto cleanup;
        }
        status = apta_stream_parse_ggrd_revn(context, stream, &effective,
                                             &layout, result);
        if (status < 0)
            goto cleanup;
    }
    if ((effective.requested_features & APTA_FEATURE_MUSICAL_KEY) != 0u &&
        layout.known[7].present) {
        status =
            apta_stream_parse_key(context, stream, &effective, &layout, result);
        if (status < 0)
            goto cleanup;
    }
    if ((effective.requested_features & APTA_FEATURE_METER_DOWNBEAT) != 0u &&
        layout.known[8].present) {
        status = apta_stream_parse_meter(context, stream, &effective, &layout,
                                         result);
        if (status < 0)
            goto cleanup;
    }
    if ((effective.requested_features & APTA_FEATURE_CALIBRATED_QUALITY) !=
            0u &&
        layout.known[9].present) {
        status = apta_stream_parse_quality(context, stream, &effective, &layout,
                                           result);
        if (status < 0 && status != APTA_STATUS_NOT_AVAILABLE)
            goto cleanup;
    }
    status = apta_stream_validate_materialized_result(&effective, result);
    if (status < 0)
        goto cleanup;
    /* Derived bits never escape the caller's explicit selection. */
    result->info.available_features &= effective.requested_features;
    result->info.changed_features &= effective.requested_features;
    status = stream->get_size(stream->user_data, &final_size);
    if (status != APTA_STATUS_OK) {
        status = status < 0 ? status : APTA_ERROR_SOURCE;
        goto cleanup;
    }
    if (final_size != layout.file_size) {
        status = APTA_ERROR_CORRUPT_DATA;
        goto cleanup;
    }
    *result_out = result;
    result = NULL;
    status = APTA_STATUS_OK;

cleanup:
    if (result != NULL)
        apta_internal_result_release(result);
    apta_stream_release_options(context, &effective);
    return status;
}
