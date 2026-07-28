// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <math.h>
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

static double apta_round_ties_even(double value)
{
    double lower;
    double fraction;

    lower = floor(value);
    fraction = value - lower;

    if (fraction < 0.5) {
        return lower;
    }
    if (fraction > 0.5) {
        return lower + 1.0;
    }

    return fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0;
}

static int16_t apta_quantize_peak(float value)
{
    double scaled;
    double rounded;

    if (value <= -1.0f) {
        return INT16_MIN;
    }
    if (value >= 1.0f) {
        return INT16_MAX;
    }

    scaled = (double)value * 32767.0;
    rounded = apta_round_ties_even(scaled);

    if (rounded < (double)INT16_MIN) {
        rounded = (double)INT16_MIN;
    }
    if (rounded > (double)INT16_MAX) {
        rounded = (double)INT16_MAX;
    }

    return (int16_t)rounded;
}

static uint16_t apta_quantize_rms(double sum_squares, uint32_t sample_count)
{
    double rms;
    double scaled;
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

    scaled = rms * 65535.0;
    rounded = apta_round_ties_even(scaled);

    if (rounded < 0.0) {
        rounded = 0.0;
    }
    if (rounded > 65535.0) {
        rounded = 65535.0;
    }

    return (uint16_t)rounded;
}

static float apta_normalize_s16(int16_t value)
{
    return value < 0
               ? (float)((double)value / 32768.0)
               : (value == 0 ? 0.0f : (float)((double)value / 32767.0));
}

static float apta_normalize_s24(const uint8_t *bytes)
{
    int32_t value;

    value = (int32_t)((uint32_t)bytes[0] |
                      ((uint32_t)bytes[1] << 8) |
                      ((uint32_t)bytes[2] << 16));
    if ((value & 0x00800000) != 0) {
        value |= (int32_t)0xff000000;
    }

    return value < 0
               ? (float)((double)value / 8388608.0)
               : (value == 0 ? 0.0f : (float)((double)value / 8388607.0));
}

static float apta_normalize_s32(int32_t value)
{
    return value < 0
               ? (float)((double)value / 2147483648.0)
               : (value == 0 ? 0.0f : (float)((double)value / 2147483647.0));
}

