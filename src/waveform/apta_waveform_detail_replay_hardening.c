// SPDX-License-Identifier: Apache-2.0
#include "apta_waveform_detail_internal.h"

#include <string.h>

apta_status_t apta_internal_detail_next_pcm_request_base(
    apta_session_t *session,
    apta_pcm_request_t *request_out);

apta_status_t apta_internal_detail_accept_replay_base(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

static int apta_detail_scheduler_request_matches(
    const apta_internal_request_t *request)
{
    apta_internal_schedule_score_t score;

    if ((request->request.feature_mask &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
        return 0;
    }
    apta_internal_scheduler_score_request(request, &score);
    return score.request_id != 0u;
}

static int apta_detail_scheduler_request_better(
    const apta_internal_request_t *candidate,
    const apta_internal_request_t *current)
{
    apta_internal_schedule_score_t candidate_score;
    apta_internal_schedule_score_t current_score;
    int candidate_matches =
        apta_detail_scheduler_request_matches(candidate);
    int current_matches =
        apta_detail_scheduler_request_matches(current);

    if (candidate_matches != current_matches) {
        return candidate_matches;
    }
    if (!candidate_matches) {
        return 0;
    }

    apta_internal_scheduler_score_request(candidate, &candidate_score);
    apta_internal_scheduler_score_request(current, &current_score);
    return apta_internal_scheduler_score_better(
        &candidate_score,
        &current_score);
}

static void apta_detail_scheduler_stage_requests(apta_session_t *session)
{
    uint32_t index;

    for (index = 1u; index < APTA_INTERNAL_MAX_REGION_REQUESTS; ++index) {
        apta_internal_request_t candidate = session->requests[index];
        uint32_t position = index;

        while (position > 0u &&
               apta_detail_scheduler_request_better(
                   &candidate,
                   &session->requests[position - 1u])) {
            session->requests[position] = session->requests[position - 1u];
            position -= 1u;
        }
        session->requests[position] = candidate;
    }

    for (index = 0u; index < APTA_INTERNAL_MAX_REGION_REQUESTS; ++index) {
        apta_internal_schedule_score_t score;

        if (!apta_detail_scheduler_request_matches(
                &session->requests[index])) {
            continue;
        }
        apta_internal_scheduler_score_request(
            &session->requests[index],
            &score);
        session->requests[index].request.priority =
            (uint8_t)score.effective_priority;
    }
}

apta_status_t apta_internal_detail_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out)
{
    const apta_source_frame_t maximum_safe_end =
        UINT64_MAX -
        (APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN - 1u);
    apta_internal_request_t saved_requests[APTA_INTERNAL_MAX_REGION_REQUESTS];
    uint64_t saved_focus_lookahead;
    uint32_t slot;
    uint32_t selected_request_id;
    apta_status_t status;

    if (session == NULL || request_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    memcpy(saved_requests, session->requests, sizeof(session->requests));
    saved_focus_lookahead = session->focus.lookahead_frames;
    if (session->has_focus &&
        session->focus.playhead_frame < maximum_safe_end &&
        session->focus.lookahead_frames >
            maximum_safe_end - session->focus.playhead_frame) {
        session->focus.lookahead_frames =
            maximum_safe_end - session->focus.playhead_frame;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        if (session->requests[slot].request.range.end_frame >
            maximum_safe_end) {
            session->requests[slot].request.range.end_frame =
                maximum_safe_end;
        }
    }

    apta_detail_scheduler_stage_requests(session);
    status = apta_internal_detail_next_pcm_request_base(
        session,
        request_out);
    selected_request_id =
        status == APTA_STATUS_OK ? request_out->request_token : 0u;

    session->focus.lookahead_frames = saved_focus_lookahead;
    memcpy(session->requests, saved_requests, sizeof(session->requests));

    if (status == APTA_STATUS_OK) {
        apta_internal_scheduler_note_choice(session, selected_request_id);
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
