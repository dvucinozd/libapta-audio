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

/* A3: these run once per source sample per channel, ahead of every other
 * per-sample path, so they must not use double on a target without hardware
 * double. For 16- and 24-bit input both the value and the divisor are exactly
 * representable in float, so a single float division is correctly rounded and
 * produces bit-identical results to the previous double form. */
static float apta_normalize_s16(int16_t value)
{
    if (value < 0) {
        return (float)value / 32768.0f;
    }
    return value == 0 ? 0.0f : (float)value / 32767.0f;
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
        return (float)value / 8388608.0f;
    }
    return value == 0 ? 0.0f : (float)value / 8388607.0f;
}

static float apta_normalize_s32(int32_t value)
{
    if (value < 0) {
        /* Divisor is 2^31: scaling by a power of two is exact, so converting
         * first and scaling second gives the same result as the double form. */
        return (float)value / 2147483648.0f;
    }
    /* Retained in double deliberately: a 32-bit magnitude exceeds float's
     * 24-bit mantissa, so (float)value would discard bits before the division.
     * This is the one per-sample double left, and it is reachable only for
     * APTA_SAMPLE_S32 input. */
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

/*
 * D1: seed a fresh session's overview coverage from a parsed result.
 *
 * The accepted-range table is already the right representation for "these
 * ranges are done", so seeding is a matter of populating it and the overview
 * accumulators from the parsed columns rather than from accepted PCM.
 *
 * Columns are quantized in the container, so the reconstruction is exact only
 * to that quantization: minimum and maximum invert the int16 peak scaling and
 * rms inverts the uint16 scaling. A seeded column therefore reproduces the
 * parsed column, not the original float accumulator state.
 *
 * S4 and S6 are deliberately not seeded. The parsed result carries a published
 * tempo and grid but not the onset timeline they were derived from, so there
 * is nothing to resume from; the engines rebuild their own evidence from the
 * PCM that follows. Seeding waveform coverage while letting the onset engines
 * restart is the honest split.
 */
apta_status_t APTA_CALL apta_session_seed_from_result(
    apta_session_t *session,
    const apta_result_t *result)
{
    apta_result_info_t info;
    apta_waveform_overview_view_t overview;
    apta_status_t status;
    uint32_t span;

    if (session == NULL || result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (atomic_flag_test_and_set_explicit(
            &session->process_lock,
            memory_order_acquire)) {
        return APTA_ERROR_BUSY;
    }
    if (atomic_load_explicit(&session->state, memory_order_acquire) !=
        APTA_SESSION_CREATED) {
        status = APTA_ERROR_INVALID_STATE;
        goto done;
    }

    apta_result_info_init(&info);
    status = apta_result_get_info(result, &info);
    if (status < 0) {
        goto done;
    }
    if ((info.available_features & APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
        status = APTA_ERROR_CONFLICT;
        goto done;
    }

    apta_waveform_overview_view_init(&overview);
    status = apta_result_get_waveform_overview(result, 0u, &overview);
    if (status < 0) {
        goto done;
    }

    /*
     * Compatibility. The work order asks for source_sample_rate, channel_count
     * and total_frames to be checked against the session config as well, but a
     * parsed result carries none of the three: apta_result_info_t has no such
     * fields and no container section records them. Only the column geometry
     * and the coverage extent can be validated here. The caller has to
     * guarantee the rest -- documented in docs/api/APTA-SESSION-SEEDING-0.1.md.
     */
    if (overview.level.frames_per_column !=
        session->overview_frames_per_column) {
        status = APTA_ERROR_CONFLICT;
        goto done;
    }
    for (span = 0u; span < overview.span_count; ++span) {
        if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
            overview.spans[span].source_range.end_frame >
                session->config.total_frames) {
            status = APTA_ERROR_CONFLICT;
            goto done;
        }
    }

    for (span = 0u; span < overview.span_count; ++span) {
        const apta_waveform_span_t *view = &overview.spans[span];
        uint32_t column;

        for (column = 0u; column < view->column_count; ++column) {
            const apta_waveform_column_t *source = &view->columns[column];
            const uint32_t column_index = view->first_column_index + column;
            const apta_source_frame_t column_first =
                (apta_source_frame_t)column_index *
                session->overview_frames_per_column;
            apta_source_frame_t column_end;
            uint32_t sample_count;
            float rms;
            uint64_t scaled;

            if ((source->flags & APTA_WAVEFORM_COLUMN_VALID) == 0u) {
                continue;
            }

            column_end = column_first +
                         (apta_source_frame_t)session->overview_frames_per_column;
            if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
                column_end > session->config.total_frames) {
                column_end = session->config.total_frames;
            }
            if (column_end <= column_first) {
                continue;
            }
            sample_count = (uint32_t)(column_end - column_first);

            /* Invert the container quantization. */
            rms = (float)source->rms / 65535.0f;
            scaled = (uint64_t)(rms * APTA_INTERNAL_SQUARE_MAGNITUDE_SCALE);

            status = apta_internal_waveform_seed_column(
                session,
                column_index,
                (float)source->minimum / 32767.0f,
                (float)source->maximum / 32767.0f,
                scaled * scaled * (uint64_t)sample_count,
                sample_count,
                (source->flags & APTA_WAVEFORM_COLUMN_CLIPPED) != 0u);
            if (status < 0) {
                goto done;
            }
        }

        status = apta_ensure_range_capacity(
            session,
            session->accepted_range_count + 1u);
        if (status < 0) {
            goto done;
        }
        apta_insert_accepted_range(
            session,
            view->source_range.first_frame,
            view->source_range.end_frame);
    }

    status = APTA_STATUS_OK;

done:
    atomic_flag_clear_explicit(&session->process_lock, memory_order_release);
    return status;
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
