// SPDX-License-Identifier: Apache-2.0
#include "apta_waveform_detail_internal.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

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

static int apta_detail_column_is_empty(
    const apta_session_t *session,
    uint64_t global_column)
{
    const uint64_t tile64 =
        global_column / APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE;
    const uint32_t local_column = (uint32_t)(
        global_column % APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE);
    const apta_internal_detail_tile_t *tile;

    if (tile64 > UINT32_MAX) {
        return 0;
    }

    tile = apta_detail_find_tile(session, (uint32_t)tile64);
    return tile == NULL ||
           tile->accumulators[local_column].sample_count == 0u;
}

static int apta_detail_select_target(
    const apta_session_t *session,
    apta_source_frame_t *first_out,
    apta_source_frame_t *end_out,
    uint8_t *priority_out,
    uint32_t *token_out)
{
    apta_source_frame_t first = 0u;
    apta_source_frame_t end = 0u;
    uint8_t priority = APTA_PRIORITY_BACKGROUND;
    uint32_t token = 0u;
    int have_target = 0;
    uint32_t slot;

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        const apta_internal_request_t *candidate = &session->requests[slot];

        if (candidate->request_id == 0u ||
            candidate->state == APTA_REQUEST_CANCELLED ||
            candidate->state == APTA_REQUEST_FAILED ||
            candidate->state == APTA_REQUEST_SATISFIED ||
            (candidate->request.feature_mask &
             APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
            continue;
        }

        if (!have_target || candidate->request.priority > priority) {
            first = candidate->request.range.first_frame;
            end = candidate->request.range.end_frame;
            priority = candidate->request.priority;
            token = candidate->request_id;
            have_target = 1;
        }
    }

    if (!have_target && session->has_focus &&
        (session->focus.feature_mask & APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
        first = session->focus.playhead_frame >
                        session->focus.lookbehind_frames
                    ? session->focus.playhead_frame -
                          session->focus.lookbehind_frames
                    : 0u;
        end = session->focus.playhead_frame;
        if (UINT64_MAX - end < session->focus.lookahead_frames) {
            end = UINT64_MAX;
        } else {
            end += session->focus.lookahead_frames;
        }
        priority = session->focus.priority;
        token = 0u;
        have_target = 1;
    }

    if (!have_target) {
        return 0;
    }

    if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        end > session->config.total_frames) {
        end = session->config.total_frames;
    }
    if (end <= first) {
        return 0;
    }

    *first_out = first;
    *end_out = end;
    *priority_out = priority;
    *token_out = token;
    return 1;
}

apta_status_t apta_internal_detail_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out)
{
    apta_source_frame_t target_first;
    apta_source_frame_t target_end;
    uint8_t priority;
    uint32_t token;
    uint64_t first_column;
    uint64_t end_column;
    uint64_t column;
    uint64_t request_first_column;
    uint64_t request_end_column;
    const uint64_t maximum_columns =
        APTA_INTERNAL_MAX_PUSH_FRAMES /
        APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    if (!apta_detail_select_target(
            session,
            &target_first,
            &target_end,
            &priority,
            &token)) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    first_column =
        target_first / APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
    end_column =
        (target_end + APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN - 1u) /
        APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;

    request_first_column = UINT64_MAX;
    for (column = first_column; column < end_column; ++column) {
        if (apta_detail_column_is_empty(session, column)) {
            request_first_column = column;
            break;
        }
    }
    if (request_first_column == UINT64_MAX) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    request_end_column = request_first_column;
    while (request_end_column < end_column &&
           request_end_column - request_first_column < maximum_columns &&
           apta_detail_column_is_empty(session, request_end_column)) {
        request_end_column += 1u;
    }

    if (request_first_column >
        UINT64_MAX / APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN ||
        request_end_column >
        UINT64_MAX / APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    request_out->range.first_frame =
        request_first_column * APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
    request_out->range.end_frame =
        request_end_column * APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN;
    if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        request_out->range.end_frame > session->config.total_frames) {
        request_out->range.end_frame = session->config.total_frames;
    }
    if (request_out->range.end_frame <= request_out->range.first_frame) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    request_out->feature_mask = APTA_FEATURE_WAVEFORM_DETAIL;
    request_out->priority = priority;
    request_out->request_token = token;
    return APTA_STATUS_OK;
}

/* A3: per-sample conversion. See apta_normalize_s16() in
 * apta_waveform_input.c for why float is bit-identical here. */
