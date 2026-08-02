// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_session_workspace.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

/* C1/B3: one-pole coefficients for the shared band split, derived from the
 * sample rate so the corners stay at 200 Hz and 2 kHz whatever the rate is.
 * a = 1 - exp(-2*pi*fc/fs), clamped to a stable range. */
void apta_internal_band_filter_init(
    apta_internal_band_filter_t *filter,
    uint32_t sample_rate)
{
    const float rate = (float)sample_rate;
    float low;
    float mid;

    memset(filter, 0, sizeof(*filter));

    if (rate <= 0.0f) {
        return;
    }

    low = 1.0f - expf(-6.2831853f * APTA_INTERNAL_BAND_LOW_HZ / rate);
    mid = 1.0f - expf(-6.2831853f * APTA_INTERNAL_BAND_HIGH_HZ / rate);
    /* A corner at or above Nyquist degenerates; pass the sample through. */
    filter->low_coefficient = fminf(fmaxf(low, 0.0f), 1.0f);
    filter->mid_coefficient = fminf(fmaxf(mid, 0.0f), 1.0f);
}

void apta_internal_band_filter_reset(apta_internal_band_filter_t *filter)
{
    filter->low_state = 0.0f;
    filter->mid_state = 0.0f;
}

void apta_internal_band_filter_split(
    apta_internal_band_filter_t *filter,
    float sample,
    float out_bands[APTA_INTERNAL_BAND_COUNT])
{
    filter->low_state +=
        filter->low_coefficient * (sample - filter->low_state);
    filter->mid_state +=
        filter->mid_coefficient * (sample - filter->mid_state);

    out_bands[0] = filter->low_state;
    out_bands[1] = filter->mid_state - filter->low_state;
    out_bands[2] = sample - filter->mid_state;
}

void apta_internal_waveform_init_bands(apta_session_t *session)
{
    apta_internal_band_filter_init(
        &session->overview_band_filter,
        session->config.source_sample_rate);
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
    apta_internal_band_filter_init(
        &session->onset_band_filter,
        session->config.source_sample_rate);
    session->onset_band_next_frame = 0u;
    session->onset_band_filter_valid = 0u;
#endif
}

static uint32_t apta_min_u32(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static apta_source_frame_t apta_min_frame(
    apta_source_frame_t left,
    apta_source_frame_t right)
{
    return left < right ? left : right;
}

static int apta_ranges_overlap(
    apta_source_frame_t first_a,
    apta_source_frame_t end_a,
    apta_source_frame_t first_b,
    apta_source_frame_t end_b)
{
    return first_a < end_b && first_b < end_a;
}

/* C1: grow the parallel band-sum array to match the accumulator capacity.
 * Shared, because the accumulators are grown from two places: this file's
 * insert path and the detail layer's pre-reservation in
 * apta_waveform_process_detail.c. A no-op when bands were not requested. */
apta_status_t apta_internal_waveform_grow_band_sums(
    apta_session_t *session,
    uint32_t capacity)
{
    uint32_t *bands;
    size_t band_bytes;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_3BAND) == 0u) {
        return APTA_STATUS_OK;
    }
    if (!apta_internal_size_array_fits(
            0u,
            (size_t)capacity * APTA_INTERNAL_BAND_COUNT,
            sizeof(uint32_t))) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    band_bytes =
        (size_t)capacity * APTA_INTERNAL_BAND_COUNT * sizeof(uint32_t);

    bands = (uint32_t *)apta_internal_session_allocate(
        session,
        band_bytes,
        alignof(uint32_t),
        APTA_MEMORY_PERSISTENT);
    if (bands == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(bands, 0, band_bytes);
    if (session->overview_accumulator_count != 0u &&
        session->overview_band_sums != NULL) {
        memcpy(
            bands,
            session->overview_band_sums,
            (size_t)session->overview_accumulator_count *
                APTA_INTERNAL_BAND_COUNT * sizeof(uint32_t));
    }
    apta_internal_context_deallocate(
        session->context,
        session->overview_band_sums);
    session->overview_band_sums = bands;
    return APTA_STATUS_OK;
}

