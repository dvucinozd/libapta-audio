// SPDX-License-Identifier: Apache-2.0
#include "apta_result_builder_internal.h"
#include "apta_grid_match_internal.h"
#include "../beatgrid/apta_s6_internal.h"

#include <stdalign.h>
#include <string.h>

#if defined(APTA_ENABLE_TEST_HOOKS)
uint64_t apta_test_builder_grid_match_work;
#define APTA_TEST_BUILDER_GRID_WORK_PTR (&apta_test_builder_grid_match_work)
#else
#define APTA_TEST_BUILDER_GRID_WORK_PTR NULL
#endif

static void *apta_builder_result_copy(
    apta_context_t *context,
    const void *source,
    uint32_t count,
    size_t element_size,
    size_t alignment)
{
    void *copy;
    size_t bytes;
    if (count == 0u) return NULL;
    if (source == NULL ||
        !apta_internal_size_array_fits(0u, count, element_size)) {
        return NULL;
    }
    bytes = (size_t)count * element_size;
    copy = apta_internal_context_allocate(
        context, bytes, alignment, APTA_MEMORY_PERSISTENT);
    if (copy != NULL) memcpy(copy, source, bytes);
    return copy;
}

static apta_feature_mask_t apta_builder_features(
    const apta_result_builder_t *builder)
{
    apta_feature_mask_t features = 0u;
    uint32_t index;
    if (builder->overview.span_count != 0u) {
        features |= APTA_FEATURE_WAVEFORM_OVERVIEW;
        for (index = 0u; index < builder->overview_column_count; ++index) {
            if ((builder->overview_columns[index].flags &
                 APTA_WAVEFORM_COLUMN_HAS_3BAND) != 0u) {
                features |= APTA_FEATURE_WAVEFORM_3BAND;
                break;
            }
        }
    }
    if (builder->detail_tile_count != 0u) {
        features |= APTA_FEATURE_WAVEFORM_DETAIL;
    }
    if (builder->tempo.selected.tempo_millibpm != 0u) {
        features |= APTA_FEATURE_BPM | APTA_FEATURE_CONFIDENCE;
    }
    if (builder->local_grid.view.representation !=
        APTA_GRID_REPRESENTATION_NONE) {
        features |= APTA_FEATURE_LOCAL_BEATGRID;
        if ((builder->local_grid.view.flags & APTA_GRID_FLAG_LOCKED) != 0u) {
            features |= APTA_FEATURE_GRID_LOCKING;
        }
    }
    if (builder->global_grid.view.representation !=
        APTA_GRID_REPRESENTATION_NONE) {
        features |= APTA_FEATURE_GLOBAL_BEATGRID;
        if ((builder->global_grid.view.flags &
             APTA_GRID_FLAG_DYNAMIC_TEMPO) != 0u) {
            features |= APTA_FEATURE_DYNAMIC_TEMPO;
        }
    }
    if (apta_builder_state_valid(builder->key.state)) {
        features |= APTA_FEATURE_MUSICAL_KEY;
    }
    if (apta_builder_state_valid(builder->meter.state)) {
        features |= APTA_FEATURE_METER_DOWNBEAT;
    }
    if (builder->quality_count != 0u) {
        features |= APTA_FEATURE_CALIBRATED_QUALITY;
    }
    if ((builder->overview.span_count != 0u &&
         builder->overview.confidence != APTA_CONFIDENCE_UNKNOWN) ||
        (builder->detail_tile_count != 0u &&
         builder->detail_tiles[0].confidence != APTA_CONFIDENCE_UNKNOWN) ||
        (builder->local_grid.view.representation !=
             APTA_GRID_REPRESENTATION_NONE &&
         builder->local_grid.view.confidence != APTA_CONFIDENCE_UNKNOWN) ||
        (builder->global_grid.view.representation !=
             APTA_GRID_REPRESENTATION_NONE &&
         builder->global_grid.view.confidence != APTA_CONFIDENCE_UNKNOWN) ||
        (apta_builder_state_valid(builder->key.state) &&
         builder->key.confidence != APTA_CONFIDENCE_UNKNOWN) ||
        (apta_builder_state_valid(builder->meter.state) &&
         builder->meter.confidence != APTA_CONFIDENCE_UNKNOWN)) {
        features |= APTA_FEATURE_CONFIDENCE;
    }
    return features;
}

