// SPDX-License-Identifier: Apache-2.0
#include "apta_result_builder_internal.h"

#include <stdalign.h>
#include <string.h>

static uint64_t apta_builder_array_bytes(uint32_t count, size_t element_size)
{
    return (uint64_t)count * (uint64_t)element_size;
}

apta_status_t APTA_CALL apta_result_builder_set_waveform_overview(
    apta_result_builder_t *builder,
    const apta_waveform_overview_view_t *overview)
{
    apta_waveform_span_t *spans;
    apta_waveform_column_t *columns;
    uint32_t column_count;
    uint32_t span_index;
    uint32_t column_offset = 0u;
    uint64_t old_bytes;
    uint64_t new_bytes;
    apta_status_t status;
    if (builder == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    status = apta_builder_validate_overview(
        builder, overview, &column_count);
    if (status < 0) return status;
    old_bytes = apta_builder_array_bytes(
                    builder->overview.span_count,
                    sizeof(apta_waveform_span_t)) +
                apta_builder_array_bytes(
                    builder->overview_column_count,
                    sizeof(apta_waveform_column_t));
    new_bytes = apta_builder_array_bytes(
                    overview->span_count, sizeof(apta_waveform_span_t)) +
                apta_builder_array_bytes(
                    column_count, sizeof(apta_waveform_column_t));
    if (!apta_builder_can_replace(builder, old_bytes, new_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    spans = (apta_waveform_span_t *)apta_builder_allocate_copy(
        builder, overview->spans, overview->span_count,
        sizeof(*spans), alignof(apta_waveform_span_t));
    if (spans == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    columns = (apta_waveform_column_t *)apta_internal_context_allocate(
        builder->context,
        (size_t)column_count * sizeof(*columns),
        alignof(apta_waveform_column_t), APTA_MEMORY_PERSISTENT);
    if (columns == NULL) {
        apta_internal_context_deallocate(builder->context, spans);
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    for (span_index = 0u; span_index < overview->span_count; ++span_index) {
        uint32_t count = overview->spans[span_index].column_count;
        memcpy(columns + column_offset, overview->spans[span_index].columns,
               (size_t)count * sizeof(*columns));
        spans[span_index].columns = columns + column_offset;
        column_offset += count;
    }
    apta_internal_context_deallocate(builder->context, builder->overview_spans);
    apta_internal_context_deallocate(builder->context, builder->overview_columns);
    builder->overview = *overview;
    builder->overview_spans = spans;
    builder->overview_columns = columns;
    builder->overview_column_count = column_count;
    builder->overview.spans = spans;
    builder->payload_bytes = builder->payload_bytes - old_bytes + new_bytes;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_set_waveform_detail(
    apta_result_builder_t *builder,
    const apta_waveform_detail_input_t *detail)
{
    apta_waveform_tile_view_t *tiles;
    apta_waveform_column_t *columns;
    uint32_t column_count;
    uint32_t tile_index;
    uint32_t column_offset = 0u;
    uint64_t old_bytes;
    uint64_t new_bytes;
    apta_status_t status;
    if (builder == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    status = apta_builder_validate_detail(builder, detail, &column_count);
    if (status < 0) return status;
    old_bytes = apta_builder_array_bytes(
                    builder->detail_tile_count,
                    sizeof(apta_waveform_tile_view_t)) +
                apta_builder_array_bytes(
                    builder->detail_column_count,
                    sizeof(apta_waveform_column_t));
    new_bytes = apta_builder_array_bytes(
                    detail->tile_count, sizeof(apta_waveform_tile_view_t)) +
                apta_builder_array_bytes(
                    column_count, sizeof(apta_waveform_column_t));
    if (!apta_builder_can_replace(builder, old_bytes, new_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    tiles = (apta_waveform_tile_view_t *)apta_builder_allocate_copy(
        builder, detail->tiles, detail->tile_count,
        sizeof(*tiles), alignof(apta_waveform_tile_view_t));
    if (tiles == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    columns = (apta_waveform_column_t *)apta_internal_context_allocate(
        builder->context, (size_t)column_count * sizeof(*columns),
        alignof(apta_waveform_column_t), APTA_MEMORY_PERSISTENT);
    if (columns == NULL) {
        apta_internal_context_deallocate(builder->context, tiles);
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    for (tile_index = 0u; tile_index < detail->tile_count; ++tile_index) {
        uint32_t count = detail->tiles[tile_index].column_count;
        memcpy(columns + column_offset, detail->tiles[tile_index].columns,
               (size_t)count * sizeof(*columns));
        tiles[tile_index].columns = columns + column_offset;
        column_offset += count;
    }
    apta_internal_context_deallocate(builder->context, builder->detail_tiles);
    apta_internal_context_deallocate(builder->context, builder->detail_columns);
    builder->detail_tile_count = detail->tile_count;
    builder->detail_tiles = tiles;
    builder->detail_columns = columns;
    builder->detail_column_count = column_count;
    builder->payload_bytes = builder->payload_bytes - old_bytes + new_bytes;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_set_tempo(
    apta_result_builder_t *builder,
    const apta_tempo_view_t *tempo)
{
    apta_tempo_candidate_t *candidates;
    uint64_t old_bytes;
    uint64_t new_bytes;
    apta_status_t status;
    if (builder == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    status = apta_builder_validate_tempo(builder, tempo);
    if (status < 0) return status;
    old_bytes = apta_builder_array_bytes(
        builder->tempo.candidate_count, sizeof(apta_tempo_candidate_t));
    new_bytes = apta_builder_array_bytes(
        tempo->candidate_count, sizeof(apta_tempo_candidate_t));
    if (!apta_builder_can_replace(builder, old_bytes, new_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    candidates = NULL;
    if (tempo->candidate_count != 0u) {
        candidates = (apta_tempo_candidate_t *)apta_builder_allocate_copy(
            builder, tempo->candidates, tempo->candidate_count,
            sizeof(*candidates), alignof(apta_tempo_candidate_t));
        if (candidates == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    }
    apta_internal_context_deallocate(builder->context, builder->tempo_candidates);
    builder->tempo = *tempo;
    builder->tempo_candidates = candidates;
    builder->tempo.candidates = candidates;
    builder->payload_bytes = builder->payload_bytes - old_bytes + new_bytes;
    return APTA_STATUS_OK;
}

static uint64_t apta_builder_grid_bytes(const apta_builder_grid_t *grid)
{
    return apta_builder_array_bytes(
               grid->view.coverage_range_count, sizeof(apta_frame_range_t)) +
           apta_builder_array_bytes(
               grid->view.segment_count, sizeof(apta_grid_segment_t)) +
           apta_builder_array_bytes(grid->view.beat_count, sizeof(apta_beat_t));
}

apta_status_t APTA_CALL apta_result_builder_set_beatgrid(
    apta_result_builder_t *builder,
    apta_feature_mask_t grid_feature,
    const apta_grid_view_t *grid)
{
    apta_builder_grid_t replacement;
    apta_builder_grid_t *destination;
    uint64_t old_bytes;
    uint64_t new_bytes;
    apta_status_t status;
    if (builder == NULL ||
        (grid_feature != APTA_FEATURE_LOCAL_BEATGRID &&
         grid_feature != APTA_FEATURE_GLOBAL_BEATGRID)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    status = apta_builder_validate_grid(builder, grid);
    if (status < 0) return status;
    status = apta_builder_validate_grid_modifiers(grid_feature, grid);
    if (status < 0) return status;
    destination = grid_feature == APTA_FEATURE_LOCAL_BEATGRID
                      ? &builder->local_grid
                      : &builder->global_grid;
    old_bytes = apta_builder_grid_bytes(destination);
    new_bytes = apta_builder_array_bytes(
                    grid->coverage_range_count, sizeof(apta_frame_range_t)) +
                apta_builder_array_bytes(
                    grid->segment_count, sizeof(apta_grid_segment_t)) +
                apta_builder_array_bytes(grid->beat_count, sizeof(apta_beat_t));
    if (!apta_builder_can_replace(builder, old_bytes, new_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    memset(&replacement, 0, sizeof(replacement));
    replacement.view = *grid;
    replacement.coverage = (apta_frame_range_t *)apta_builder_allocate_copy(
        builder, grid->coverage_ranges, grid->coverage_range_count,
        sizeof(apta_frame_range_t), alignof(apta_frame_range_t));
    replacement.segments = (apta_grid_segment_t *)apta_builder_allocate_copy(
        builder, grid->segments, grid->segment_count,
        sizeof(apta_grid_segment_t), alignof(apta_grid_segment_t));
    replacement.beats = (apta_beat_t *)apta_builder_allocate_copy(
        builder, grid->beats, grid->beat_count,
        sizeof(apta_beat_t), alignof(apta_beat_t));
    if ((grid->coverage_range_count != 0u && replacement.coverage == NULL) ||
        (grid->segment_count != 0u && replacement.segments == NULL) ||
        (grid->beat_count != 0u && replacement.beats == NULL)) {
        apta_builder_clear_grid(builder, &replacement);
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    replacement.view.coverage_ranges = replacement.coverage;
    replacement.view.segments = replacement.segments;
    replacement.view.beats = replacement.beats;
    apta_builder_clear_grid(builder, destination);
    *destination = replacement;
    if (grid_feature == APTA_FEATURE_GLOBAL_BEATGRID) {
        apta_grid_revision_view_init(&builder->global_revision);
        builder->has_global_revision = 0u;
    }
    builder->payload_bytes = builder->payload_bytes - old_bytes + new_bytes;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_set_grid_revision(
    apta_result_builder_t *builder,
    const apta_grid_revision_view_t *revision)
{
    apta_status_t status =
        apta_builder_validate_grid_revision(builder, revision);
    if (status < 0) return status;
    builder->global_revision = *revision;
    builder->has_global_revision = 1u;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_set_key(
    apta_result_builder_t *builder,
    const apta_key_view_t *key)
{
    apta_key_candidate_t *candidates;
    uint64_t old_bytes;
    uint64_t new_bytes;
    apta_status_t status;
    if (builder == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    status = apta_builder_validate_key(builder, key);
    if (status < 0) return status;
    old_bytes = apta_builder_array_bytes(
        builder->key.candidate_count, sizeof(apta_key_candidate_t));
    new_bytes = apta_builder_array_bytes(
        key->candidate_count, sizeof(apta_key_candidate_t));
    if (!apta_builder_can_replace(builder, old_bytes, new_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    candidates = NULL;
    if (key->candidate_count != 0u) {
        candidates = (apta_key_candidate_t *)apta_builder_allocate_copy(
            builder, key->candidates, key->candidate_count,
            sizeof(*candidates), alignof(apta_key_candidate_t));
        if (candidates == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    }
    apta_internal_context_deallocate(builder->context, builder->key_candidates);
    builder->key = *key;
    builder->key_candidates = candidates;
    builder->key.candidates = candidates;
    builder->payload_bytes = builder->payload_bytes - old_bytes + new_bytes;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_set_meter(
    apta_result_builder_t *builder,
    const apta_meter_view_t *meter)
{
    apta_meter_segment_t *segments;
    uint64_t old_bytes;
    uint64_t new_bytes;
    apta_status_t status;
    if (builder == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    status = apta_builder_validate_meter(builder, meter);
    if (status < 0) return status;
    old_bytes = apta_builder_array_bytes(
        builder->meter.segment_count, sizeof(apta_meter_segment_t));
    new_bytes = apta_builder_array_bytes(
        meter->segment_count, sizeof(apta_meter_segment_t));
    if (!apta_builder_can_replace(builder, old_bytes, new_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    segments = (apta_meter_segment_t *)apta_builder_allocate_copy(
        builder, meter->segments, meter->segment_count,
        sizeof(*segments), alignof(apta_meter_segment_t));
    if (segments == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    apta_internal_context_deallocate(builder->context, builder->meter_segments);
    builder->meter = *meter;
    builder->meter_segments = segments;
    builder->meter.segments = segments;
    builder->payload_bytes = builder->payload_bytes - old_bytes + new_bytes;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_set_quality(
    apta_result_builder_t *builder,
    const apta_quality_view_t *quality)
{
    apta_quality_view_t *records;
    uint32_t index;
    uint32_t new_count;
    uint64_t old_bytes;
    uint64_t new_bytes;
    apta_status_t status;
    if (builder == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    status = apta_builder_validate_quality(quality);
    if (status < 0) return status;
    for (index = 0u; index < builder->quality_count; ++index) {
        if (builder->quality[index].feature == quality->feature) {
            return APTA_ERROR_CONFLICT;
        }
    }
    if (builder->quality_count == UINT32_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    new_count = builder->quality_count + 1u;
    if (new_count > builder->options.maximum_quality_records) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    old_bytes = apta_builder_array_bytes(
        builder->quality_count, sizeof(apta_quality_view_t));
    new_bytes = apta_builder_array_bytes(
        new_count, sizeof(apta_quality_view_t));
    if (!apta_builder_can_replace(builder, old_bytes, new_bytes)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    records = (apta_quality_view_t *)apta_internal_context_allocate(
        builder->context, (size_t)new_count * sizeof(*records),
        alignof(apta_quality_view_t), APTA_MEMORY_PERSISTENT);
    if (records == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    if (builder->quality_count != 0u) {
        memcpy(records, builder->quality,
               (size_t)builder->quality_count * sizeof(*records));
    }
    records[builder->quality_count] = *quality;
    apta_internal_context_deallocate(builder->context, builder->quality);
    builder->quality = records;
    builder->quality_count = new_count;
    builder->payload_bytes = builder->payload_bytes - old_bytes + new_bytes;
    return APTA_STATUS_OK;
}