static apta_status_t apta_ensure_accumulator_capacity(
    apta_session_t *session,
    uint32_t needed)
{
    apta_internal_waveform_accumulator_t *replacement;
    uint32_t capacity;
    size_t bytes;
    apta_status_t status;

    if (session->overview_accumulator_capacity >= needed) {
        return APTA_STATUS_OK;
    }

    capacity = session->overview_accumulator_capacity == 0u
                   ? 16u
                   : session->overview_accumulator_capacity;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2u) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        capacity *= 2u;
    }

    if (!apta_internal_size_array_fits(
            0u,
            capacity,
            sizeof(*replacement))) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    bytes = (size_t)capacity * sizeof(*replacement);

    replacement =
        (apta_internal_waveform_accumulator_t *)apta_internal_context_allocate(
            session->context,
            bytes,
            alignof(apta_internal_waveform_accumulator_t),
            APTA_MEMORY_PERSISTENT);
    if (replacement == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    if (session->overview_accumulator_count != 0u) {
        memcpy(
            replacement,
            session->overview_accumulators,
            (size_t)session->overview_accumulator_count * sizeof(*replacement));
    }

    apta_internal_context_deallocate(
        session->context,
        session->overview_accumulators);
    session->overview_accumulators = replacement;

    status = apta_internal_waveform_grow_band_sums(session, capacity);
    if (status < 0) {
        return status;
    }

    session->overview_accumulator_capacity = capacity;
    return APTA_STATUS_OK;
}

static apta_status_t apta_find_or_insert_accumulator(
    apta_session_t *session,
    uint32_t column_index,
    apta_internal_waveform_accumulator_t **accumulator_out)
{
    uint32_t low;
    uint32_t high;
    uint32_t middle;
    uint32_t position;
    uint32_t index;
    apta_status_t status;

    low = 0u;
    high = session->overview_accumulator_count;

    while (low < high) {
        middle = low + (high - low) / 2u;
        if (session->overview_accumulators[middle].column_index < column_index) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }

    position = low;
    if (position < session->overview_accumulator_count &&
        session->overview_accumulators[position].column_index == column_index) {
        *accumulator_out = &session->overview_accumulators[position];
        return APTA_STATUS_OK;
    }

    status = apta_ensure_accumulator_capacity(
        session,
        session->overview_accumulator_count + 1u);
    if (status < 0) {
        return status;
    }

    for (index = session->overview_accumulator_count;
         index > position;
         --index) {
        session->overview_accumulators[index] =
            session->overview_accumulators[index - 1u];
    }

    /* C1: the band sums are a parallel array indexed by position, so they must
     * follow the same insertion shift. Getting this wrong attaches a column's
     * bands to its neighbour, which no existing test would have caught. */
    if (session->overview_band_sums != NULL) {
        uint32_t *bands = session->overview_band_sums;
        uint32_t band;

        for (index = session->overview_accumulator_count;
             index > position;
             --index) {
            for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
                bands[(size_t)index * APTA_INTERNAL_BAND_COUNT + band] =
                    bands[(size_t)(index - 1u) *
                              APTA_INTERNAL_BAND_COUNT + band];
            }
        }
        for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
            bands[(size_t)position * APTA_INTERNAL_BAND_COUNT + band] = 0u;
        }
    }

    memset(
        &session->overview_accumulators[position],
        0,
        sizeof(session->overview_accumulators[position]));
    session->overview_accumulators[position].column_index = column_index;
    session->overview_accumulators[position].minimum = 1.0f;
    session->overview_accumulators[position].maximum = -1.0f;
    session->overview_accumulator_count += 1u;

    *accumulator_out = &session->overview_accumulators[position];
    return APTA_STATUS_OK;
}

/* D1: install a completed overview accumulator for one column, reconstructed
 * from a parsed result. Exposed because the accumulator store and its sorted
 * insertion are private to this file. */
