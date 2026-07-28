// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <string.h>

static int apta_process_ranges_overlap(
    apta_source_frame_t first_a,
    apta_source_frame_t end_a,
    apta_source_frame_t first_b,
    apta_source_frame_t end_b)
{
    return first_a < end_b && first_b < end_a;
}

static void apta_process_score_node(
    const apta_session_t *session,
    const apta_internal_pcm_node_t *node,
    apta_internal_schedule_score_t *score_out)
{
    apta_source_frame_t node_first;
    apta_source_frame_t node_end;
    apta_source_frame_t focus_first;
    apta_source_frame_t focus_end;
    uint32_t slot;

    memset(score_out, 0, sizeof(*score_out));
    score_out->enqueue_serial = UINT64_MAX;

    node_first = node->first_frame + node->processed_frames;
    node_end = node->first_frame + node->frame_count;

    focus_first = 0u;
    focus_end = 0u;
    if (session->has_focus &&
        (session->focus.feature_mask &
         APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
        focus_first =
            session->focus.playhead_frame > session->focus.lookbehind_frames
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

    if (focus_end > focus_first &&
        apta_process_ranges_overlap(
            node_first,
            node_end,
            focus_first,
            focus_end)) {
        score_out->effective_priority = session->focus.priority;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        const apta_internal_request_t *request = &session->requests[slot];
        apta_internal_schedule_score_t candidate;

        if ((request->request.feature_mask &
             APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u ||
            !apta_process_ranges_overlap(
                node_first,
                node_end,
                request->request.range.first_frame,
                request->request.range.end_frame)) {
            continue;
        }

        apta_internal_scheduler_score_request(request, &candidate);
        if (candidate.request_id != 0u &&
            apta_internal_scheduler_score_better(
                &candidate,
                score_out)) {
            *score_out = candidate;
        }
    }
}

static int apta_process_node_better(
    const apta_session_t *session,
    const apta_internal_pcm_node_t *candidate,
    const apta_internal_pcm_node_t *current)
{
    apta_internal_schedule_score_t candidate_score;
    apta_internal_schedule_score_t current_score;

    apta_process_score_node(session, candidate, &candidate_score);
    apta_process_score_node(session, current, &current_score);
    return apta_internal_scheduler_score_better(
        &candidate_score,
        &current_score);
}

static uint32_t apta_process_sort_pcm_queue(apta_session_t *session)
{
    apta_internal_pcm_node_t *source;
    apta_internal_pcm_node_t *sorted;
    apta_internal_pcm_node_t *tail;
    apta_internal_schedule_score_t head_score;

    source = session->pcm_head;
    sorted = NULL;

    while (source != NULL) {
        apta_internal_pcm_node_t *next = source->next;

        if (sorted == NULL ||
            apta_process_node_better(session, source, sorted)) {
            source->next = sorted;
            sorted = source;
        } else {
            apta_internal_pcm_node_t *position = sorted;

            while (position->next != NULL &&
                   !apta_process_node_better(
                       session,
                       source,
                       position->next)) {
                position = position->next;
            }
            source->next = position->next;
            position->next = source;
        }
        source = next;
    }

    session->pcm_head = sorted;
    tail = sorted;
    while (tail != NULL && tail->next != NULL) {
        tail = tail->next;
    }
    session->pcm_tail = tail;

    if (sorted == NULL) {
        return 0u;
    }
    apta_process_score_node(session, sorted, &head_score);
    return head_score.request_id;
}

apta_status_t apta_internal_waveform_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out)
{
    apta_feature_mask_t saved_focus_mask;
    apta_feature_mask_t saved_request_masks[APTA_INTERNAL_MAX_REGION_REQUESTS];
    uint8_t saved_request_priorities[APTA_INTERNAL_MAX_REGION_REQUESTS];
    uint32_t selected_request_id;
    uint32_t slot;
    apta_status_t status;
    int detail_changed;

    apta_internal_detail_refresh_completed(session);
    detail_changed =
        session->detail_mutation_serial != session->detail_published_serial;

    saved_focus_mask = session->focus.feature_mask;
    if (session->has_focus &&
        (session->focus.feature_mask & APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
        session->focus.feature_mask |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        saved_request_masks[slot] =
            session->requests[slot].request.feature_mask;
        saved_request_priorities[slot] =
            session->requests[slot].request.priority;
        if ((saved_request_masks[slot] &
             APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
            session->requests[slot].request.feature_mask |=
                APTA_FEATURE_WAVEFORM_OVERVIEW;
        }
    }

    selected_request_id = apta_process_sort_pcm_queue(session);

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        apta_internal_schedule_score_t score;

        apta_internal_scheduler_score_request(
            &session->requests[slot],
            &score);
        if (score.request_id != 0u) {
            session->requests[slot].request.priority =
                (uint8_t)score.effective_priority;
        }
    }

    status = apta_internal_waveform_process_base(
        session,
        budget,
        progress_out,
        did_work_out,
        published_output_out);

    session->focus.feature_mask = saved_focus_mask;
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        session->requests[slot].request.feature_mask =
            saved_request_masks[slot];
        session->requests[slot].request.priority =
            saved_request_priorities[slot];
    }

    if (status < 0) {
        return status;
    }
    if (*did_work_out != 0u) {
        apta_internal_scheduler_note_choice(
            session,
            selected_request_id);
    }

    apta_internal_detail_update_request_states(session);

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) != 0u &&
        (detail_changed ||
         session->detail_mutation_serial !=
             session->detail_published_serial)) {
        if (*published_output_out == 0u) {
            apta_status_t publish_status = apta_internal_publish_result(
                session,
                APTA_FEATURE_WAVEFORM_DETAIL);
            if (publish_status < 0) {
                return publish_status;
            }
            *published_output_out = 1u;
        }

        session->detail_published_serial = session->detail_mutation_serial;
        if (progress_out != NULL) {
            progress_out->changed_features |= APTA_FEATURE_WAVEFORM_DETAIL;
            progress_out->published_generation = session->generation;
        }

        if (status == APTA_STATUS_WOULD_BLOCK) {
            status = APTA_STATUS_OK;
        }
    }

    return status;
}
