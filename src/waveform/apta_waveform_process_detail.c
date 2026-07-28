// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

apta_status_t apta_internal_waveform_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out)
{
    apta_feature_mask_t saved_focus_mask;
    apta_feature_mask_t saved_request_masks[APTA_INTERNAL_MAX_REGION_REQUESTS];
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
        if ((saved_request_masks[slot] &
             APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
            session->requests[slot].request.feature_mask |=
                APTA_FEATURE_WAVEFORM_OVERVIEW;
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
    }

    if (status < 0) {
        return status;
    }

    apta_internal_detail_update_request_states(session);

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) != 0u &&
        (detail_changed ||
         session->detail_mutation_serial != session->detail_published_serial)) {
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