apta_status_t apta_internal_waveform_seed_column(
    apta_session_t *session,
    uint32_t column_index,
    float minimum,
    float maximum,
    uint64_t sum_squares,
    uint32_t sample_count,
    int clipped)
{
    apta_internal_waveform_accumulator_t *accumulator;
    apta_status_t status;

    status = apta_find_or_insert_accumulator(
        session,
        column_index,
        &accumulator);
    if (status < 0) {
        return status;
    }
    accumulator->minimum = minimum;
    accumulator->maximum = maximum;
    accumulator->sum_squares = sum_squares;
    accumulator->sample_count = sample_count;
    accumulator->clipped = clipped != 0 ? 1u : 0u;
    /* overview_complete_count is maintained separately from the per-accumulator
     * flag and is what the snapshot sizes its column array from. Setting the
     * flag without it makes the snapshot write past its allocation. */
    if (!accumulator->complete) {
        accumulator->complete = 1u;
        session->overview_complete_count += 1u;
    }
    return APTA_STATUS_OK;
}

static uint32_t apta_expected_column_frames(
    const apta_session_t *session,
    uint32_t column_index)
{
    apta_source_frame_t first_frame;
    apta_source_frame_t end_frame;

    first_frame = (apta_source_frame_t)column_index *
                  (apta_source_frame_t)session->overview_frames_per_column;

    if (!session->end_of_input_signalled) {
        return session->overview_frames_per_column;
    }

    if (first_frame >= session->final_end_frame) {
        return 0u;
    }

    end_frame = first_frame + session->overview_frames_per_column;
    end_frame = apta_min_frame(end_frame, session->final_end_frame);
    return (uint32_t)(end_frame - first_frame);
}

static void apta_refresh_completed_columns(apta_session_t *session)
{
    uint32_t index;

    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        apta_internal_waveform_accumulator_t *accumulator =
            &session->overview_accumulators[index];
        uint32_t expected;

        if (accumulator->complete) {
            continue;
        }

        expected = apta_expected_column_frames(session, accumulator->column_index);
        if (expected != 0u && accumulator->sample_count == expected) {
            accumulator->complete = 1u;
            session->overview_complete_count += 1u;
        }
    }
}

static apta_status_t apta_process_samples(
    apta_session_t *session,
    apta_internal_pcm_node_t *node,
    uint32_t frame_count)
{
    uint32_t offset;

    for (offset = 0u; offset < frame_count; ++offset) {
        apta_source_frame_t source_frame;
        uint64_t column64;
        uint32_t column_index;
        float sample;
        float magnitude;
        uint32_t scaled;
        apta_internal_waveform_accumulator_t *accumulator;
        apta_status_t status;

        source_frame = node->first_frame +
                       (uint64_t)node->processed_frames +
                       (uint64_t)offset;
        column64 = source_frame / session->overview_frames_per_column;
        if (column64 > UINT32_MAX) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        column_index = (uint32_t)column64;

        status = apta_find_or_insert_accumulator(
            session,
            column_index,
            &accumulator);
        if (status < 0) {
            return status;
        }

        sample = node->samples[node->processed_frames + offset];
        if (sample < accumulator->minimum) {
            accumulator->minimum = sample;
        }
        if (sample > accumulator->maximum) {
            accumulator->maximum = sample;
        }
        /* A3: integer accumulation with a branchless clamp. See
         * apta_internal_waveform_accumulator_t. */
        magnitude = fminf(fabsf(sample), 1.0f);
        /* scaled is at most 2^23, so it fits uint32_t and the product is a
         * widening 32x32->64 multiply, which 32-bit targets do natively.
         * Declaring it uint64_t instead makes this a full 64x64 multiply and
         * pulls in a libgcc helper call per sample -- measured as a new
         * libgcc.a dependency in the ESP-IDF component size report. */
        scaled = (uint32_t)(magnitude *
                            APTA_INTERNAL_SQUARE_MAGNITUDE_SCALE);
        accumulator->sum_squares += (uint64_t)scaled * scaled;
        accumulator->sample_count += 1u;

        /* C1: two one-pole low-passes split the sample three ways. The state
         * advances on every sample regardless of which column it lands in, so
         * the filter sees a continuous stream. */
        if (session->overview_band_sums != NULL) {
            const size_t base =
                (size_t)(accumulator - session->overview_accumulators) *
                APTA_INTERNAL_BAND_COUNT;
            float bands[APTA_INTERNAL_BAND_COUNT];

            apta_internal_band_filter_split(
                &session->overview_band_filter,
                sample,
                bands);

            session->overview_band_sums[base + 0u] += (uint32_t)(
                fminf(fabsf(bands[0]), 1.0f) *
                APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE);
            session->overview_band_sums[base + 1u] += (uint32_t)(
                fminf(fabsf(bands[1]), 1.0f) *
                APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE);
            session->overview_band_sums[base + 2u] += (uint32_t)(
                fminf(fabsf(bands[2]), 1.0f) *
                APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE);
        }

        if (sample <= -1.0f || sample >= 1.0f) {
            accumulator->clipped = 1u;
        }
    }

    return APTA_STATUS_OK;
}

