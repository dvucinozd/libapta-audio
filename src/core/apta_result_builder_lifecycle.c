// SPDX-License-Identifier: Apache-2.0
#include "apta_result_builder_internal.h"

#include <stdalign.h>
#include <string.h>

#define APTA_BUILDER_DEFAULT_MAX_OVERVIEW_SPANS 65536u
#define APTA_BUILDER_DEFAULT_MAX_WAVEFORM_COLUMNS 16777216u
#define APTA_BUILDER_DEFAULT_MAX_DETAIL_TILES 65536u
#define APTA_BUILDER_DEFAULT_MAX_TEMPO_CANDIDATES 32u
#define APTA_BUILDER_DEFAULT_MAX_GRID_COVERAGE 65536u
#define APTA_BUILDER_DEFAULT_MAX_GRID_SEGMENTS 65536u
#define APTA_BUILDER_DEFAULT_MAX_GRID_BEATS 1048576u
#define APTA_BUILDER_DEFAULT_MAX_KEY_CANDIDATES 24u
#define APTA_BUILDER_DEFAULT_MAX_METER_SEGMENTS 65536u
#define APTA_BUILDER_DEFAULT_MAX_QUALITY_RECORDS 11u
#define APTA_BUILDER_DEFAULT_MAX_ALLOCATION_BYTES UINT64_C(268435456)

static void apta_builder_init_prefix(void *value, size_t size)
{
    memset(value, 0, size);
    ((uint32_t *)value)[0] = (uint32_t)size;
    ((uint32_t *)value)[1] = APTA_API_VERSION;
}

void APTA_CALL apta_result_builder_options_init(
    apta_result_builder_options_t *options)
{
    if (options == NULL) {
        return;
    }
    apta_builder_init_prefix(options, sizeof(*options));
    options->maximum_overview_spans =
        APTA_BUILDER_DEFAULT_MAX_OVERVIEW_SPANS;
    options->maximum_waveform_columns =
        APTA_BUILDER_DEFAULT_MAX_WAVEFORM_COLUMNS;
    options->maximum_detail_tiles =
        APTA_BUILDER_DEFAULT_MAX_DETAIL_TILES;
    options->maximum_tempo_candidates =
        APTA_BUILDER_DEFAULT_MAX_TEMPO_CANDIDATES;
    options->maximum_grid_coverage_ranges =
        APTA_BUILDER_DEFAULT_MAX_GRID_COVERAGE;
    options->maximum_grid_segments =
        APTA_BUILDER_DEFAULT_MAX_GRID_SEGMENTS;
    options->maximum_grid_beats = APTA_BUILDER_DEFAULT_MAX_GRID_BEATS;
    options->maximum_key_candidates =
        APTA_BUILDER_DEFAULT_MAX_KEY_CANDIDATES;
    options->maximum_meter_segments =
        APTA_BUILDER_DEFAULT_MAX_METER_SEGMENTS;
    options->maximum_quality_records =
        APTA_BUILDER_DEFAULT_MAX_QUALITY_RECORDS;
    options->maximum_allocation_bytes =
        APTA_BUILDER_DEFAULT_MAX_ALLOCATION_BYTES;
}

void APTA_CALL apta_result_builder_info_init(
    apta_result_builder_info_t *info)
{
    if (info != NULL) {
        apta_builder_init_prefix(info, sizeof(*info));
        info->generation = 1u;
        info->session_state = APTA_SESSION_COMPLETED;
    }
}

void APTA_CALL apta_result_provenance_init(
    apta_result_provenance_t *provenance)
{
    if (provenance != NULL) {
        apta_builder_init_prefix(provenance, sizeof(*provenance));
    }
}

void APTA_CALL apta_waveform_detail_input_init(
    apta_waveform_detail_input_t *input)
{
    if (input != NULL) {
        apta_builder_init_prefix(input, sizeof(*input));
    }
}

