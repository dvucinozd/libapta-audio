// SPDX-License-Identifier: Apache-2.0
#include "apta_s6_internal.h"

apta_status_t apta_internal_waveform_process_s6_base(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out);

apta_status_t apta_internal_waveform_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out)
{
    apta_status_t status;
    apta_status_t refresh_status;
    apta_feature_mask_t pending;

    status = apta_internal_waveform_process_s6_base(
        session,
        budget,
        progress_out,
        did_work_out,
        published_output_out);
    if (status < 0) {
        return status;
    }

    refresh_status = apta_internal_s6_refresh(session);
    if (refresh_status < 0) {
        return refresh_status;
    }
    pending = apta_internal_s6_pending_features(session);
    if (pending != 0u) {
        const apta_status_t publish_status =
            apta_internal_publish_result(session, pending);
        if (publish_status < 0) {
            return publish_status;
        }
        apta_internal_s6_mark_published(session);
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