static int apta_accumulator_range_complete(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame)
{
    uint64_t first_column;
    uint64_t last_column;
    uint64_t column;
    uint32_t index;

    if (first_frame >= end_frame) {
        return 0;
    }

    first_column = first_frame / session->overview_frames_per_column;
    last_column = (end_frame - 1u) / session->overview_frames_per_column;
    if (last_column > UINT32_MAX) {
        return 0;
    }

    index = 0u;
    for (column = first_column; column <= last_column; ++column) {
        while (index < session->overview_accumulator_count &&
               session->overview_accumulators[index].column_index < column) {
            index += 1u;
        }
        if (index == session->overview_accumulator_count ||
            session->overview_accumulators[index].column_index != column ||
            !session->overview_accumulators[index].complete) {
            return 0;
        }
    }

    return 1;
}

static int apta_accumulator_range_has_output(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame)
{
    uint32_t index;

    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        apta_source_frame_t column_first;
        apta_source_frame_t column_end;

        if (!session->overview_accumulators[index].complete) {
            continue;
        }

        column_first = (apta_source_frame_t)
                           session->overview_accumulators[index].column_index *
                       session->overview_frames_per_column;
        column_end = column_first + session->overview_frames_per_column;
        if (apta_ranges_overlap(column_first, column_end, first_frame, end_frame)) {
            return 1;
        }
    }

    return 0;
}

static void apta_update_request_states(apta_session_t *session)
{
    uint32_t slot;

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        apta_internal_request_t *request = &session->requests[slot];

        if (request->request_id == 0u ||
            request->state == APTA_REQUEST_CANCELLED ||
            request->state == APTA_REQUEST_FAILED ||
            request->state == APTA_REQUEST_SATISFIED) {
            continue;
        }

        if ((request->request.feature_mask &
             APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
            continue;
        }

        if (apta_accumulator_range_complete(
                session,
                request->request.range.first_frame,
                request->request.range.end_frame)) {
            request->state = APTA_REQUEST_SATISFIED;
        } else if (apta_accumulator_range_has_output(
                       session,
                       request->request.range.first_frame,
                       request->request.range.end_frame)) {
            request->state = APTA_REQUEST_PARTIALLY_SATISFIED;
        } else {
            request->state = APTA_REQUEST_WAITING_FOR_PCM;
        }
    }
}