int apta_builder_bytes_zero(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

int apta_builder_can_replace(
    const apta_result_builder_t *builder,
    uint64_t old_bytes,
    uint64_t new_bytes)
{
    uint64_t retained;
    uint64_t limit;
    if (builder == NULL || old_bytes > builder->payload_bytes) {
        return 0;
    }
    retained = builder->payload_bytes - old_bytes;
    limit = builder->options.maximum_allocation_bytes;
    return retained <= limit && new_bytes <= limit - retained &&
           sizeof(apta_result_t) <= limit - retained - new_bytes;
}

void *apta_builder_allocate_copy(
    apta_result_builder_t *builder,
    const void *source,
    uint32_t count,
    size_t element_size,
    size_t alignment)
{
    void *copy;
    size_t bytes;
    if (count == 0u) {
        return NULL;
    }
    if (source == NULL ||
        !apta_internal_size_array_fits(0u, count, element_size)) {
        return NULL;
    }
    bytes = (size_t)count * element_size;
    copy = apta_internal_context_allocate(
        builder->context, bytes, alignment, APTA_MEMORY_PERSISTENT);
    if (copy != NULL) {
        memcpy(copy, source, bytes);
    }
    return copy;
}

void apta_builder_clear_grid(
    apta_result_builder_t *builder,
    apta_builder_grid_t *grid)
{
    apta_internal_context_deallocate(builder->context, grid->coverage);
    apta_internal_context_deallocate(builder->context, grid->segments);
    apta_internal_context_deallocate(builder->context, grid->beats);
    memset(grid, 0, sizeof(*grid));
}

static void apta_builder_clear_payload(apta_result_builder_t *builder)
{
    if (builder == NULL) {
        return;
    }
    apta_internal_metadata_cleanup(builder->context, &builder->metadata);
    apta_internal_context_deallocate(
        builder->context, builder->provenance_storage);
    apta_internal_context_deallocate(builder->context, builder->overview_spans);
    apta_internal_context_deallocate(builder->context, builder->overview_columns);
    apta_internal_context_deallocate(builder->context, builder->detail_tiles);
    apta_internal_context_deallocate(builder->context, builder->detail_columns);
    apta_internal_context_deallocate(builder->context, builder->tempo_candidates);
    apta_builder_clear_grid(builder, &builder->local_grid);
    apta_builder_clear_grid(builder, &builder->global_grid);
    apta_internal_context_deallocate(builder->context, builder->key_candidates);
    apta_internal_context_deallocate(builder->context, builder->meter_segments);
    apta_internal_context_deallocate(builder->context, builder->quality);

    memset(&builder->metadata, 0, sizeof(builder->metadata));
    apta_metadata_view_init(&builder->metadata.view);
    apta_result_provenance_init(&builder->provenance);
    builder->provenance_storage = NULL;
    builder->provenance_storage_size = 0u;
    apta_waveform_overview_view_init(&builder->overview);
    builder->overview_spans = NULL;
    builder->overview_columns = NULL;
    builder->overview_column_count = 0u;
    builder->detail_tile_count = 0u;
    builder->detail_tiles = NULL;
    builder->detail_columns = NULL;
    builder->detail_column_count = 0u;
    apta_tempo_view_init(&builder->tempo);
    builder->tempo_candidates = NULL;
    apta_key_view_init(&builder->key);
    builder->key_candidates = NULL;
    apta_meter_view_init(&builder->meter);
    builder->meter_segments = NULL;
    builder->quality_count = 0u;
    builder->quality = NULL;
    builder->payload_bytes = 0u;
    builder->has_source = 0u;
    builder->has_provenance = 0u;
}

void APTA_CALL apta_result_builder_reset(apta_result_builder_t *builder)
{
    if (builder == NULL) {
        return;
    }
    apta_builder_clear_payload(builder);
    apta_result_builder_info_init(&builder->info);
    apta_source_info_init(&builder->source);
}

apta_status_t APTA_CALL apta_result_builder_create(
    apta_context_t *context,
    const apta_result_builder_options_t *options,
    apta_result_builder_t **builder_out)
{
    apta_result_builder_options_t effective;
    apta_result_builder_t *builder;

    if (builder_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *builder_out = NULL;
    if (context == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    apta_result_builder_options_init(&effective);
    if (options != NULL) {
        if (!apta_internal_validate_struct(
                options, sizeof(*options), options->struct_size,
                options->api_version)) {
            return APTA_ERROR_INCOMPATIBLE_VERSION;
        }
        if (options->flags != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        if (!apta_builder_bytes_zero(
                options->reserved32, sizeof(options->reserved32)) ||
            !apta_builder_bytes_zero(
                options->reserved64, sizeof(options->reserved64))) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        effective = *options;
#define APTA_BUILDER_DEFAULT_IF_ZERO(field, value) \
        do { if (effective.field == 0u) effective.field = (value); } while (0)
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_overview_spans,
                                     APTA_BUILDER_DEFAULT_MAX_OVERVIEW_SPANS);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_waveform_columns,
                                     APTA_BUILDER_DEFAULT_MAX_WAVEFORM_COLUMNS);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_detail_tiles,
                                     APTA_BUILDER_DEFAULT_MAX_DETAIL_TILES);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_tempo_candidates,
                                     APTA_BUILDER_DEFAULT_MAX_TEMPO_CANDIDATES);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_grid_coverage_ranges,
                                     APTA_BUILDER_DEFAULT_MAX_GRID_COVERAGE);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_grid_segments,
                                     APTA_BUILDER_DEFAULT_MAX_GRID_SEGMENTS);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_grid_beats,
                                     APTA_BUILDER_DEFAULT_MAX_GRID_BEATS);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_key_candidates,
                                     APTA_BUILDER_DEFAULT_MAX_KEY_CANDIDATES);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_meter_segments,
                                     APTA_BUILDER_DEFAULT_MAX_METER_SEGMENTS);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_quality_records,
                                     APTA_BUILDER_DEFAULT_MAX_QUALITY_RECORDS);
        APTA_BUILDER_DEFAULT_IF_ZERO(maximum_allocation_bytes,
                                     APTA_BUILDER_DEFAULT_MAX_ALLOCATION_BYTES);
