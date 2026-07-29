// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_session_workspace.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

static uint32_t apta_min_u32(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static float apta_normalize_s16(int16_t value)
{
    if (value < 0) {
        return (float)((double)value / 32768.0);
    }
    return value == 0 ? 0.0f : (float)((double)value / 32767.0);
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

    if (value < 0) {
        return (float)((double)value / 8388608.0);
    }
    return value == 0 ? 0.0f : (float)((double)value / 8388607.0);
}

static float apta_normalize_s32(int32_t value)
{
    if (value < 0) {
        return (float)((double)value / 2147483648.0);
    }
    return value == 0 ? 0.0f : (float)((double)value / 2147483647.0);
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
                   : session->accepted_range_capacity;
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

    replacement = (apta_internal_range_t *)apta_internal_session_allocate(
        session,
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

static void apta_refresh_accepted_ends(apta_session_t *session)
{
    apta_source_frame_t contiguous_end;
    uint32_t index;

    contiguous_end = 0u;
    for (index = 0u; index < session->accepted_range_count; ++index) {
        const apta_internal_range_t *range = &session->accepted_ranges[index];

        if (range->first_frame > contiguous_end) {
            break;
        }
        if (range->end_frame > contiguous_end) {
            contiguous_end = range->end_frame;
        }
    }

    session->greatest_accepted_end = contiguous_end;
    session->maximum_accepted_end =
        session->accepted_range_count == 0u
            ? 0u
            : session->accepted_ranges[session->accepted_range_count - 1u].end_frame;
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

    apta_refresh_accepted_ends(session);
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
        if (!apta_internal_size_array_fits(
                sizeof(*node),
                accepted,
                sizeof(node->samples[0]))) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }

        bytes = sizeof(*node) + (size_t)accepted * sizeof(node->samples[0]);
        node = (apta_internal_pcm_node_t *)apta_internal_session_allocate(
            session,
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