static float apta_detail_normalize_s16(int16_t value)
{
    if (value < 0) {
        return (float)value / 32768.0f;
    }
    return value == 0 ? 0.0f : (float)value / 32767.0f;
}

static float apta_detail_normalize_s24(const uint8_t *bytes)
{
    int32_t value = (int32_t)((uint32_t)bytes[0] |
                              ((uint32_t)bytes[1] << 8u) |
                              ((uint32_t)bytes[2] << 16u));

    if ((value & 0x00800000) != 0) {
        value |= (int32_t)0xFF000000;
    }
    if (value < 0) {
        return (float)value / 8388608.0f;
    }
    return value == 0 ? 0.0f : (float)value / 8388607.0f;
}

static float apta_detail_normalize_s32(int32_t value)
{
    if (value < 0) {
        return (float)value / 2147483648.0f;
    }
    /* Retained in double: see apta_normalize_s32() in apta_waveform_input.c. */
    return value == 0 ? 0.0f : (float)((double)value / 2147483647.0);
}

static float apta_detail_clamp_f32(float value)
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

static float apta_detail_read_channel_sample(
    const apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t frame_index,
    uint32_t channel)
{
    const uint32_t sample_index =
        frame_index * (uint32_t)session->config.channel_count + channel;

    switch (session->config.sample_format) {
    case APTA_SAMPLE_S16_NATIVE_INTERLEAVED: {
        int16_t value;
        const uint8_t *bytes = (const uint8_t *)block->data;
        memcpy(
            &value,
            bytes + (size_t)sample_index * sizeof(value),
            sizeof(value));
        return apta_detail_normalize_s16(value);
    }
    case APTA_SAMPLE_S24_3LE_INTERLEAVED: {
        const uint8_t *bytes = (const uint8_t *)block->data;
        return apta_detail_normalize_s24(
            bytes + (size_t)sample_index * 3u);
    }
    case APTA_SAMPLE_S32_NATIVE_INTERLEAVED: {
        int32_t value;
        const uint8_t *bytes = (const uint8_t *)block->data;
        memcpy(
            &value,
            bytes + (size_t)sample_index * sizeof(value),
            sizeof(value));
        return apta_detail_normalize_s32(value);
    }
    case APTA_SAMPLE_F32_NATIVE_INTERLEAVED: {
        float value;
        const uint8_t *bytes = (const uint8_t *)block->data;
        memcpy(
            &value,
            bytes + (size_t)sample_index * sizeof(value),
            sizeof(value));
        return apta_detail_clamp_f32(value);
    }
    case APTA_SAMPLE_F32_NATIVE_PLANAR: {
        float value;
        const uint8_t *bytes = (const uint8_t *)block->planes[channel];
        memcpy(
            &value,
            bytes + (size_t)frame_index * sizeof(value),
            sizeof(value));
        return apta_detail_clamp_f32(value);
    }
    default:
        return 0.0f;
    }
}

static float apta_detail_read_mono_sample(
    const apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t frame_index)
{
    const float left = apta_detail_read_channel_sample(
        session,
        block,
        frame_index,
        0u);

    if (session->config.channel_count == 1u) {
        return left;
    }

    return (left +
            apta_detail_read_channel_sample(
                session,
                block,
                frame_index,
                1u)) *
           0.5f;
}

apta_status_t apta_internal_detail_accept_replay(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out)
{
    apta_pcm_request_t request;
    uint64_t requested_frames;
    uint32_t accepted;
    uint32_t frame;
    apta_status_t status;

    *accepted_frames_out = 0u;
    apta_pcm_request_init(&request);
    status = apta_internal_detail_next_pcm_request(session, &request);
    if (status != APTA_STATUS_OK) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    requested_frames = request.range.end_frame - request.range.first_frame;
    if (block->first_frame != request.range.first_frame ||
        block->frame_count == 0u ||
        (uint64_t)block->frame_count > requested_frames) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    accepted = block->frame_count;
    if (accepted > APTA_INTERNAL_MAX_PUSH_FRAMES) {
        accepted = APTA_INTERNAL_MAX_PUSH_FRAMES;
    }

    for (frame = 0u; frame < accepted; ++frame) {
        status = apta_internal_detail_process_sample(
            session,
            block->first_frame + frame,
            apta_detail_read_mono_sample(session, block, frame));
        if (status < 0) {
            return status;
        }
    }

    apta_internal_detail_refresh_completed(session);
    *accepted_frames_out = accepted;
    return accepted == block->frame_count
               ? APTA_STATUS_OK
               : APTA_STATUS_MORE_WORK;
}