#undef APTA_BUILDER_DEFAULT_IF_ZERO
    }
    if (effective.maximum_allocation_bytes < sizeof(apta_result_t)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    builder = (apta_result_builder_t *)apta_internal_context_allocate(
        context, sizeof(*builder), alignof(apta_result_builder_t),
        APTA_MEMORY_PERSISTENT);
    if (builder == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(builder, 0, sizeof(*builder));
    builder->context = context;
    builder->options = effective;
    apta_result_builder_reset(builder);
    *builder_out = builder;
    return APTA_STATUS_OK;
}

void APTA_CALL apta_result_builder_destroy(apta_result_builder_t *builder)
{
    apta_context_t *context;
    if (builder == NULL) {
        return;
    }
    context = builder->context;
    apta_builder_clear_payload(builder);
    apta_internal_context_deallocate(context, builder);
}

apta_status_t APTA_CALL apta_result_builder_set_info(
    apta_result_builder_t *builder,
    const apta_result_builder_info_t *info)
{
    if (builder == NULL || info == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            info, sizeof(*info), info->struct_size, info->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (info->generation == 0u || info->container_version > 1u ||
        info->session_state > APTA_SESSION_FAILED || info->flags != 0u ||
        !apta_builder_bytes_zero(info->reserved32, sizeof(info->reserved32)) ||
        !apta_builder_bytes_zero(info->reserved64, sizeof(info->reserved64))) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    builder->info = *info;
    return APTA_STATUS_OK;
}

static int apta_builder_source_valid(const apta_source_info_t *source)
{
    uint16_t expected_channels = 0u;
    if (source->sample_rate == 0u || source->sample_rate > 768000u ||
        source->channel_count == 0u || source->channel_count > 8u ||
        source->flags != 0u || source->reserved16 != 0u ||
        !apta_builder_bytes_zero(source->reserved32, sizeof(source->reserved32))) {
        return 0;
    }
    if (source->channel_layout == APTA_CHANNEL_LAYOUT_MONO) {
        expected_channels = 1u;
    } else if (source->channel_layout == APTA_CHANNEL_LAYOUT_STEREO) {
        expected_channels = 2u;
    } else if (source->channel_layout != APTA_CHANNEL_LAYOUT_UNSPECIFIED) {
        return 0;
    }
    if (expected_channels != 0u && source->channel_count != expected_channels) {
        return 0;
    }
    if (source->fingerprint_kind >
        APTA_SOURCE_FINGERPRINT_SHA256_SOURCE_OBJECT_BYTES) {
        return 0;
    }
    return source->fingerprint_kind != APTA_SOURCE_FINGERPRINT_NONE ||
           apta_builder_bytes_zero(
               source->fingerprint, sizeof(source->fingerprint));
}

apta_status_t APTA_CALL apta_result_builder_set_source_info(
    apta_result_builder_t *builder,
    const apta_source_info_t *source_info)
{
    if (builder == NULL || source_info == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            source_info, sizeof(*source_info), source_info->struct_size,
            source_info->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (!apta_builder_source_valid(source_info)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    builder->source = *source_info;
    builder->has_source = 1u;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_set_metadata(
    apta_result_builder_t *builder,
    const apta_metadata_t *metadata)
{
    apta_internal_metadata_t replacement;
    apta_status_t status;
    uint64_t old_bytes;
    uint64_t input_bytes;
    if (builder == NULL || metadata == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            metadata, sizeof(*metadata), metadata->struct_size,
            metadata->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    input_bytes = (uint64_t)metadata->producer_name.size +
                  metadata->producer_version_string.size +
                  metadata->backend_name.size + metadata->backend_version.size +
                  metadata->application_source_id.size + metadata->comments.size;
    old_bytes = builder->metadata.storage_size;
    if (!apta_builder_can_replace(builder, old_bytes, input_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    memset(&replacement, 0, sizeof(replacement));
    status = apta_internal_metadata_copy_from_input(
        builder->context, metadata, &replacement);
    if (status < 0) {
        return status;
    }
    apta_internal_metadata_cleanup(builder->context, &builder->metadata);
    builder->metadata = replacement;
    builder->payload_bytes = builder->payload_bytes - old_bytes +
                             replacement.storage_size;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_set_provenance(
    apta_result_builder_t *builder,
    const apta_result_provenance_t *provenance)
{
    apta_metadata_t carrier;
    apta_internal_metadata_t copied;
    apta_result_provenance_t replacement;
    uint64_t bytes;
    apta_status_t status;
    if (builder == NULL || provenance == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            provenance, sizeof(*provenance), provenance->struct_size,
            provenance->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (provenance->origin != APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT) {
        return provenance->origin == APTA_RESULT_PROVENANCE_NATIVE_ANALYSIS
                   ? APTA_ERROR_UNSUPPORTED
                   : APTA_ERROR_INVALID_ARGUMENT;
    }
    if (provenance->flags != 0u || provenance->source_name.size == 0u ||
        provenance->source_name.size >
            APTA_RESULT_PROVENANCE_MAX_SOURCE_NAME_BYTES ||
        provenance->source_version.size >
            APTA_RESULT_PROVENANCE_MAX_VERSION_BYTES ||
        !apta_builder_bytes_zero(
            provenance->reserved32, sizeof(provenance->reserved32)) ||
        !apta_builder_bytes_zero(
            provenance->reserved64, sizeof(provenance->reserved64))) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    bytes = (uint64_t)provenance->source_name.size +
            provenance->source_version.size;
    if (!apta_builder_can_replace(
            builder, builder->provenance_storage_size, bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    apta_metadata_init(&carrier);
    carrier.producer_name = provenance->source_name;
    carrier.producer_version_string = provenance->source_version;
    carrier.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    if (provenance->source_version.size != 0u) {
        carrier.flags |= APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT;
    }
    memset(&copied, 0, sizeof(copied));
    status = apta_internal_metadata_copy_from_input(
        builder->context, &carrier, &copied);
    if (status < 0) {
        return status;
    }
    replacement = *provenance;
    replacement.source_name = copied.view.producer_name;
    replacement.source_version = copied.view.producer_version_string;
    apta_internal_context_deallocate(
        builder->context, builder->provenance_storage);
    builder->payload_bytes = builder->payload_bytes -
                             builder->provenance_storage_size +
                             copied.storage_size;
    builder->provenance = replacement;
    builder->provenance_storage = copied.storage;
    builder->provenance_storage_size = copied.storage_size;
    builder->has_provenance = 1u;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_get_provenance(
    const apta_result_t *result,
    apta_result_provenance_t *provenance_out)
{
    if (result == NULL || provenance_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            provenance_out, sizeof(*provenance_out),
            provenance_out->struct_size, provenance_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (result->provenance.origin == APTA_RESULT_PROVENANCE_UNSPECIFIED) {
        apta_result_provenance_init(provenance_out);
        return APTA_STATUS_NOT_AVAILABLE;
    }
    *provenance_out = result->provenance;
    return APTA_STATUS_OK;
}