static float apta_clamp_f32(float value)
{
    if (!isfinite(value)) {
        return 0.0f;
    }
    if (value < -1.0f) {
        return -1.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float apta_read_channel_sample(
    const apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t frame_index,
    uint32_t channel)
{
    uint32_t sample_index;

    sample_index = frame_index * (uint32_t)session->config.channel_count + channel;

    switch (session->config.sample_format) {
    case APTA_SAMPLE_S16_NATIVE_INTERLEAVED: {
        int16_t value;
        const uint8_t *bytes = (const uint8_t *)block->data;
        memcpy(&value, bytes + (size_t)sample_index * sizeof(value), sizeof(value));
        return apta_normalize_s16(value);
    }
    case APTA_SAMPLE_S24_3LE_INTERLEAVED: {
        const uint8_t *bytes = (const uint8_t *)block->data;
        return apta_normalize_s24(bytes + (size_t)sample_index * 3u);
    }
    case APTA_SAMPLE_S32_NATIVE_INTERLEAVED: {
        int32_t value;
        const uint8_t *bytes = (const uint8_t *)block->data;
        memcpy(&value, bytes + (size_t)sample_index * sizeof(value), sizeof(value));
        return apta_normalize_s32(value);
    }
    case APTA_SAMPLE_F32_NATIVE_INTERLEAVED: {
        float value;
        const uint8_t *bytes = (const uint8_t *)block->data;
        memcpy(&value, bytes + (size_t)sample_index * sizeof(value), sizeof(value));
        return apta_clamp_f32(value);
    }
    case APTA_SAMPLE_F32_NATIVE_PLANAR: {
        float value;
        const uint8_t *bytes = (const uint8_t *)block->planes[channel];
        memcpy(&value, bytes + (size_t)frame_index * sizeof(value), sizeof(value));
        return apta_clamp_f32(value);
    }
    default:
        return 0.0f;
    }
}

static float apta_read_mono_sample(
    const apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t frame_index)
{
    float left;
    float right;

    left = apta_read_channel_sample(session, block, frame_index, 0u);
    if (session->config.channel_count == 1u) {
        return left;
    }

    right = apta_read_channel_sample(session, block, frame_index, 1u);
    return (left + right) * 0.5f;
}

static apta_status_t apta_ensure_range_capacity(
    apta_session_t *session,
    uint32_t needed)
{
    apta_internal_range_t *replacement;
    uint32_t capacity;
    size_t bytes;

    if (session->accepted_range_capacity >= needed) {
        return APTA_STATUS_OK;
    }

    capacity = session->accepted_range_capacity == 0u
                   ? 8u
                   : session->accepted_range_capacity * 2u;
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

    replacement = (apta_internal_range_t *)apta_internal_context_allocate(
        session->context,
        bytes,
        alignof(apta_internal_range_t),
        APTA_MEMORY_PERSISTENT);
    if (replacement == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    if (session->accepted_range_count != 0u) {
        memcpy(
            replacement,
            session->accepted_ranges,
            (size_t)session->accepted_range_count * sizeof(*replacement));
    }

    apta_internal_context_deallocate(session->context, session->accepted_ranges);
    session->accepted_ranges = replacement;
    session->accepted_range_capacity = capacity;
    return APTA_STATUS_OK;
}

static uint32_t apta_nonoverlapping_prefix(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    uint32_t requested_frames,
    int *conflict_at_start_out)
{
    apta_source_frame_t requested_end;
    uint32_t index;

    *conflict_at_start_out = 0;
    requested_end = first_frame + (uint64_t)requested_frames;

    for (index = 0u; index < session->accepted_range_count; ++index) {
        const apta_internal_range_t *range = &session->accepted_ranges[index];

        if (range->end_frame <= first_frame) {
            continue;
        }
        if (range->first_frame <= first_frame) {
            *conflict_at_start_out = 1;
            return 0u;
        }
        if (range->first_frame < requested_end) {
            return (uint32_t)(range->first_frame - first_frame);
        }
        break;
    }

    return requested_frames;
}

static void apta_insert_accepted_range(
    apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame)
{
    uint32_t position;
    uint32_t index;

    position = 0u;
    while (position < session->accepted_range_count &&
           session->accepted_ranges[position].first_frame < first_frame) {
        position += 1u;
    }

    for (index = session->accepted_range_count; index > position; --index) {
        session->accepted_ranges[index] = session->accepted_ranges[index - 1u];
    }

    session->accepted_ranges[position].first_frame = first_frame;
    session->accepted_ranges[position].end_frame = end_frame;
    session->accepted_range_count += 1u;

    if (position > 0u &&
        session->accepted_ranges[position - 1u].end_frame == first_frame) {
        session->accepted_ranges[position - 1u].end_frame = end_frame;
        for (index = position; index + 1u < session->accepted_range_count; ++index) {
            session->accepted_ranges[index] = session->accepted_ranges[index + 1u];
        }
        session->accepted_range_count -= 1u;
        position -= 1u;
    }

    if (position + 1u < session->accepted_range_count &&
        session->accepted_ranges[position].end_frame ==
            session->accepted_ranges[position + 1u].first_frame) {
        session->accepted_ranges[position].end_frame =
            session->accepted_ranges[position + 1u].end_frame;
        for (index = position + 1u;
             index + 1u < session->accepted_range_count;
             ++index) {
            session->accepted_ranges[index] = session->accepted_ranges[index + 1u];
        }
        session->accepted_range_count -= 1u;
    }

    if (end_frame > session->greatest_accepted_end) {
        session->greatest_accepted_end = end_frame;
    }
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
                   : session->overview_accumulator_capacity * 2u;
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

        if ((request->request.feature_mask & APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
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

    best = session->pcm_head;
    best_previous = NULL;
    best_priority = 0u;

    focus_first = 0u;
    focus_end = 0u;
    if (session->has_focus &&
        (session->focus.feature_mask & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
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
        if (focus_end > focus_first) {
            best_priority = session->focus.priority;
        }
    }

    previous = NULL;
    for (node = session->pcm_head; node != NULL; node = node->next) {
        apta_source_frame_t node_first =
            node->first_frame + node->processed_frames;
        apta_source_frame_t node_end = node->first_frame + node->frame_count;
        uint32_t priority = 0u;

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

apta_status_t apta_internal_waveform_accept_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out)
{
    apta_internal_pcm_node_t *node;
    apta_status_t status;
    uint32_t candidate;
    uint32_t accepted;
    uint32_t frame;
    int conflict_at_start;
    size_t bytes;

    *accepted_frames_out = 0u;

    candidate = apta_min_u32(block->frame_count, APTA_INTERNAL_MAX_PUSH_FRAMES);
    accepted = apta_nonoverlapping_prefix(
        session,
        block->first_frame,
        candidate,
        &conflict_at_start);

    if (accepted == 0u) {
        return conflict_at_start ? APTA_ERROR_CONFLICT : APTA_STATUS_WOULD_BLOCK;
    }

    status = apta_ensure_range_capacity(
        session,
        session->accepted_range_count + 1u);
    if (status < 0) {
        return status == APTA_ERROR_OUT_OF_MEMORY
                   ? APTA_STATUS_WOULD_BLOCK
                   : status;
    }

    node = NULL;
    while (accepted != 0u) {
        if ((size_t)accepted >
            (SIZE_MAX - sizeof(*node)) / sizeof(node->samples[0])) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        bytes = sizeof(*node) + (size_t)accepted * sizeof(node->samples[0]);
        node = (apta_internal_pcm_node_t *)apta_internal_context_allocate(
            session->context,
            bytes,
            alignof(apta_internal_pcm_node_t),
            APTA_MEMORY_LARGE);
        if (node != NULL) {
            break;
        }
        accepted /= 2u;
    }

    if (node == NULL) {
        return APTA_STATUS_WOULD_BLOCK;
    }

    memset(node, 0, sizeof(*node));
    node->first_frame = block->first_frame;
    node->frame_count = accepted;

    for (frame = 0u; frame < accepted; ++frame) {
        node->samples[frame] = apta_read_mono_sample(session, block, frame);
    }

    apta_insert_accepted_range(
        session,
        block->first_frame,
        block->first_frame + accepted);

    if (session->pcm_tail != NULL) {
        session->pcm_tail->next = node;
    } else {
        session->pcm_head = node;
    }
    session->pcm_tail = node;
    session->queued_pcm_frames += accepted;

    *accepted_frames_out = accepted;
    return accepted == block->frame_count
               ? APTA_STATUS_OK
               : APTA_STATUS_MORE_WORK;
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
            uint64_t now = session->context->clock.monotonic_time_ns(
                session->context->clock.user_data);
            uint64_t budget_ns = (uint64_t)budget->soft_time_budget_us * 1000u;
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
            *published_output_out ? APTA_FEATURE_WAVEFORM_OVERVIEW : 0u;
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
        (session->focus.feature_mask & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
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
            target_end = target_first + APTA_INTERNAL_MAX_PUSH_FRAMES;
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

apta_status_t apta_internal_waveform_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    uint32_t complete_count;
    uint32_t span_count;
    uint32_t index;
    uint32_t output_index;
    uint32_t span_index;
    uint32_t previous_column;
    uint32_t span_output_start;
    int have_previous;
    size_t column_bytes;
    size_t span_bytes;
    int full_coverage;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u ||
        session->overview_complete_count == 0u) {
        return APTA_STATUS_OK;
    }

    complete_count = session->overview_complete_count;
    span_count = 0u;
    have_previous = 0;
    previous_column = 0u;

    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        const apta_internal_waveform_accumulator_t *accumulator =
            &session->overview_accumulators[index];
        if (!accumulator->complete) {
            continue;
        }
        if (!have_previous || accumulator->column_index != previous_column + 1u) {
            span_count += 1u;
        }
        previous_column = accumulator->column_index;
        have_previous = 1;
    }

    if ((size_t)complete_count > SIZE_MAX / sizeof(*result->overview_columns) ||
        (size_t)span_count > SIZE_MAX / sizeof(*result->overview_spans)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    column_bytes = (size_t)complete_count * sizeof(*result->overview_columns);
    span_bytes = (size_t)span_count * sizeof(*result->overview_spans);

    result->overview_columns =
        (apta_waveform_column_t *)apta_internal_context_allocate(
            session->context,
            column_bytes,
            alignof(apta_waveform_column_t),
            APTA_MEMORY_PERSISTENT);
    if (result->overview_columns == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    result->overview_spans =
        (apta_waveform_span_t *)apta_internal_context_allocate(
            session->context,
            span_bytes,
            alignof(apta_waveform_span_t),
            APTA_MEMORY_PERSISTENT);
    if (result->overview_spans == NULL) {
        apta_internal_context_deallocate(
            session->context,
            result->overview_columns);
        result->overview_columns = NULL;
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(result->overview_columns, 0, column_bytes);
    memset(result->overview_spans, 0, span_bytes);

    output_index = 0u;
    span_index = 0u;
    have_previous = 0;
    previous_column = 0u;
    span_output_start = 0u;

    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        const apta_internal_waveform_accumulator_t *accumulator =
            &session->overview_accumulators[index];
        apta_waveform_column_t *column;

        if (!accumulator->complete) {
            continue;
        }

        if (!have_previous || accumulator->column_index != previous_column + 1u) {
            if (have_previous) {
                apta_waveform_span_t *previous_span =
                    &result->overview_spans[span_index - 1u];
                apta_source_frame_t end_frame =
                    ((apta_source_frame_t)previous_column + 1u) *
                    session->overview_frames_per_column;
                if (session->end_of_input_signalled) {
                    end_frame = apta_min_frame(end_frame, session->final_end_frame);
                }
                previous_span->source_range.end_frame = end_frame;
                previous_span->column_count = output_index - span_output_start;
            }

            span_output_start = output_index;
            result->overview_spans[span_index].source_range.struct_size =
                (uint32_t)sizeof(result->overview_spans[span_index].source_range);
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
        column->minimum = apta_quantize_peak(accumulator->minimum);
        column->maximum = apta_quantize_peak(accumulator->maximum);
        column->rms = apta_quantize_rms(
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
        apta_waveform_span_t *last_span = &result->overview_spans[span_index - 1u];
        apta_source_frame_t end_frame =
            ((apta_source_frame_t)previous_column + 1u) *
            session->overview_frames_per_column;
        if (session->end_of_input_signalled) {
            end_frame = apta_min_frame(end_frame, session->final_end_frame);
        }
        last_span->source_range.end_frame = end_frame;
        last_span->column_count = output_index - span_output_start;
    }

    memset(&result->overview, 0, sizeof(result->overview));
    result->overview.struct_size = (uint32_t)sizeof(result->overview);
    result->overview.api_version = APTA_API_VERSION;
    result->overview.level.struct_size = (uint32_t)sizeof(result->overview.level);
    result->overview.level.api_version = APTA_API_VERSION;
    result->overview.level.level_id = 0u;
    result->overview.level.frames_per_column =
        session->overview_frames_per_column;
    result->overview.level.origin_frame = 0u;
    result->overview.confidence = APTA_CONFIDENCE_UNKNOWN;
    result->overview.span_count = span_count;
    result->overview.spans = result->overview_spans;

    full_coverage = 0;
    if (session->end_of_input_signalled && span_count == 1u &&
        result->overview_spans[0].source_range.first_frame == 0u &&
        result->overview_spans[0].source_range.end_frame ==
            session->final_end_frame) {
        full_coverage = 1;
    }

    result->overview.state =
        full_coverage && atomic_load_explicit(
                             &session->state,
                             memory_order_acquire) == APTA_SESSION_COMPLETED
            ? APTA_FEATURE_FINAL
            : (full_coverage ? APTA_FEATURE_STABLE : APTA_FEATURE_PARTIAL);

    result->info.available_features |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    return APTA_STATUS_OK;
}

void apta_internal_waveform_cleanup_session(apta_session_t *session)
{
    apta_internal_pcm_node_t *node;

    if (session == NULL) {
        return;
    }

    node = session->pcm_head;
    while (node != NULL) {
        apta_internal_pcm_node_t *next = node->next;
        apta_internal_context_deallocate(session->context, node);
        node = next;
    }

    apta_internal_context_deallocate(session->context, session->accepted_ranges);
    apta_internal_context_deallocate(
        session->context,
        session->overview_accumulators);

    session->pcm_head = NULL;
    session->pcm_tail = NULL;
    session->accepted_ranges = NULL;
    session->overview_accumulators = NULL;
}

void apta_internal_waveform_cleanup_result(apta_result_t *result)
{
    if (result == NULL) {
        return;
    }

    apta_internal_context_deallocate(result->context, result->overview_spans);
    apta_internal_context_deallocate(result->context, result->overview_columns);
    result->overview_spans = NULL;
    result->overview_columns = NULL;
}
