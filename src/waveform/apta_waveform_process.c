// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

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

static apta_status_t apta_ensure_accumulator_capacity(
    apta_session_t *session,
    uint32_t needed)
{
    apta_internal_waveform_accumulator_t *replacement;
    uint32_t capacity;
    size_t bytes;

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

    if ((size_t)capacity > SIZE_MAX / sizeof(*replacement)) {
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
        accumulator->sum_squares += (double)sample * (double)sample;
        accumulator->sample_count += 1u;
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