static apta_internal_pcm_node_t *apta_select_pcm_node(
    apta_session_t *session,
    apta_internal_pcm_node_t **previous_out)
{
    apta_internal_pcm_node_t *node;
    apta_internal_pcm_node_t *previous;
    apta_internal_pcm_node_t *best;
    apta_internal_pcm_node_t *best_previous;
    uint32_t best_priority;
    uint32_t slot;
    apta_source_frame_t focus_first;
    apta_source_frame_t focus_end;

    best = NULL;
    best_previous = NULL;
    best_priority = 0u;

    focus_first = 0u;
    focus_end = 0u;
    if (session->has_focus &&
        (session->focus.feature_mask &
         APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
        focus_first = session->focus.playhead_frame > session->focus.lookbehind_frames
                          ? session->focus.playhead_frame -
                                session->focus.lookbehind_frames
                          : 0u;
        focus_end = session->focus.playhead_frame;
        if (UINT64_MAX - focus_end < session->focus.lookahead_frames) {
            focus_end = UINT64_MAX;
        } else {
            focus_end += session->focus.lookahead_frames;
        }
    }

    previous = NULL;
    for (node = session->pcm_head; node != NULL; node = node->next) {
        apta_source_frame_t node_first;
        apta_source_frame_t node_end;
        uint32_t priority;

        node_first = node->first_frame + node->processed_frames;
        node_end = node->first_frame + node->frame_count;
        priority = 0u;

        if (focus_end > focus_first &&
            apta_ranges_overlap(node_first, node_end, focus_first, focus_end)) {
            priority = session->focus.priority;
        }

        for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
            const apta_internal_request_t *request = &session->requests[slot];

            if (request->request_id == 0u ||
                request->state == APTA_REQUEST_CANCELLED ||
                request->state == APTA_REQUEST_FAILED ||
                request->state == APTA_REQUEST_SATISFIED ||
                (request->request.feature_mask &
                 APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
                continue;
            }

            if (apta_ranges_overlap(
                    node_first,
                    node_end,
                    request->request.range.first_frame,
                    request->request.range.end_frame) &&
                request->request.priority > priority) {
                priority = request->request.priority;
            }
        }

        if (best == NULL || priority > best_priority) {
            best = node;
            best_previous = previous;
            best_priority = priority;
        }

        previous = node;
    }

    *previous_out = best_previous;
    return best;
}

static void apta_remove_pcm_node(
    apta_session_t *session,
    apta_internal_pcm_node_t *node,
    apta_internal_pcm_node_t *previous)
{
    if (previous != NULL) {
        previous->next = node->next;
    } else {
        session->pcm_head = node->next;
    }

    if (session->pcm_tail == node) {
        session->pcm_tail = previous;
    }

    session->queued_pcm_frames -= node->frame_count;
    apta_internal_context_deallocate(session->context, node);
}

apta_status_t apta_internal_waveform_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out)
{
    uint32_t frame_limit;
    uint32_t step_limit;
    uint32_t consumed;
    uint32_t steps;
    uint32_t completed_before;
    uint64_t start_time;
    apta_status_t status;

    *did_work_out = 0u;
    *published_output_out = 0u;

    frame_limit = budget->maximum_input_frames == 0u
                      ? UINT32_MAX
                      : budget->maximum_input_frames;
    step_limit = budget->maximum_steps == 0u
                     ? UINT32_MAX
                     : budget->maximum_steps;
    consumed = 0u;
    steps = 0u;
    completed_before = session->overview_complete_count;

    apta_refresh_completed_columns(session);

    start_time = 0u;
    if (budget->soft_time_budget_us != 0u &&
        session->context->clock.monotonic_time_ns != NULL) {
        start_time = session->context->clock.monotonic_time_ns(
            session->context->clock.user_data);
    }

    while (session->pcm_head != NULL &&
           consumed < frame_limit &&
           steps < step_limit) {
        apta_internal_pcm_node_t *previous;
        apta_internal_pcm_node_t *node;
        uint32_t remaining;
        uint32_t chunk;

        node = apta_select_pcm_node(session, &previous);
        if (node == NULL) {
            break;
        }

        remaining = node->frame_count - node->processed_frames;
        chunk = apta_min_u32(remaining, APTA_INTERNAL_PROCESS_CHUNK_FRAMES);
        chunk = apta_min_u32(chunk, frame_limit - consumed);
        if (chunk == 0u) {
            break;
        }

        status = apta_process_samples(session, node, chunk);
        if (status < 0) {
            return status;
        }

        node->processed_frames += chunk;
        consumed += chunk;
        steps += 1u;
        *did_work_out = 1u;

        apta_refresh_completed_columns(session);

        if (node->processed_frames == node->frame_count) {
            apta_remove_pcm_node(session, node, previous);
        }

        if (start_time != 0u) {
            uint64_t now;
            uint64_t budget_ns;

            now = session->context->clock.monotonic_time_ns(
                session->context->clock.user_data);
            budget_ns = (uint64_t)budget->soft_time_budget_us * 1000u;
            if (now - start_time >= budget_ns) {
                break;
            }
        }
    }

    apta_update_request_states(session);

    if (session->overview_complete_count != completed_before) {
        status = apta_internal_publish_result(
            session,
            APTA_FEATURE_WAVEFORM_OVERVIEW);
        if (status < 0) {
            return status;
        }
        *published_output_out = 1u;
    }

    if (progress_out != NULL) {
        progress_out->consumed_input_frames = consumed;
        progress_out->completed_steps = steps;
        progress_out->changed_features =
            *published_output_out != 0u
                ? APTA_FEATURE_WAVEFORM_OVERVIEW
                : 0u;
        progress_out->published_generation = session->generation;
    }

    if (*did_work_out == 0u) {
        return APTA_STATUS_WOULD_BLOCK;
    }

    return session->pcm_head != NULL
               ? APTA_STATUS_MORE_WORK
               : APTA_STATUS_OK;
}

