// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

/*
 * Internal publication-run state is stored in the existing private reserved
 * bytes so the experimental branch does not perturb the public ABI or the
 * verified overview-only layout.
 */
#define APTA_DETAIL_RUN_VALID(tile) ((tile)->reserved8[0])
#define APTA_DETAIL_RUN_FIRST(tile) ((tile)->reserved8[1])
#define APTA_DETAIL_RUN_COUNT(tile) ((tile)->reserved8[2])

static int apta_detail_ranges_overlap(
    apta_source_frame_t first_a,
    apta_source_frame_t end_a,
    apta_source_frame_t first_b,
    apta_source_frame_t end_b)
{
    return first_a < end_b && first_b < end_a;
}

static apta_source_frame_t apta_detail_tile_first(uint32_t tile_index)
{
    return (apta_source_frame_t)tile_index *
           (apta_source_frame_t)APTA_INTERNAL_DETAIL_TILE_FRAMES;
}

static apta_source_frame_t apta_detail_tile_end(uint32_t tile_index)
{
    return apta_detail_tile_first(tile_index) +
           (apta_source_frame_t)APTA_INTERNAL_DETAIL_TILE_FRAMES;
}

static int apta_detail_tile_is_protected(
    const apta_session_t *session,
    uint32_t tile_index)
{
    const apta_source_frame_t tile_first =
        apta_detail_tile_first(tile_index);
    const apta_source_frame_t tile_end =
        apta_detail_tile_end(tile_index);
    uint32_t slot;

    if (session->has_focus &&
        (session->focus.feature_mask & APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
        apta_source_frame_t focus_first =
            session->focus.playhead_frame > session->focus.lookbehind_frames
                ? session->focus.playhead_frame -
                      session->focus.lookbehind_frames
                : 0u;
        apta_source_frame_t focus_end = session->focus.playhead_frame;

        if (UINT64_MAX - focus_end < session->focus.lookahead_frames) {
            focus_end = UINT64_MAX;
        } else {
            focus_end += session->focus.lookahead_frames;
        }

        if (apta_detail_ranges_overlap(
                tile_first,
                tile_end,
                focus_first,
                focus_end)) {
            return 1;
        }
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        const apta_internal_request_t *request = &session->requests[slot];

        if (request->request_id == 0u ||
            request->state == APTA_REQUEST_CANCELLED ||
            request->state == APTA_REQUEST_FAILED ||
            request->state == APTA_REQUEST_SATISFIED ||
            (request->request.feature_mask &
             APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
            continue;
        }

        if (apta_detail_ranges_overlap(
                tile_first,
                tile_end,
                request->request.range.first_frame,
                request->request.range.end_frame)) {
            return 1;
        }
    }

    return 0;
}

static void apta_detail_initialize_tile(
    apta_internal_detail_tile_t *tile,
    uint32_t tile_index,
    uint64_t access_serial)
{
    uint32_t column;

    memset(tile, 0, sizeof(*tile));
    tile->tile_index = tile_index;
    tile->access_serial = access_serial;
    tile->occupied = 1u;

    for (column = 0u;
         column < APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE;
         ++column) {
        tile->accumulators[column].column_index = column;
        tile->accumulators[column].minimum = 1.0f;
        tile->accumulators[column].maximum = -1.0f;
    }
}

static apta_internal_detail_tile_t *apta_detail_find_or_create_tile(
    apta_session_t *session,
    uint32_t tile_index)
{
    apta_internal_detail_tile_t *empty = NULL;
    apta_internal_detail_tile_t *oldest = NULL;
    apta_internal_detail_tile_t *oldest_unprotected = NULL;
    uint32_t slot;

    for (slot = 0u; slot < APTA_INTERNAL_MAX_DETAIL_TILES; ++slot) {
        apta_internal_detail_tile_t *tile = &session->detail_tiles[slot];

        if (tile->occupied && tile->tile_index == tile_index) {
            session->detail_access_serial += 1u;
            tile->access_serial = session->detail_access_serial;
            return tile;
        }
        if (!tile->occupied && empty == NULL) {
            empty = tile;
            continue;
        }
        if (tile->occupied &&
            (oldest == NULL || tile->access_serial < oldest->access_serial)) {
            oldest = tile;
        }
        if (tile->occupied &&
            !apta_detail_tile_is_protected(session, tile->tile_index) &&
            (oldest_unprotected == NULL ||
             tile->access_serial < oldest_unprotected->access_serial)) {
            oldest_unprotected = tile;
        }
    }

    if (empty == NULL) {
        if (oldest_unprotected != NULL) {
            empty = oldest_unprotected;
        } else if (!apta_detail_tile_is_protected(session, tile_index)) {
            /* All resident tiles are protected; degrade the incoming cache. */
            return NULL;
        } else {
            /* The protected working set exceeds the fixed four-tile budget. */
            empty = oldest;
        }
    }
    if (empty == NULL) {
        return NULL;
    }

    if (empty->occupied && APTA_DETAIL_RUN_VALID(empty) != 0u) {
        session->detail_mutation_serial += 1u;
    }

    session->detail_access_serial += 1u;
    apta_detail_initialize_tile(
        empty,
        tile_index,
        session->detail_access_serial);
    return empty;
}

static uint32_t apta_detail_expected_column_frames(
    const apta_session_t *session,
    uint32_t tile_index,
    uint32_t column_index)
{
    const apta_source_frame_t first_frame =
        apta_detail_tile_first(tile_index) +
        (apta_source_frame_t)column_index *
            APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
    apta_source_frame_t end_frame;

    if (!session->end_of_input_signalled) {
        return APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
    }
    if (first_frame >= session->final_end_frame) {
        return 0u;
    }

    end_frame = first_frame + APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
    if (end_frame > session->final_end_frame) {
        end_frame = session->final_end_frame;
    }
    return (uint32_t)(end_frame - first_frame);
}

static uint32_t apta_detail_expected_tile_columns(
    const apta_session_t *session,
    uint32_t tile_index)
{
    const apta_source_frame_t tile_first =
        apta_detail_tile_first(tile_index);
    apta_source_frame_t frames;

    if (!session->end_of_input_signalled ||
        session->final_end_frame >= apta_detail_tile_end(tile_index)) {
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

static void apta_detail_select_or_extend_run(
    apta_internal_detail_tile_t *tile)
{
    uint32_t old_valid = APTA_DETAIL_RUN_VALID(tile);
    uint32_t old_first = APTA_DETAIL_RUN_FIRST(tile);
    uint32_t old_count = APTA_DETAIL_RUN_COUNT(tile);
    uint32_t first;
    uint32_t end;

    if (old_valid != 0u) {
        first = old_first;
        end = old_first + old_count;

        while (first > 0u && tile->accumulators[first - 1u].complete) {
            first -= 1u;
        }
        while (end < APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE &&
               tile->accumulators[end].complete) {
            end += 1u;
        }

        APTA_DETAIL_RUN_FIRST(tile) = (uint8_t)first;
        APTA_DETAIL_RUN_COUNT(tile) = (uint8_t)(end - first);
        return;
    }

    {
        uint32_t best_first = 0u;
        uint32_t best_count = 0u;
        uint32_t current_first = 0u;
        uint32_t current_count = 0u;
        uint32_t column;

        for (column = 0u;
             column < APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE;
             ++column) {
            if (tile->accumulators[column].complete) {
                if (current_count == 0u) {
                    current_first = column;
                }
                current_count += 1u;
                if (current_count > best_count) {
                    best_first = current_first;
                    best_count = current_count;
                }
            } else {
                current_count = 0u;
            }
        }

        if (best_count != 0u) {
            APTA_DETAIL_RUN_VALID(tile) = 1u;
            APTA_DETAIL_RUN_FIRST(tile) = (uint8_t)best_first;
            APTA_DETAIL_RUN_COUNT(tile) = (uint8_t)best_count;
        }
    }
}

apta_status_t apta_internal_detail_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
    float sample)
{
    uint64_t tile64;
    uint32_t tile_index;
    uint32_t column_index;
    float magnitude;
    uint32_t scaled;
    apta_internal_detail_tile_t *tile;
    apta_internal_waveform_accumulator_t *accumulator;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
        return APTA_STATUS_OK;
    }

    tile64 = source_frame / APTA_INTERNAL_DETAIL_TILE_FRAMES;
    if (tile64 > UINT32_MAX /
                     APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    tile_index = (uint32_t)tile64;
    column_index = (uint32_t)(
        (source_frame % APTA_INTERNAL_DETAIL_TILE_FRAMES) /
        APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN);

    tile = apta_detail_find_or_create_tile(session, tile_index);
    if (tile == NULL) {
        /* Bounded cache degradation is not a PCM acceptance failure. */
        return APTA_STATUS_NOT_AVAILABLE;
    }

    accumulator = &tile->accumulators[column_index];
    if (sample < accumulator->minimum) {
        accumulator->minimum = sample;
    }
    if (sample > accumulator->maximum) {
        accumulator->maximum = sample;
    }
    /* A3: integer accumulation with a branchless clamp. */
    magnitude = fminf(fabsf(sample), 1.0f);
    /* Widening 32x32->64 multiply; see apta_waveform_process.c. */
    scaled = (uint32_t)(magnitude * APTA_INTERNAL_SQUARE_MAGNITUDE_SCALE);
    accumulator->sum_squares += (uint64_t)scaled * scaled;
    accumulator->sample_count += 1u;
    if (sample <= -1.0f || sample >= 1.0f) {
        accumulator->clipped = 1u;
    }

    return APTA_STATUS_OK;
}

void apta_internal_detail_refresh_completed(apta_session_t *session)
{
    uint32_t slot;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
        return;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_DETAIL_TILES; ++slot) {
        apta_internal_detail_tile_t *tile = &session->detail_tiles[slot];
        uint32_t old_valid;
        uint32_t old_first;
        uint32_t old_count;
        uint32_t column;

        if (!tile->occupied) {
            continue;
        }

        old_valid = APTA_DETAIL_RUN_VALID(tile);
        old_first = APTA_DETAIL_RUN_FIRST(tile);
        old_count = APTA_DETAIL_RUN_COUNT(tile);

        for (column = 0u;
             column < APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE;
             ++column) {
            apta_internal_waveform_accumulator_t *accumulator =
                &tile->accumulators[column];
            const uint32_t expected = apta_detail_expected_column_frames(
                session,
                tile->tile_index,
                column);

            if (!accumulator->complete && expected != 0u &&
                accumulator->sample_count == expected) {
                accumulator->complete = 1u;
                tile->complete_count += 1u;
            }
        }

        apta_detail_select_or_extend_run(tile);
        if (old_valid != APTA_DETAIL_RUN_VALID(tile) ||
            old_first != APTA_DETAIL_RUN_FIRST(tile) ||
            old_count != APTA_DETAIL_RUN_COUNT(tile)) {
            session->detail_mutation_serial += 1u;
        }
    }
}

static const apta_internal_detail_tile_t *apta_detail_find_tile(
    const apta_session_t *session,
    uint32_t tile_index)
{
    uint32_t slot;

    for (slot = 0u; slot < APTA_INTERNAL_MAX_DETAIL_TILES; ++slot) {
        const apta_internal_detail_tile_t *tile = &session->detail_tiles[slot];

        if (tile->occupied && tile->tile_index == tile_index) {
            return tile;
        }
    }
    return NULL;
}

int apta_internal_detail_range_complete(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame)
{
    uint64_t first_column;
    uint64_t last_column;
    uint64_t global_column;

    if (first_frame >= end_frame) {
        return 0;
    }

    first_column = first_frame / APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
    last_column = (end_frame - 1u) /
                  APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;

    for (global_column = first_column;
         global_column <= last_column;
         ++global_column) {
        const uint64_t tile64 =
            global_column / APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE;
        const uint32_t local_column = (uint32_t)(
            global_column % APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE);
        const apta_internal_detail_tile_t *tile;

        if (tile64 > UINT32_MAX) {
            return 0;
        }
        tile = apta_detail_find_tile(session, (uint32_t)tile64);
        if (tile == NULL || !tile->accumulators[local_column].complete) {
            return 0;
        }
    }

    return 1;
}

int apta_internal_detail_range_has_output(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame)
{
    uint32_t slot;

    for (slot = 0u; slot < APTA_INTERNAL_MAX_DETAIL_TILES; ++slot) {
        const apta_internal_detail_tile_t *tile = &session->detail_tiles[slot];
        apta_source_frame_t run_first;
        apta_source_frame_t run_end;

        if (!tile->occupied || APTA_DETAIL_RUN_VALID(tile) == 0u) {
            continue;
        }

        run_first = apta_detail_tile_first(tile->tile_index) +
                    (apta_source_frame_t)APTA_DETAIL_RUN_FIRST(tile) *
                        APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
        run_end = run_first +
                  (apta_source_frame_t)APTA_DETAIL_RUN_COUNT(tile) *
                      APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
        if (session->end_of_input_signalled &&
            run_end > session->final_end_frame) {
            run_end = session->final_end_frame;
        }

        if (apta_detail_ranges_overlap(
                run_first,
                run_end,
                first_frame,
                end_frame)) {
            return 1;
        }
    }

    return 0;
}

static double apta_detail_round_ties_even(double value)
{
    const double lower = floor(value);
    const double fraction = value - lower;

    if (fraction < 0.5) {
        return lower;
    }
    if (fraction > 0.5) {
        return lower + 1.0;
    }
    return fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0;
}

static int16_t apta_detail_quantize_peak(float value)
{
    double rounded;

    if (value <= -1.0f) {
        return INT16_MIN;
    }
    if (value >= 1.0f) {
        return INT16_MAX;
    }

    rounded = apta_detail_round_ties_even((double)value * 32767.0);
    return (int16_t)rounded;
}

/* A3: see apta_quantize_rms() in apta_waveform_snapshot.c. */
static uint16_t apta_detail_quantize_rms(
    uint64_t sum_squares,
    uint32_t sample_count)
{
    double rms;
    double rounded;

    if (sample_count == 0u) {
        return 0u;
    }

    rms = sqrt((double)sum_squares / (double)sample_count) /
          (double)APTA_INTERNAL_SQUARE_MAGNITUDE_SCALE;
    if (rms > 1.0) {
        rms = 1.0;
    }
    rounded = apta_detail_round_ties_even(rms * 65535.0);
    if (rounded < 0.0) {
        rounded = 0.0;
    }
    if (rounded > 65535.0) {
        rounded = 65535.0;
    }
    return (uint16_t)rounded;
}

static void apta_detail_sort_slots(
    const apta_session_t *session,
    uint32_t *slots,
    uint32_t slot_count)
{
    uint32_t index;

    for (index = 1u; index < slot_count; ++index) {
        const uint32_t value = slots[index];
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

apta_status_t apta_internal_detail_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    uint32_t slots[APTA_INTERNAL_MAX_DETAIL_TILES];
    uint32_t tile_count = 0u;
    uint32_t total_columns = 0u;
    uint32_t slot;
    uint32_t output_column = 0u;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
        return APTA_STATUS_OK;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_DETAIL_TILES; ++slot) {
        const apta_internal_detail_tile_t *tile = &session->detail_tiles[slot];
        const uint32_t count = APTA_DETAIL_RUN_COUNT(tile);

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

    apta_detail_sort_slots(session, slots, tile_count);

    result->detail_tiles =
        (apta_waveform_tile_view_t *)apta_internal_context_allocate(
            session->context,
            (size_t)tile_count * sizeof(*result->detail_tiles),
            alignof(apta_waveform_tile_view_t),
            APTA_MEMORY_PERSISTENT);
    if (result->detail_tiles == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    result->detail_columns =
        (apta_waveform_column_t *)apta_internal_context_allocate(
            session->context,
            (size_t)total_columns * sizeof(*result->detail_columns),
            alignof(apta_waveform_column_t),
            APTA_MEMORY_PERSISTENT);
    if (result->detail_columns == NULL) {
        apta_internal_context_deallocate(
            session->context,
            result->detail_tiles);
        result->detail_tiles = NULL;
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(
        result->detail_tiles,
        0,
        (size_t)tile_count * sizeof(*result->detail_tiles));
    memset(
        result->detail_columns,
        0,
        (size_t)total_columns * sizeof(*result->detail_columns));

    for (slot = 0u; slot < tile_count; ++slot) {
        const apta_internal_detail_tile_t *tile =
            &session->detail_tiles[slots[slot]];
        apta_waveform_tile_view_t *view = &result->detail_tiles[slot];
        const uint32_t first = APTA_DETAIL_RUN_FIRST(tile);
        const uint32_t count = APTA_DETAIL_RUN_COUNT(tile);
        const uint32_t expected_columns = apta_detail_expected_tile_columns(
            session,
            tile->tile_index);
        const apta_source_frame_t tile_first =
            apta_detail_tile_first(tile->tile_index);
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

        if (first == 0u && count == expected_columns) {
            view->state = session->end_of_input_signalled
                              ? APTA_FEATURE_FINAL
                              : APTA_FEATURE_STABLE;
        } else {
            view->state = APTA_FEATURE_PARTIAL;
        }

        for (column = 0u; column < count; ++column) {
            const apta_internal_waveform_accumulator_t *accumulator =
                &tile->accumulators[first + column];
            apta_waveform_column_t *output =
                &result->detail_columns[output_column + column];

            output->minimum = apta_detail_quantize_peak(accumulator->minimum);
            output->maximum = apta_detail_quantize_peak(accumulator->maximum);
            output->rms = apta_detail_quantize_rms(
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
