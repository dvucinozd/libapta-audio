// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

apta_status_t apta_internal_waveform_process_s4_base(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out);

static int apta_s4_range_changed(
    const apta_frame_range_t *before,
    const apta_frame_range_t *after)
{
    return before->first_frame != after->first_frame ||
           before->end_frame != after->end_frame;
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
    apta_frame_range_t old_requested = session->local_grid_requested_range;
    apta_frame_range_t old_applicability =
        session->local_grid_applicability_range;
    uint64_t old_mutation_serial = session->s4_mutation_serial;
    apta_status_t status;
    apta_status_t refresh_status;
    apta_feature_mask_t pending;
    uint32_t slot;

    saved_focus_mask = session->focus.feature_mask;
    if ((saved_focus_mask & APTA_INTERNAL_S4_FEATURES) != 0u) {
        session->focus.feature_mask |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    }
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        saved_request_masks[slot] =
            session->requests[slot].request.feature_mask;
        if ((saved_request_masks[slot] & APTA_INTERNAL_S4_FEATURES) != 0u) {
            session->requests[slot].request.feature_mask |=
                APTA_FEATURE_WAVEFORM_OVERVIEW;
        }
    }

    status = apta_internal_waveform_process_s4_base(
        session,
        budget,
        progress_out,
        did_work_out,
        published_output_out);

    session->focus.feature_mask = saved_focus_mask;
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        session->requests[slot].request.feature_mask = saved_request_masks[slot];
    }

    if (status < 0) {
        return status;
    }

    refresh_status = apta_internal_s4_refresh(session);
    if (refresh_status < 0) {
        return refresh_status;
    }
    if (session->has_local_grid &&
        session->s4_mutation_serial == old_mutation_serial &&
        (apta_s4_range_changed(
             &old_requested,
             &session->local_grid_requested_range) ||
         apta_s4_range_changed(
             &old_applicability,
             &session->local_grid_applicability_range))) {
        session->s4_mutation_serial += 1u;
    }

    pending = apta_internal_s4_pending_features(session);
    if (pending != 0u) {
        apta_status_t publish_status =
            apta_internal_publish_result(session, pending);
        if (publish_status < 0) {
            return publish_status;
        }
        apta_internal_s4_mark_published(session);
        *published_output_out = 1u;
        if (progress_out != NULL) {
            progress_out->changed_features |= pending;
            progress_out->published_generation = session->generation;
        }
        if (status == APTA_STATUS_WOULD_BLOCK) {
            status = APTA_STATUS_OK;
        }
    }

    return status;
}