static int apta_find_missing_range(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame,
    apta_source_frame_t *missing_first_out,
    apta_source_frame_t *missing_end_out)
{
    apta_source_frame_t cursor;
    uint32_t index;

    cursor = first_frame;
    for (index = 0u; index < session->accepted_range_count; ++index) {
        const apta_internal_range_t *range = &session->accepted_ranges[index];

        if (range->end_frame <= cursor) {
            continue;
        }
        if (range->first_frame > cursor) {
            *missing_first_out = cursor;
            *missing_end_out = apta_min_frame(range->first_frame, end_frame);
            return *missing_first_out < *missing_end_out;
        }

        cursor = range->end_frame;
        if (cursor >= end_frame) {
            return 0;
        }
    }

    if (cursor < end_frame) {
        *missing_first_out = cursor;
        *missing_end_out = end_frame;
        return 1;
    }

    return 0;
}

apta_status_t apta_internal_waveform_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out)
{
    apta_source_frame_t target_first;
    apta_source_frame_t target_end;
    apta_source_frame_t missing_first;
    apta_source_frame_t missing_end;
    uint32_t priority;
    uint32_t token;
    uint32_t slot;

    target_first = 0u;
    target_end = 0u;
    priority = APTA_PRIORITY_BACKGROUND;
    token = 0u;

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        const apta_internal_request_t *candidate = &session->requests[slot];

        if (candidate->request_id == 0u ||
            candidate->state == APTA_REQUEST_CANCELLED ||
            candidate->state == APTA_REQUEST_FAILED ||
            candidate->state == APTA_REQUEST_SATISFIED ||
            (candidate->request.feature_mask &
             APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
            continue;
        }

        if (token == 0u || candidate->request.priority > priority) {
            target_first = candidate->request.range.first_frame;
            target_end = candidate->request.range.end_frame;
            priority = candidate->request.priority;
            token = candidate->request_id;
        }
    }

    if (token == 0u && session->has_focus &&
        (session->focus.feature_mask &
         APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
        target_first = session->focus.playhead_frame > session->focus.lookbehind_frames
                           ? session->focus.playhead_frame -
                                 session->focus.lookbehind_frames
                           : 0u;
        target_end = session->focus.playhead_frame;
        if (UINT64_MAX - target_end < session->focus.lookahead_frames) {
            target_end = UINT64_MAX;
        } else {
            target_end += session->focus.lookahead_frames;
        }
        priority = session->focus.priority;
    }

    if (target_end <= target_first) {
        if (session->config.total_frames == APTA_TOTAL_FRAMES_UNKNOWN) {
            target_first = session->greatest_accepted_end;
            if (UINT64_MAX - target_first < APTA_INTERNAL_MAX_PUSH_FRAMES) {
                target_end = UINT64_MAX;
            } else {
                target_end = target_first + APTA_INTERNAL_MAX_PUSH_FRAMES;
            }
        } else {
            target_first = 0u;
            target_end = session->config.total_frames;
        }
    }

    if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN) {
        target_end = apta_min_frame(target_end, session->config.total_frames);
    }

    if (!apta_find_missing_range(
            session,
            target_first,
            target_end,
            &missing_first,
            &missing_end)) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    if (missing_end - missing_first > APTA_INTERNAL_MAX_PUSH_FRAMES) {
        missing_end = missing_first + APTA_INTERNAL_MAX_PUSH_FRAMES;
    }

    request_out->range.first_frame = missing_first;
    request_out->range.end_frame = missing_end;
    request_out->feature_mask = APTA_FEATURE_WAVEFORM_OVERVIEW;
    request_out->priority = (uint8_t)priority;
    request_out->request_token = token;
    return APTA_STATUS_OK;
}
