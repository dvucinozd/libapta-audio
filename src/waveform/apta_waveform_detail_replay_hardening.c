// SPDX-License-Identifier: Apache-2.0
#include "apta_waveform_detail_internal.h"

apta_status_t apta_internal_detail_next_pcm_request_base(
    apta_session_t *session,
    apta_pcm_request_t *request_out);

apta_status_t apta_internal_detail_accept_replay_base(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

apta_status_t apta_internal_detail_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out)
{
    const apta_source_frame_t maximum_safe_end =
        UINT64_MAX -
        (APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN - 1u);
    uint64_t saved_focus_lookahead;
    apta_source_frame_t saved_request_ends[APTA_INTERNAL_MAX_REGION_REQUESTS];
    uint32_t slot;
    apta_status_t status;

    if (session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    saved_focus_lookahead = session->focus.lookahead_frames;
    if (session->has_focus &&
        session->focus.playhead_frame < maximum_safe_end &&
        session->focus.lookahead_frames >
            maximum_safe_end - session->focus.playhead_frame) {
        session->focus.lookahead_frames =
            maximum_safe_end - session->focus.playhead_frame;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        saved_request_ends[slot] =
            session->requests[slot].request.range.end_frame;
        if (session->requests[slot].request.range.end_frame >
            maximum_safe_end) {
            session->requests[slot].request.range.end_frame =
                maximum_safe_end;
        }
    }

    status = apta_internal_detail_next_pcm_request_base(
        session,
        request_out);

    session->focus.lookahead_frames = saved_focus_lookahead;
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        session->requests[slot].request.range.end_frame =
            saved_request_ends[slot];
    }

    return status;
}

apta_status_t apta_internal_detail_accept_replay(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out)
{
    apta_source_frame_t end_frame;

    if (session == NULL || block == NULL || accepted_frames_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    end_frame = block->first_frame + block->frame_count;
    if ((block->first_frame %
         APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN) != 0u ||
        ((end_frame % APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN) != 0u &&
         !(session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
           end_frame == session->config.total_frames))) {
        *accepted_frames_out = 0u;
        return APTA_STATUS_NOT_AVAILABLE;
    }

    return apta_internal_detail_accept_replay_base(
        session,
        block,
        accepted_frames_out);
}
