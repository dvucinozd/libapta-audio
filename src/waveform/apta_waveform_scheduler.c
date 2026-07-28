// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <string.h>

apta_status_t apta_internal_waveform_next_pcm_request_base(
    apta_session_t *session,
    apta_pcm_request_t *request_out);

static int apta_scheduler_request_matches_feature(
    const apta_internal_request_t *request,
    apta_feature_mask_t feature)
{
    apta_internal_schedule_score_t score;

    if ((request->request.feature_mask & feature) == 0u) {
        return 0;
    }
    apta_internal_scheduler_score_request(request, &score);
    return score.request_id != 0u;
}

static int apta_scheduler_request_better(
    const apta_internal_request_t *candidate,
    const apta_internal_request_t *current,
    apta_feature_mask_t feature)
{
    apta_internal_schedule_score_t candidate_score;
    apta_internal_schedule_score_t current_score;
    int candidate_matches;
    int current_matches;

    candidate_matches =
        apta_scheduler_request_matches_feature(candidate, feature);
    current_matches =
        apta_scheduler_request_matches_feature(current, feature);
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

static void apta_scheduler_stage_requests(
    apta_session_t *session,
    apta_feature_mask_t feature,
    apta_internal_request_t saved[APTA_INTERNAL_MAX_REGION_REQUESTS])
{
    uint32_t index;

    memcpy(saved, session->requests, sizeof(session->requests));

    for (index = 1u; index < APTA_INTERNAL_MAX_REGION_REQUESTS; ++index) {
        apta_internal_request_t candidate = session->requests[index];
        uint32_t position = index;

        while (position > 0u &&
               apta_scheduler_request_better(
                   &candidate,
                   &session->requests[position - 1u],
                   feature)) {
            session->requests[position] = session->requests[position - 1u];
            position -= 1u;
        }
        session->requests[position] = candidate;
    }

    for (index = 0u; index < APTA_INTERNAL_MAX_REGION_REQUESTS; ++index) {
        apta_internal_schedule_score_t score;

        if (!apta_scheduler_request_matches_feature(
                &session->requests[index],
                feature)) {
            continue;
        }
        apta_internal_scheduler_score_request(
            &session->requests[index],
            &score);
        session->requests[index].request.priority =
            (uint8_t)score.effective_priority;
    }
}

apta_status_t apta_internal_waveform_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out)
{
    apta_internal_request_t saved[APTA_INTERNAL_MAX_REGION_REQUESTS];
    apta_status_t status;
    uint32_t selected_request_id;

    if (session == NULL || request_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    apta_scheduler_stage_requests(
        session,
        APTA_FEATURE_WAVEFORM_OVERVIEW,
        saved);
    status = apta_internal_waveform_next_pcm_request_base(
        session,
        request_out);
    selected_request_id =
        status == APTA_STATUS_OK ? request_out->request_token : 0u;
    memcpy(session->requests, saved, sizeof(session->requests));

    if (status == APTA_STATUS_OK) {
        apta_internal_scheduler_note_choice(session, selected_request_id);
    }
    return status;
}
