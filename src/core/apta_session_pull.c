// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <stdint.h>

static uint32_t apta_pull_min_u32(uint32_t left, uint32_t right)
{
    return left < right ? left : right;
}

static apta_status_t apta_pull_source_failure(apta_session_t *session)
{
    apta_status_t transition_status;

    transition_status = apta_internal_session_transition(
        session,
        APTA_SESSION_FAILED);
    return transition_status < 0
               ? transition_status
               : APTA_ERROR_SOURCE;
}

apta_status_t apta_internal_pull_pcm_before_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    uint32_t *pulled_frames_out)
{
    apta_pcm_request_t request;
    apta_pcm_block_t block;
    apta_session_state_t state;
    apta_status_t status;
    apta_status_t accept_status;
    uint64_t request_frames64;
    uint32_t request_frames;
    uint32_t accepted_frames;

    if (session == NULL || budget == NULL || pulled_frames_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *pulled_frames_out = 0u;

    if (session->config.input_mode != APTA_INPUT_MODE_PULL) {
        return APTA_STATUS_OK;
    }
    if (!session->has_pull_source) {
        return APTA_ERROR_INVALID_STATE;
    }

    state = atomic_load_explicit(&session->state, memory_order_acquire);
    if (state == APTA_SESSION_DRAINING ||
        state == APTA_SESSION_COMPLETED) {
        return APTA_STATUS_OK;
    }
    if (state == APTA_SESSION_CANCELLED) {
        return APTA_ERROR_CANCELLED;
    }
    if (state == APTA_SESSION_FAILED) {
        return APTA_ERROR_SOURCE;
    }

    /* Process already-owned PCM before asking the host for another block. */
    if (session->pcm_head != NULL) {
        return APTA_STATUS_OK;
    }

    apta_pcm_request_init(&request);
    status = apta_internal_waveform_next_pcm_request(session, &request);
    if (status == APTA_STATUS_NOT_AVAILABLE) {
        if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN) {
            return apta_internal_session_signal_end_of_input(
                session,
                session->config.total_frames);
        }
        return APTA_STATUS_WOULD_BLOCK;
    }
    if (status < 0) {
        return status;
    }
    if (status != APTA_STATUS_OK ||
        request.range.end_frame <= request.range.first_frame) {
        return apta_pull_source_failure(session);
    }

    request_frames64 = request.range.end_frame - request.range.first_frame;
    if (request_frames64 > UINT32_MAX) {
        return apta_pull_source_failure(session);
    }
    request_frames = (uint32_t)request_frames64;
    if (budget->maximum_input_frames != 0u) {
        request_frames = apta_pull_min_u32(
            request_frames,
            budget->maximum_input_frames);
    }
    if (request_frames == 0u) {
        return APTA_STATUS_WOULD_BLOCK;
    }

    apta_pcm_block_init(&block);
    status = session->pull_source.read_frames(
        session->pull_source.user_data,
        request.range.first_frame,
        request_frames,
        &block);

    if (status == APTA_STATUS_WOULD_BLOCK) {
        return APTA_STATUS_WOULD_BLOCK;
    }

    if (status == APTA_STATUS_END_OF_INPUT) {
        if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
            request.range.first_frame != session->config.total_frames) {
            return apta_pull_source_failure(session);
        }
        return apta_internal_session_signal_end_of_input(
            session,
            request.range.first_frame);
    }

    if (status != APTA_STATUS_OK) {
        return apta_pull_source_failure(session);
    }

    accept_status = apta_internal_source_validate_pcm_block(session, &block);
    if (accept_status >= 0 &&
        (block.first_frame != request.range.first_frame ||
         block.frame_count > request_frames ||
         block.first_frame > UINT64_MAX - (uint64_t)block.frame_count ||
         block.first_frame + (uint64_t)block.frame_count >
             request.range.end_frame)) {
        accept_status = APTA_ERROR_SOURCE;
    }

    if (accept_status >= 0 && state == APTA_SESSION_CREATED) {
        accept_status = apta_internal_session_transition(
            session,
            APTA_SESSION_ACTIVE);
    }

    accepted_frames = 0u;
    if (accept_status >= 0 &&
        (session->config.requested_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
        accept_status = apta_internal_waveform_accept_pcm(
            session,
            &block,
            &accepted_frames);
    }

    session->pull_source.release_frames(
        session->pull_source.user_data,
        &block);

    if (accept_status < 0) {
        if (accept_status == APTA_ERROR_RESULT_SLOTS_EXHAUSTED ||
            accept_status == APTA_ERROR_OUT_OF_MEMORY ||
            accept_status == APTA_ERROR_LIMIT_EXCEEDED) {
            return accept_status;
        }
        return apta_pull_source_failure(session);
    }

    *pulled_frames_out = accepted_frames;
    return APTA_STATUS_OK;
}
