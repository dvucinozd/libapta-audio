// SPDX-License-Identifier: Apache-2.0
#include "apta_key_internal.h"

apta_status_t apta_internal_waveform_process_key_base(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out);

int apta_internal_analysis_pending_key_base(const apta_session_t *session);

apta_status_t apta_internal_waveform_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out)
{
    apta_progress_t local_progress;
    apta_progress_t *work_progress = progress_out;
    apta_status_t status;
    apta_status_t key_status;
    apta_feature_mask_t pending;
    uint32_t remaining_steps;
    uint32_t key_steps = 0u;

    if (session == NULL || budget == NULL || did_work_out == NULL ||
        published_output_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (work_progress == NULL) {
        apta_progress_init(&local_progress);
        work_progress = &local_progress;
    }

    status = apta_internal_waveform_process_key_base(
        session,
        budget,
        work_progress,
        did_work_out,
        published_output_out);
    if (status < 0) {
        return status;
    }

    remaining_steps = budget->maximum_steps == 0u
                          ? UINT32_MAX
                          : work_progress->completed_steps < budget->maximum_steps
                                ? budget->maximum_steps - work_progress->completed_steps
                                : 0u;
    if (session->process_deadline_ns != 0u &&
        session->context->clock.monotonic_time_ns != NULL &&
        session->context->clock.monotonic_time_ns(
            session->context->clock.user_data) >= session->process_deadline_ns) {
        remaining_steps = 0u;
    }

    key_status = apta_internal_key_refresh(
        session,
        remaining_steps,
        &key_steps);
    if (key_status < 0) {
        return key_status;
    }
    work_progress->completed_steps += key_steps;
    if (key_steps != 0u) {
        *did_work_out = 1u;
        if (status == APTA_STATUS_WOULD_BLOCK) {
            status = APTA_STATUS_OK;
        }
    }
    if (key_status == APTA_STATUS_MORE_WORK) {
        status = APTA_STATUS_MORE_WORK;
    }

    pending = apta_internal_key_pending_features(session);
    if (pending != 0u) {
        const apta_status_t publish_status =
            apta_internal_publish_result(session, pending);
        if (publish_status < 0) {
            return publish_status;
        }
        apta_internal_key_mark_published(session);
        *published_output_out = 1u;
        work_progress->changed_features |= pending;
        work_progress->published_generation = session->generation;
        if (status == APTA_STATUS_WOULD_BLOCK) {
            status = APTA_STATUS_OK;
        }
    }
    return status;
}

int apta_internal_analysis_pending(const apta_session_t *session)
{
    return apta_internal_analysis_pending_key_base(session) ||
           apta_internal_key_refresh_pending(session);
}
