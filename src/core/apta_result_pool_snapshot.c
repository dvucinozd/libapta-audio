// SPDX-License-Identifier: Apache-2.0
#include "apta_result_pool.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define APTA_DETAIL_RUN_VALID(tile) ((tile)->reserved8[0])
#define APTA_DETAIL_RUN_FIRST(tile) ((tile)->reserved8[1])
#define APTA_DETAIL_RUN_COUNT(tile) ((tile)->reserved8[2])

static apta_source_frame_t apta_pool_min_frame(
    apta_source_frame_t left,
    apta_source_frame_t right)
{
    return left < right ? left : right;
}

static double apta_pool_round_ties_even(double value)
{
    double lower = floor(value);
    double fraction = value - lower;

    if (fraction < 0.5) {
        return lower;
    }
    if (fraction > 0.5) {
        return lower + 1.0;
    }
    return fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0;
}

static int16_t apta_pool_quantize_peak(float value)
{
    double rounded;

    if (value <= -1.0f) {
        return INT16_MIN;
    }
    if (value >= 1.0f) {
        return INT16_MAX;
    }

    rounded = apta_pool_round_ties_even((double)value * 32767.0);
    if (rounded < (double)INT16_MIN) {
        rounded = (double)INT16_MIN;
    }
    if (rounded > (double)INT16_MAX) {
        rounded = (double)INT16_MAX;
    }
    return (int16_t)rounded;
}

static uint16_t apta_pool_quantize_rms(
    double sum_squares,
    uint32_t sample_count)
{
    double rms;
    double rounded;

    if (sample_count == 0u) {
        return 0u;
    }

    rms = sqrt(sum_squares / (double)sample_count);
    if (rms < 0.0) {
        rms = 0.0;
    }
    if (rms > 1.0) {
        rms = 1.0;
    }

    rounded = apta_pool_round_ties_even(rms * 65535.0);
    if (rounded < 0.0) {
        rounded = 0.0;
    }
    if (rounded > 65535.0) {
        rounded = 65535.0;
    }
    return (uint16_t)rounded;
}

static uint8_t *apta_pool_result_slot_storage(
    apta_internal_result_pool_control_t *pool,
    const apta_result_t *result)
{
    if (pool == NULL || result == NULL || result->result_pool != pool) {
        return NULL;
    }
    return (uint8_t *)apta_internal_result_pool_get_slot_storage(
        pool,
        result->result_pool_slot_index);
}

static int apta_pool_region_is_valid(
    const apta_internal_result_pool_control_t *pool,
    size_t offset,
    size_t bytes)
{
    return pool != NULL &&
           offset <= pool->layout.slot_bytes &&
           bytes <= pool->layout.slot_bytes - offset;
}

static void apta_pool_metadata_copy_text(
    apta_utf8_view_t *destination,
    const apta_utf8_view_t *source,
    uint8_t *storage,
    size_t *offset)
{
    destination->size = source->size;
    if (source->size == 0u) {
        destination->data = NULL;
        return;
    }

    memcpy(storage + *offset, source->data, source->size);
    destination->data = (const char *)(storage + *offset);
    *offset += source->size;
}

static void apta_pool_metadata_copy_bytes(
    apta_bytes_view_t *destination,
    const apta_bytes_view_t *source,
    uint8_t *storage,
    size_t *offset)
{
    destination->size = source->size;
    if (source->size == 0u) {
        destination->data = NULL;
        return;
    }

    memcpy(storage + *offset, source->data, source->size);
    destination->data = storage + *offset;
    *offset += source->size;
}