static int apta_builder_all_features_final(
    const apta_result_builder_t *builder)
{
    uint32_t index;
    if (builder->overview.span_count != 0u &&
        builder->overview.state != APTA_FEATURE_FINAL) return 0;
    for (index = 0u; index < builder->detail_tile_count; ++index) {
        if (builder->detail_tiles[index].state != APTA_FEATURE_FINAL) return 0;
    }
    if (builder->tempo.selected.tempo_millibpm != 0u &&
        builder->tempo.selected.state != APTA_FEATURE_FINAL) return 0;
    if (builder->local_grid.view.representation !=
            APTA_GRID_REPRESENTATION_NONE &&
        builder->local_grid.view.state != APTA_FEATURE_FINAL) return 0;
    if (builder->global_grid.view.representation !=
            APTA_GRID_REPRESENTATION_NONE &&
        (builder->global_grid.view.state != APTA_FEATURE_FINAL ||
         builder->global_revision.state != APTA_GRID_REVISION_APPLIED)) return 0;
    if (apta_builder_state_valid(builder->key.state) &&
        builder->key.state != APTA_FEATURE_FINAL) return 0;
    if (apta_builder_state_valid(builder->meter.state) &&
        builder->meter.state != APTA_FEATURE_FINAL) return 0;
    for (index = 0u; index < builder->meter.segment_count; ++index) {
        if (builder->meter.segments[index].state != APTA_FEATURE_FINAL) {
            return 0;
        }
    }
    for (index = 0u; index < builder->quality_count; ++index) {
        if (builder->quality[index].state != APTA_FEATURE_FINAL) return 0;
    }
    return 1;
}

static int apta_builder_grid_matches_tempo(
    const apta_result_builder_t *builder,
    const apta_grid_view_t *grid)
{
    uint32_t index;
    int selected_found = 0;
    const int dynamic =
        (grid->flags & APTA_GRID_FLAG_DYNAMIC_TEMPO) != 0u;
    if (grid->representation == APTA_GRID_REPRESENTATION_EXPLICIT) {
        if (grid->beat_count == 1u) {
            /* One beat establishes phase only; there is no period to compare. */
            return 1;
        }
        for (index = 1u; index < grid->beat_count; ++index) {
            const apta_beat_t *previous = &grid->beats[index - 1u];
            const apta_beat_t *current = &grid->beats[index];
            const uint64_t ordinal_delta =
                (uint64_t)current->ordinal - (uint64_t)previous->ordinal;
            long double frame_delta =
                (long double)(current->position.whole_frame -
                              previous->position.whole_frame) +
                ((long double)current->position.fraction_q32 -
                 (long double)previous->position.fraction_q32) /
                    4294967296.0L;
            long double derived =
                ((long double)builder->source.sample_rate * 60000.0L *
                 (long double)ordinal_delta) / frame_delta;
            long double difference =
                derived -
                (long double)builder->tempo.selected.tempo_millibpm;
            if (difference < 0.0L) difference = -difference;
            if (difference > 1.0L) return 0;
        }
        return 1;
    }
    for (index = 0u; index < grid->segment_count; ++index) {
        const apta_grid_segment_t *segment = &grid->segments[index];
        long double period = (long double)segment->frames_per_beat.whole_frames +
            (long double)segment->frames_per_beat.fraction_q32 /
                4294967296.0L;
        long double derived =
            ((long double)builder->source.sample_rate * 60000.0L) / period;
        long double difference =
            derived - (long double)segment->nominal_tempo_millibpm;
        if (difference < 0.0L) difference = -difference;
        if (difference > 1.0L) return 0;
        if (segment->nominal_tempo_millibpm ==
            builder->tempo.selected.tempo_millibpm) {
            selected_found = 1;
        } else if (!dynamic) {
            return 0;
        }
    }
    return selected_found;
}

