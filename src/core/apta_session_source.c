// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <stdint.h>

static int apta_source_format_is_planar(apta_sample_format_t format)
{
    return format == APTA_SAMPLE_F32_NATIVE_PLANAR;
}

static apta_status_t apta_source_validate_pcm_block(
    const apta_session_t *session,
    const apta_pcm_block_t *block)
{
    uint32_t channel;
    apta_source_frame_t end_frame;

    if (!apta_internal_validate_struct(
            block,
            sizeof(*block),
            block->struct_size,
            block->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (block->frame_count == 0u ||
        block->first_frame == APTA_TOTAL_FRAMES_UNKNOWN ||
        block->first_frame > UINT64_MAX - (uint64_t)block->frame_count) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    end_frame = block->first_frame + (uint64_t)block->frame_count;

    if (session->end_of_input_signalled &&
        end_frame > session->final_end_frame) {
        return APTA_ERROR_CONFLICT;
    }

    if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        end_frame > session->config.total_frames) {
        return APTA_ERROR_CONFLICT;
    }

    if (apta_source_format_is_planar(session->config.sample_format)) {
        if (block->data != NULL) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }

        for (channel = 0u; channel < session->config.channel_count; ++channel) {
            if (block->planes[channel] == NULL) {
                return APTA_ERROR_INVALID_ARGUMENT;
            }
        }
        for (; channel < 8u; ++channel) {
            if (block->planes[channel] != NULL) {
                return APTA_ERROR_INVALID_ARGUMENT;
            }
        }
    } else {
        if (block->data == NULL) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        for (channel = 0u; channel < 8u; ++channel) {
            if (block->planes[channel] != NULL) {
                return APTA_ERROR_INVALID_ARGUMENT;
            }
        }
    }

    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_set_source(
    apta_session_t *session,
    const apta_pcm_source_t *source)
{
    if (session == NULL || source == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (session->config.input_mode != APTA_INPUT_MODE_PULL) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (atomic_load_explicit(&session->state, memory_order_acquire) !=
        APTA_SESSION_CREATED) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (!apta_internal_validate_struct(
            source,
            sizeof(*source),
            source->struct_size,
            source->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (source->read_frames == NULL || source->release_frames == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    session->pull_source = *source;
    session->has_pull_source = 1u;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_push_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out)
{
    apta_session_state_t state;
    apta_status_t status;

    if (accepted_frames_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *accepted_frames_out = 0u;

    if (session == NULL || block == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (session->config.input_mode != APTA_INPUT_MODE_PUSH) {
        return APTA_ERROR_INVALID_STATE;
    }

    state = atomic_load_explicit(&session->state, memory_order_acquire);
    if (state == APTA_SESSION_CANCELLED) {
        return APTA_ERROR_CANCELLED;
    }
    if (state == APTA_SESSION_DRAINING ||
        state == APTA_SESSION_COMPLETED ||
        state == APTA_SESSION_FAILED) {
        return APTA_ERROR_INVALID_STATE;
    }

    status = apta_source_validate_pcm_block(session, block);
    if (status < 0) {
        return status;
    }

    if (state == APTA_SESSION_CREATED) {
        status = apta_internal_session_transition(
            session,
            APTA_SESSION_ACTIVE);
        if (status < 0) {
            return status;
        }
    }

    /* M1 validates lifecycle and ownership but has no enabled analyser yet. */
    *accepted_frames_out = block->frame_count;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_signal_end_of_input(
    apta_session_t *session,
    apta_source_frame_t final_end_frame)
{
    apta_session_state_t state;
    apta_source_frame_t old_total;
    apta_source_frame_t old_final;
    uint32_t old_signalled;
    apta_status_t status;

    if (session == NULL || final_end_frame == APTA_TOTAL_FRAMES_UNKNOWN) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (session->config.input_mode != APTA_INPUT_MODE_PUSH) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (session->end_of_input_signalled) {
        return session->final_end_frame == final_end_frame
                   ? APTA_STATUS_OK
                   : APTA_ERROR_CONFLICT;
    }

    if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        session->config.total_frames != final_end_frame) {
        return APTA_ERROR_CONFLICT;
    }

    state = atomic_load_explicit(&session->state, memory_order_acquire);
    if (state == APTA_SESSION_COMPLETED ||
        state == APTA_SESSION_CANCELLED ||
        state == APTA_SESSION_FAILED ||
        state == APTA_SESSION_DRAINING) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (state == APTA_SESSION_CREATED) {
        status = apta_internal_session_transition(
            session,
            APTA_SESSION_ACTIVE);
        if (status < 0) {
            return status;
        }
    }

    old_total = session->config.total_frames;
    old_final = session->final_end_frame;
    old_signalled = session->end_of_input_signalled;

    session->config.total_frames = final_end_frame;
    session->final_end_frame = final_end_frame;
    session->end_of_input_signalled = 1u;

    status = apta_internal_session_transition(
        session,
        APTA_SESSION_DRAINING);
    if (status < 0) {
        session->config.total_frames = old_total;
        session->final_end_frame = old_final;
        session->end_of_input_signalled = old_signalled;
    }

    return status;
}