static apta_status_t apta_pool_build_metadata(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result)
{
    uint8_t *slot_storage;
    uint8_t *metadata_storage;
    size_t offset = 0u;

    if (!apta_internal_metadata_is_present(&session->metadata)) {
        return APTA_STATUS_OK;
    }
    if (session->metadata.storage_size > pool->layout.metadata_capacity) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (!apta_pool_region_is_valid(
            pool,
            pool->layout.metadata_offset,
            pool->layout.metadata_capacity)) {
        return APTA_ERROR_INTERNAL;
    }

    slot_storage = apta_pool_result_slot_storage(pool, result);
    if (slot_storage == NULL) {
        return APTA_ERROR_INTERNAL;
    }
    metadata_storage = slot_storage + pool->layout.metadata_offset;

    result->metadata.view = session->metadata.view;
    result->metadata.view.struct_size =
        (uint32_t)sizeof(result->metadata.view);
    result->metadata.view.api_version = APTA_API_VERSION;
    result->metadata.view.producer_name.data = NULL;
    result->metadata.view.producer_version_string.data = NULL;
    result->metadata.view.backend_name.data = NULL;
    result->metadata.view.backend_version.data = NULL;
    result->metadata.view.application_source_id.data = NULL;
    result->metadata.view.comments.data = NULL;
    result->metadata.storage =
        session->metadata.storage_size != 0u ? metadata_storage : NULL;
    result->metadata.storage_size = session->metadata.storage_size;
    result->metadata.present = 1u;

    apta_pool_metadata_copy_text(
        &result->metadata.view.producer_name,
        &session->metadata.view.producer_name,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_text(
        &result->metadata.view.producer_version_string,
        &session->metadata.view.producer_version_string,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_text(
        &result->metadata.view.backend_name,
        &session->metadata.view.backend_name,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_text(
        &result->metadata.view.backend_version,
        &session->metadata.view.backend_version,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_bytes(
        &result->metadata.view.application_source_id,
        &session->metadata.view.application_source_id,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_text(
        &result->metadata.view.comments,
        &session->metadata.view.comments,
        metadata_storage,
        &offset);

    return offset == session->metadata.storage_size
               ? APTA_STATUS_OK
               : APTA_ERROR_INTERNAL;
}

static apta_status_t apta_pool_build_overview(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result)
{
    uint8_t *slot_storage;
    uint32_t complete_count;
    uint32_t span_count = 0u;
    uint32_t index;
    uint32_t output_index = 0u;
    uint32_t span_index = 0u;
    uint32_t previous_column = 0u;
    uint32_t span_output_start = 0u;
    int have_previous = 0;
    int full_coverage;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u ||
        session->overview_complete_count == 0u) {
        return APTA_STATUS_OK;
    }

    complete_count = session->overview_complete_count;
    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        const apta_internal_waveform_accumulator_t *accumulator =
            &session->overview_accumulators[index];

        if (!accumulator->complete) {
            continue;
        }
        if (!have_previous ||
            accumulator->column_index != previous_column + 1u) {
            span_count += 1u;
        }
        previous_column = accumulator->column_index;
        have_previous = 1;
    }

    if (complete_count > pool->layout.overview_column_capacity ||
        span_count > pool->layout.overview_span_capacity) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (!apta_pool_region_is_valid(
            pool,
            pool->layout.overview_spans_offset,
            (size_t)pool->layout.overview_span_capacity *
                sizeof(apta_waveform_span_t)) ||
        !apta_pool_region_is_valid(
            pool,
            pool->layout.overview_columns_offset,
            (size_t)pool->layout.overview_column_capacity *
                sizeof(apta_waveform_column_t))) {
        return APTA_ERROR_INTERNAL;
    }

    slot_storage = apta_pool_result_slot_storage(pool, result);
    if (slot_storage == NULL) {
        return APTA_ERROR_INTERNAL;
    }
    result->overview_spans = (apta_waveform_span_t *)(void *)(
        slot_storage + pool->layout.overview_spans_offset);
    result->overview_columns = (apta_waveform_column_t *)(void *)(
        slot_storage + pool->layout.overview_columns_offset);

    have_previous = 0;
    previous_column = 0u;
    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        const apta_internal_waveform_accumulator_t *accumulator =
            &session->overview_accumulators[index];
        apta_waveform_column_t *column;

        if (!accumulator->complete) {
            continue;
        }

        if (!have_previous ||
            accumulator->column_index != previous_column + 1u) {
            if (have_previous) {
                apta_waveform_span_t *previous_span =
                    &result->overview_spans[span_index - 1u];
                apta_source_frame_t end_frame =
                    ((apta_source_frame_t)previous_column + 1u) *
                    session->overview_frames_per_column;
                if (session->end_of_input_signalled) {
                    end_frame = apta_pool_min_frame(
                        end_frame,
                        session->final_end_frame);
                }
                previous_span->source_range.end_frame = end_frame;
                previous_span->column_count =
                    output_index - span_output_start;
            }

            span_output_start = output_index;
            result->overview_spans[span_index].source_range.struct_size =
                (uint32_t)sizeof(
                    result->overview_spans[span_index].source_range);
            result->overview_spans[span_index].source_range.api_version =
                APTA_API_VERSION;
            result->overview_spans[span_index].source_range.first_frame =
                (apta_source_frame_t)accumulator->column_index *
                session->overview_frames_per_column;
            result->overview_spans[span_index].first_column_index =
                accumulator->column_index;
            result->overview_spans[span_index].columns =
                &result->overview_columns[output_index];
            span_index += 1u;
        }

        column = &result->overview_columns[output_index];
        column->minimum = apta_pool_quantize_peak(accumulator->minimum);
        column->maximum = apta_pool_quantize_peak(accumulator->maximum);
        column->rms = apta_pool_quantize_rms(
            accumulator->sum_squares,
            accumulator->sample_count);
        column->flags = APTA_WAVEFORM_COLUMN_VALID;
        if (accumulator->clipped) {
            column->flags |= APTA_WAVEFORM_COLUMN_CLIPPED;
        }

        output_index += 1u;
        previous_column = accumulator->column_index;
        have_previous = 1;
    }

    if (have_previous) {
        apta_waveform_span_t *last_span =
            &result->overview_spans[span_index - 1u];
        apta_source_frame_t end_frame =
            ((apta_source_frame_t)previous_column + 1u) *
            session->overview_frames_per_column;
        if (session->end_of_input_signalled) {
            end_frame = apta_pool_min_frame(
                end_frame,
                session->final_end_frame);
        }
        last_span->source_range.end_frame = end_frame;
        last_span->column_count = output_index - span_output_start;
    }

    result->overview.struct_size = (uint32_t)sizeof(result->overview);
    result->overview.api_version = APTA_API_VERSION;
    result->overview.level.struct_size =
        (uint32_t)sizeof(result->overview.level);
    result->overview.level.api_version = APTA_API_VERSION;
    result->overview.level.level_id = 0u;
    result->overview.level.frames_per_column =
        session->overview_frames_per_column;
    result->overview.level.origin_frame = 0u;
    result->overview.confidence = APTA_CONFIDENCE_UNKNOWN;
    result->overview.span_count = span_count;
    result->overview.spans = result->overview_spans;

    full_coverage = session->end_of_input_signalled &&
                    span_count == 1u &&
                    result->overview_spans[0].source_range.first_frame == 0u &&
                    result->overview_spans[0].source_range.end_frame ==
                        session->final_end_frame;
    result->overview.state =
        full_coverage &&
                atomic_load_explicit(
                    &session->state,
                    memory_order_acquire) == APTA_SESSION_COMPLETED
            ? APTA_FEATURE_FINAL
            : (full_coverage ? APTA_FEATURE_STABLE
                             : APTA_FEATURE_PARTIAL);
    result->info.available_features |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    return APTA_STATUS_OK;
}

static apta_source_frame_t apta_pool_detail_tile_first(uint32_t tile_index)
{
    return (apta_source_frame_t)tile_index *
           (apta_source_frame_t)APTA_INTERNAL_DETAIL_TILE_FRAMES;
}

static uint32_t apta_pool_detail_expected_tile_columns(
    const apta_session_t *session,
    uint32_t tile_index)
{
    apta_source_frame_t tile_first =
        apta_pool_detail_tile_first(tile_index);
    apta_source_frame_t tile_end =
        tile_first + APTA_INTERNAL_DETAIL_TILE_FRAMES;
    apta_source_frame_t frames;

    if (!session->end_of_input_signalled ||
        session->final_end_frame >= tile_end) {
        return APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE;
    }
    if (session->final_end_frame <= tile_first) {
        return 0u;
    }

    frames = session->final_end_frame - tile_first;
    return (uint32_t)(
        (frames + APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN - 1u) /
        APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN);
}

static void apta_pool_sort_detail_slots(
    const apta_session_t *session,
    uint32_t *slots,
    uint32_t slot_count)
{
    uint32_t index;

    for (index = 1u; index < slot_count; ++index) {
        uint32_t value = slots[index];
        uint32_t position = index;

        while (position > 0u &&
               session->detail_tiles[slots[position - 1u]].tile_index >
                   session->detail_tiles[value].tile_index) {
            slots[position] = slots[position - 1u];
            position -= 1u;
        }
        slots[position] = value;
    }
}

static apta_status_t apta_pool_build_detail(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result)
{
    uint32_t slots[APTA_INTERNAL_MAX_DETAIL_TILES];
    uint8_t *slot_storage;
    uint32_t tile_count = 0u;
    uint32_t total_columns = 0u;
    uint32_t slot;
    uint32_t output_column = 0u;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
        return APTA_STATUS_OK;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_DETAIL_TILES; ++slot) {
        const apta_internal_detail_tile_t *tile =
            &session->detail_tiles[slot];
        uint32_t count = APTA_DETAIL_RUN_COUNT(tile);

        if (!tile->occupied || APTA_DETAIL_RUN_VALID(tile) == 0u ||
            count == 0u) {
            continue;
        }
        if (UINT32_MAX - total_columns < count) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        slots[tile_count++] = slot;
        total_columns += count;
    }

    if (tile_count == 0u) {
        return APTA_STATUS_OK;
    }
    if (tile_count > pool->layout.detail_tile_capacity ||
        total_columns > pool->layout.detail_column_capacity) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (!apta_pool_region_is_valid(
            pool,
            pool->layout.detail_tiles_offset,
            (size_t)pool->layout.detail_tile_capacity *
                sizeof(apta_waveform_tile_view_t)) ||
        !apta_pool_region_is_valid(
            pool,
            pool->layout.detail_columns_offset,
            (size_t)pool->layout.detail_column_capacity *
                sizeof(apta_waveform_column_t))) {
        return APTA_ERROR_INTERNAL;
    }

    slot_storage = apta_pool_result_slot_storage(pool, result);
    if (slot_storage == NULL) {
        return APTA_ERROR_INTERNAL;
    }
    result->detail_tiles = (apta_waveform_tile_view_t *)(void *)(
        slot_storage + pool->layout.detail_tiles_offset);
    result->detail_columns = (apta_waveform_column_t *)(void *)(
        slot_storage + pool->layout.detail_columns_offset);

    apta_pool_sort_detail_slots(session, slots, tile_count);
    for (slot = 0u; slot < tile_count; ++slot) {
        const apta_internal_detail_tile_t *tile =
            &session->detail_tiles[slots[slot]];
        apta_waveform_tile_view_t *view = &result->detail_tiles[slot];
        uint32_t first = APTA_DETAIL_RUN_FIRST(tile);
        uint32_t count = APTA_DETAIL_RUN_COUNT(tile);
        uint32_t expected_columns =
            apta_pool_detail_expected_tile_columns(
                session,
                tile->tile_index);
        apta_source_frame_t tile_first =
            apta_pool_detail_tile_first(tile->tile_index);
        apta_source_frame_t source_end;
        uint32_t column;

        view->struct_size = (uint32_t)sizeof(*view);
        view->api_version = APTA_API_VERSION;
        view->level_id = APTA_INTERNAL_DETAIL_LEVEL_ID;
        view->tile_index = tile->tile_index;
        view->source_range.struct_size =
            (uint32_t)sizeof(view->source_range);
        view->source_range.api_version = APTA_API_VERSION;
        view->source_range.first_frame =
            tile_first +
            (apta_source_frame_t)first *
                APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
        source_end = view->source_range.first_frame +
                     (apta_source_frame_t)count *
                         APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
        if (session->end_of_input_signalled &&
            source_end > session->final_end_frame) {
            source_end = session->final_end_frame;
        }
        view->source_range.end_frame = source_end;
        view->first_column_index =
            tile->tile_index * APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE + first;
        view->column_count = count;
        view->columns = &result->detail_columns[output_column];
        view->confidence = APTA_CONFIDENCE_UNKNOWN;
        view->state = first == 0u && count == expected_columns
                          ? (session->end_of_input_signalled
                                 ? APTA_FEATURE_FINAL
                                 : APTA_FEATURE_STABLE)
                          : APTA_FEATURE_PARTIAL;

        for (column = 0u; column < count; ++column) {
            const apta_internal_waveform_accumulator_t *accumulator =
                &tile->accumulators[first + column];
            apta_waveform_column_t *output =
                &result->detail_columns[output_column + column];

            output->minimum =
                apta_pool_quantize_peak(accumulator->minimum);
            output->maximum =
                apta_pool_quantize_peak(accumulator->maximum);
            output->rms = apta_pool_quantize_rms(
                accumulator->sum_squares,
                accumulator->sample_count);
            output->flags = APTA_WAVEFORM_COLUMN_VALID;
            if (accumulator->clipped) {
                output->flags |= APTA_WAVEFORM_COLUMN_CLIPPED;
            }
        }
        output_column += count;
    }

    result->detail_tile_count = tile_count;
    result->info.available_features |= APTA_FEATURE_WAVEFORM_DETAIL;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_result_pool_create_session_result(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_generation_t generation,
    apta_feature_mask_t changed_features,
    apta_result_t **result_out)
{
    apta_result_t *result = NULL;
    apta_status_t status;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;
    if (pool == NULL || session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_internal_result_pool_create_empty_result(
        pool,
        &session->config,
        generation,
        atomic_load_explicit(&session->state, memory_order_acquire),
        changed_features,
        session->lineage_id_high,
        session->lineage_id_low,
        &result);
    if (status < 0) {
        return status;
    }

    status = apta_pool_build_metadata(pool, session, result);
    if (status >= 0) {
        status = apta_pool_build_overview(pool, session, result);
    }
    if (status >= 0) {
        status = apta_pool_build_detail(pool, session, result);
    }
    if (status < 0) {
        apta_internal_result_release(result);
        return status;
    }

    *result_out = result;
    return APTA_STATUS_OK;
}