static apta_status_t apta_builder_validate_complete(
    const apta_result_builder_t *builder,
    apta_feature_mask_t *features_out)
{
    apta_feature_mask_t features;
    uint64_t finalized_bytes;
    apta_waveform_detail_input_t detail;
    uint32_t ignored_count;
    uint32_t index;
    apta_status_t status;
    if (builder == NULL || features_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (builder->has_source == 0u || builder->has_provenance == 0u) {
        return APTA_ERROR_CONFLICT;
    }
    features = apta_builder_features(builder);
    if (features == 0u) return APTA_ERROR_CONFLICT;
    if ((features & (APTA_FEATURE_LOCAL_BEATGRID |
                     APTA_FEATURE_GLOBAL_BEATGRID)) != 0u &&
        (features & APTA_FEATURE_BPM) == 0u) {
        return APTA_ERROR_CONFLICT;
    }
    finalized_bytes = builder->payload_bytes;
    if (finalized_bytes > UINT64_MAX - sizeof(apta_result_t)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    finalized_bytes += sizeof(apta_result_t);
    if ((features & APTA_FEATURE_GLOBAL_BEATGRID) != 0u) {
        if (finalized_bytes > UINT64_MAX -
                                  sizeof(apta_internal_s6_result_state_t)) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        finalized_bytes += sizeof(apta_internal_s6_result_state_t);
    }
    if (finalized_bytes > builder->options.maximum_allocation_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (builder->info.session_state == APTA_SESSION_CREATED ||
        (builder->info.session_state == APTA_SESSION_COMPLETED &&
         !apta_builder_all_features_final(builder))) {
        return APTA_ERROR_CONFLICT;
    }
    if ((features & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
        status = apta_builder_validate_overview(
            builder, &builder->overview, &ignored_count);
        if (status < 0) return status;
    }
    if ((features & APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
        apta_waveform_detail_input_init(&detail);
        detail.tile_count = builder->detail_tile_count;
        detail.tiles = builder->detail_tiles;
        status = apta_builder_validate_detail(builder, &detail, &ignored_count);
        if (status < 0) return status;
    }
    if ((features & APTA_FEATURE_BPM) != 0u) {
        status = apta_builder_validate_tempo(builder, &builder->tempo);
        if (status < 0) return status;
    }
    if ((features & APTA_FEATURE_LOCAL_BEATGRID) != 0u) {
        status = apta_builder_validate_grid(
            builder, &builder->local_grid.view);
        if (status < 0) return status;
        status = apta_builder_validate_grid_modifiers(
            APTA_FEATURE_LOCAL_BEATGRID, &builder->local_grid.view);
        if (status < 0) return status;
        if (!apta_builder_grid_matches_tempo(
                builder, &builder->local_grid.view)) {
            return APTA_ERROR_CONFLICT;
        }
    }
    if ((features & APTA_FEATURE_GLOBAL_BEATGRID) != 0u) {
        status = apta_builder_validate_grid(
            builder, &builder->global_grid.view);
        if (status < 0) return status;
        status = apta_builder_validate_grid_modifiers(
            APTA_FEATURE_GLOBAL_BEATGRID, &builder->global_grid.view);
        if (status < 0) return status;
        if (builder->has_global_revision == 0u) {
            return APTA_ERROR_CONFLICT;
        }
        status = apta_builder_validate_grid_revision(
            builder, &builder->global_revision);
        if (status < 0) return status;
        if (!apta_builder_grid_matches_tempo(
                builder, &builder->global_grid.view)) {
            return APTA_ERROR_CONFLICT;
        }
    }
    if ((features & APTA_FEATURE_MUSICAL_KEY) != 0u) {
        status = apta_builder_validate_key(builder, &builder->key);
        if (status < 0) return status;
    }
    if ((features & APTA_FEATURE_METER_DOWNBEAT) != 0u) {
        status = apta_builder_validate_meter(builder, &builder->meter);
        if (status < 0) return status;
        if ((features & (APTA_FEATURE_LOCAL_BEATGRID |
                         APTA_FEATURE_GLOBAL_BEATGRID)) != 0u) {
            apta_internal_grid_match_set_t grids;
            apta_internal_grid_match_set_init(
                &grids,
                (features & APTA_FEATURE_LOCAL_BEATGRID) != 0u
                    ? &builder->local_grid.view : NULL,
                (features & APTA_FEATURE_GLOBAL_BEATGRID) != 0u
                    ? &builder->global_grid.view : NULL,
                APTA_TEST_BUILDER_GRID_WORK_PTR);
            for (index = 0u; index < builder->meter.segment_count; ++index) {
                const apta_meter_segment_t *segment =
                    &builder->meter.segments[index];
                if (!apta_internal_grid_match_set_next(
                        &grids, segment->downbeat_frame,
                        segment->downbeat_ordinal)) {
                    return APTA_ERROR_CONFLICT;
                }
            }
        }
    }
    for (index = 0u; index < builder->quality_count; ++index) {
        status = apta_builder_validate_quality(&builder->quality[index]);
        if (status < 0) return status;
        if ((features & builder->quality[index].feature) == 0u) {
            return APTA_ERROR_CONFLICT;
        }
    }
    *features_out = features;
    return APTA_STATUS_OK;
}

static void apta_builder_cleanup_unpublished(apta_result_t *result)
{
    if (result == NULL) return;
    apta_internal_metadata_cleanup(result->context, &result->metadata);
    apta_internal_context_deallocate(
        result->context, result->provenance_storage);
    apta_internal_context_deallocate(result->context, result->key_candidates);
    apta_internal_context_deallocate(result->context, result->meter_segments);
    apta_internal_waveform_cleanup_result(result);
    apta_internal_context_deallocate(result->context, result);
}

static apta_status_t apta_builder_copy_provenance(
    const apta_result_builder_t *builder,
    apta_result_t *result)
{
    result->provenance = builder->provenance;
    result->provenance.source_name.data = NULL;
    result->provenance.source_version.data = NULL;
    if (builder->provenance_storage_size == 0u) return APTA_STATUS_OK;
    result->provenance_storage = (uint8_t *)apta_internal_context_allocate(
        builder->context, builder->provenance_storage_size, alignof(uint8_t),
        APTA_MEMORY_PERSISTENT);
    if (result->provenance_storage == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    memcpy(result->provenance_storage, builder->provenance_storage,
           builder->provenance_storage_size);
    result->provenance_storage_size = builder->provenance_storage_size;
    result->provenance.source_name.data =
        (const char *)result->provenance_storage;
    if (result->provenance.source_version.size != 0u) {
        result->provenance.source_version.data =
            (const char *)(result->provenance_storage +
                           result->provenance.source_name.size);
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_builder_copy_overview(
    const apta_result_builder_t *builder,
    apta_result_t *result)
{
    uint32_t index;
    uint32_t offset = 0u;
    if (builder->overview.span_count == 0u) return APTA_STATUS_OK;
    result->overview_spans = (apta_waveform_span_t *)apta_builder_result_copy(
        builder->context, builder->overview_spans,
        builder->overview.span_count, sizeof(apta_waveform_span_t),
        alignof(apta_waveform_span_t));
    result->overview_columns =
        (apta_waveform_column_t *)apta_builder_result_copy(
            builder->context, builder->overview_columns,
            builder->overview_column_count, sizeof(apta_waveform_column_t),
            alignof(apta_waveform_column_t));
    if (result->overview_spans == NULL || result->overview_columns == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    result->overview = builder->overview;
    result->overview.spans = result->overview_spans;
    for (index = 0u; index < result->overview.span_count; ++index) {
        result->overview_spans[index].columns =
            result->overview_columns + offset;
        offset += result->overview_spans[index].column_count;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_builder_copy_detail(
    const apta_result_builder_t *builder,
    apta_result_t *result)
{
    uint32_t index;
    uint32_t offset = 0u;
    if (builder->detail_tile_count == 0u) return APTA_STATUS_OK;
    result->detail_tiles =
        (apta_waveform_tile_view_t *)apta_builder_result_copy(
            builder->context, builder->detail_tiles,
            builder->detail_tile_count, sizeof(apta_waveform_tile_view_t),
            alignof(apta_waveform_tile_view_t));
    result->detail_columns =
        (apta_waveform_column_t *)apta_builder_result_copy(
            builder->context, builder->detail_columns,
            builder->detail_column_count, sizeof(apta_waveform_column_t),
            alignof(apta_waveform_column_t));
    if (result->detail_tiles == NULL || result->detail_columns == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    result->detail_tile_count = builder->detail_tile_count;
    for (index = 0u; index < result->detail_tile_count; ++index) {
        result->detail_tiles[index].columns = result->detail_columns + offset;
        offset += result->detail_tiles[index].column_count;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_builder_copy_tempo(
    const apta_result_builder_t *builder,
    apta_result_t *result)
{
    if (builder->tempo.selected.tempo_millibpm == 0u) return APTA_STATUS_OK;
    if (builder->tempo.candidate_count != 0u) {
        result->tempo_candidates =
            (apta_tempo_candidate_t *)apta_builder_result_copy(
                builder->context, builder->tempo_candidates,
                builder->tempo.candidate_count,
                sizeof(apta_tempo_candidate_t),
                alignof(apta_tempo_candidate_t));
        if (result->tempo_candidates == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    }
    result->tempo = builder->tempo;
    result->tempo.candidates = result->tempo_candidates;
    return APTA_STATUS_OK;
}

static apta_status_t apta_builder_copy_local_grid(
    const apta_result_builder_t *builder,
    apta_result_t *result)
{
    const apta_grid_view_t *source = &builder->local_grid.view;
    if (source->representation == APTA_GRID_REPRESENTATION_NONE) {
        return APTA_STATUS_OK;
    }
    result->local_grid_coverage =
        (apta_frame_range_t *)apta_builder_result_copy(
            builder->context, builder->local_grid.coverage,
            source->coverage_range_count, sizeof(apta_frame_range_t),
            alignof(apta_frame_range_t));
    result->local_grid_segments =
        (apta_grid_segment_t *)apta_builder_result_copy(
            builder->context, builder->local_grid.segments,
            source->segment_count, sizeof(apta_grid_segment_t),
            alignof(apta_grid_segment_t));
    result->local_grid_beats = (apta_beat_t *)apta_builder_result_copy(
        builder->context, builder->local_grid.beats, source->beat_count,
        sizeof(apta_beat_t), alignof(apta_beat_t));
    if ((source->coverage_range_count != 0u &&
         result->local_grid_coverage == NULL) ||
        (source->segment_count != 0u &&
         result->local_grid_segments == NULL) ||
        (source->beat_count != 0u && result->local_grid_beats == NULL)) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    result->local_grid = *source;
    result->local_grid.coverage_ranges = result->local_grid_coverage;
    result->local_grid.segments = result->local_grid_segments;
    result->local_grid.beats = result->local_grid_beats;
    return APTA_STATUS_OK;
}

static apta_status_t apta_builder_copy_global_grid(
    const apta_result_builder_t *builder,
    apta_result_t *result)
{
    const apta_grid_view_t *source = &builder->global_grid.view;
    if (source->representation == APTA_GRID_REPRESENTATION_NONE) {
        return APTA_STATUS_OK;
    }
    result->s6 = (apta_internal_s6_result_state_t *)
        apta_internal_context_allocate(
            builder->context, sizeof(*result->s6),
            alignof(apta_internal_s6_result_state_t), APTA_MEMORY_PERSISTENT);
    if (result->s6 == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    memset(result->s6, 0, sizeof(*result->s6));
    result->s6->revision = builder->global_revision;
    result->s6->coverage_ranges =
        (apta_frame_range_t *)apta_builder_result_copy(
            builder->context, builder->global_grid.coverage,
            source->coverage_range_count, sizeof(apta_frame_range_t),
            alignof(apta_frame_range_t));
    result->s6->segments =
        (apta_grid_segment_t *)apta_builder_result_copy(
            builder->context, builder->global_grid.segments,
            source->segment_count, sizeof(apta_grid_segment_t),
            alignof(apta_grid_segment_t));
    result->s6->beats = (apta_beat_t *)apta_builder_result_copy(
        builder->context, builder->global_grid.beats, source->beat_count,
        sizeof(apta_beat_t), alignof(apta_beat_t));
    if ((source->coverage_range_count != 0u &&
         result->s6->coverage_ranges == NULL) ||
        (source->segment_count != 0u && result->s6->segments == NULL) ||
        (source->beat_count != 0u && result->s6->beats == NULL)) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    result->s6->global_grid = *source;
    result->s6->global_grid.coverage_ranges = result->s6->coverage_ranges;
    result->s6->global_grid.segments = result->s6->segments;
    result->s6->global_grid.beats = result->s6->beats;
    return APTA_STATUS_OK;
}

static apta_status_t apta_builder_copy_new_features(
    const apta_result_builder_t *builder,
    apta_result_t *result)
{
    if (apta_builder_state_valid(builder->key.state)) {
        if (builder->key.candidate_count != 0u) {
            result->key_candidates =
                (apta_key_candidate_t *)apta_builder_result_copy(
                    builder->context, builder->key_candidates,
                    builder->key.candidate_count,
                    sizeof(apta_key_candidate_t),
                    alignof(apta_key_candidate_t));
            if (result->key_candidates == NULL) {
                return APTA_ERROR_OUT_OF_MEMORY;
            }
        }
        result->key = builder->key;
        result->key.candidates = result->key_candidates;
    }
    if (builder->meter.segment_count != 0u) {
        result->meter_segments =
            (apta_meter_segment_t *)apta_builder_result_copy(
                builder->context, builder->meter_segments,
                builder->meter.segment_count, sizeof(apta_meter_segment_t),
                alignof(apta_meter_segment_t));
        if (result->meter_segments == NULL) return APTA_ERROR_OUT_OF_MEMORY;
        result->meter = builder->meter;
        result->meter.segments = result->meter_segments;
    }
    if (builder->quality_count != 0u) {
        result->quality = (apta_quality_view_t *)apta_builder_result_copy(
            builder->context, builder->quality, builder->quality_count,
            sizeof(apta_quality_view_t), alignof(apta_quality_view_t));
        if (result->quality == NULL) return APTA_ERROR_OUT_OF_MEMORY;
        result->quality_count = builder->quality_count;
    }
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_builder_finalize(
    const apta_result_builder_t *builder,
    const apta_result_t **result_out)
{
    apta_result_t *result = NULL;
    apta_feature_mask_t features;
    apta_status_t status;
    if (result_out == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    *result_out = NULL;
    status = apta_builder_validate_complete(builder, &features);
    if (status < 0) return status;
    result = (apta_result_t *)apta_internal_context_allocate(
        builder->context, sizeof(*result), alignof(apta_result_t),
        APTA_MEMORY_PERSISTENT);
    if (result == NULL) return APTA_ERROR_OUT_OF_MEMORY;
    memset(result, 0, sizeof(*result));
    result->context = builder->context;
    atomic_init(&result->reference_count, 1u);
    apta_internal_result_init_absent_views(result);
    result->source_info = builder->source;
    result->total_source_frames = builder->source.total_frames;
    result->source_sample_rate = builder->source.sample_rate;
    result->source_channel_count = builder->source.channel_count;
    result->source_channel_layout = builder->source.channel_layout;
    apta_result_info_init(&result->info);
    result->info.specification_major = APTA_SPEC_VERSION_MAJOR;
    result->info.specification_minor = APTA_SPEC_VERSION_MINOR;
    result->info.producer_api_version = APTA_API_VERSION;
    result->info.container_version = builder->info.container_version;
    result->info.generation = builder->info.generation;
    result->info.available_features = features;
    result->info.changed_features = features;
    result->info.session_state = builder->info.session_state;
    result->info.lineage_id_high = builder->info.lineage_id_high;
    result->info.lineage_id_low = builder->info.lineage_id_low;

    if (apta_internal_metadata_is_present(&builder->metadata)) {
        status = apta_internal_metadata_copy_from_view(
            builder->context, &builder->metadata.view, &result->metadata);
        if (status < 0) goto failure;
    }
    status = apta_builder_copy_provenance(builder, result);
    if (status < 0) goto failure;
    status = apta_builder_copy_overview(builder, result);
    if (status < 0) goto failure;
    status = apta_builder_copy_detail(builder, result);
    if (status < 0) goto failure;
    status = apta_builder_copy_tempo(builder, result);
    if (status < 0) goto failure;
    status = apta_builder_copy_local_grid(builder, result);
    if (status < 0) goto failure;
    status = apta_builder_copy_global_grid(builder, result);
    if (status < 0) goto failure;
    status = apta_builder_copy_new_features(builder, result);
    if (status < 0) goto failure;

    (void)atomic_fetch_add_explicit(
        &builder->context->result_count, 1u, memory_order_acq_rel);
    *result_out = result;
    return APTA_STATUS_OK;

failure:
    apta_builder_cleanup_unpublished(result);
    return status;
}
