// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_RESULT_BUILDER_INTERNAL_H
#define APTA_RESULT_BUILDER_INTERNAL_H

#include "apta_internal.h"

typedef struct {
    apta_grid_view_t view;
    apta_frame_range_t *coverage;
    apta_grid_segment_t *segments;
    apta_beat_t *beats;
} apta_builder_grid_t;

struct apta_result_builder {
    apta_context_t *context;
    apta_result_builder_options_t options;
    uint64_t payload_bytes;

    uint32_t has_source;
    uint32_t has_provenance;
    apta_result_builder_info_t info;
    apta_source_info_t source;
    apta_internal_metadata_t metadata;
    apta_result_provenance_t provenance;
    uint8_t *provenance_storage;
    size_t provenance_storage_size;

    apta_waveform_overview_view_t overview;
    apta_waveform_span_t *overview_spans;
    apta_waveform_column_t *overview_columns;
    uint32_t overview_column_count;

    uint32_t detail_tile_count;
    apta_waveform_tile_view_t *detail_tiles;
    apta_waveform_column_t *detail_columns;
    uint32_t detail_column_count;

    apta_tempo_view_t tempo;
    apta_tempo_candidate_t *tempo_candidates;
    apta_builder_grid_t local_grid;
    apta_builder_grid_t global_grid;
    apta_grid_revision_view_t global_revision;
    uint32_t has_global_revision;

    apta_key_view_t key;
    apta_key_candidate_t *key_candidates;
    apta_meter_view_t meter;
    apta_meter_segment_t *meter_segments;
    uint32_t quality_count;
    apta_quality_view_t *quality;
};

int apta_builder_bytes_zero(const void *data, size_t size);
int apta_builder_confidence_valid(apta_confidence_value_t value);
int apta_builder_state_valid(apta_feature_state_t state);
apta_status_t apta_builder_validate_range(
    const apta_frame_range_t *range,
    const apta_source_info_t *source);
apta_status_t apta_builder_validate_overview(
    const apta_result_builder_t *builder,
    const apta_waveform_overview_view_t *view,
    uint32_t *column_count_out);
apta_status_t apta_builder_validate_detail(
    const apta_result_builder_t *builder,
    const apta_waveform_detail_input_t *input,
    uint32_t *column_count_out);
apta_status_t apta_builder_validate_tempo(
    const apta_result_builder_t *builder,
    const apta_tempo_view_t *view);
apta_status_t apta_builder_validate_grid(
    const apta_result_builder_t *builder,
    const apta_grid_view_t *view);
apta_status_t apta_builder_validate_grid_modifiers(
    apta_feature_mask_t grid_feature,
    const apta_grid_view_t *view);
apta_status_t apta_builder_validate_grid_revision(
    const apta_result_builder_t *builder,
    const apta_grid_revision_view_t *revision);
apta_status_t apta_builder_validate_key(
    const apta_result_builder_t *builder,
    const apta_key_view_t *view);
apta_status_t apta_builder_validate_meter(
    const apta_result_builder_t *builder,
    const apta_meter_view_t *view);
apta_status_t apta_builder_validate_quality(
    const apta_quality_view_t *view);

int apta_builder_can_replace(
    const apta_result_builder_t *builder,
    uint64_t old_bytes,
    uint64_t new_bytes);
void *apta_builder_allocate_copy(
    apta_result_builder_t *builder,
    const void *source,
    uint32_t count,
    size_t element_size,
    size_t alignment);
void apta_builder_clear_grid(
    apta_result_builder_t *builder,
    apta_builder_grid_t *grid);

#endif /* APTA_RESULT_BUILDER_INTERNAL_H */
