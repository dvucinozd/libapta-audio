// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"
#include "apta_result_pool.h"
#include "apta_session_workspace.h"

APTA_API apta_status_t APTA_CALL apta_session_create_workspace_base(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

APTA_API apta_status_t APTA_CALL apta_session_set_source_base(
    apta_session_t *session,
    const apta_pcm_source_t *source);

APTA_API apta_status_t APTA_CALL apta_session_push_pcm_base(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

APTA_API apta_status_t APTA_CALL apta_session_signal_end_of_input_base(
    apta_session_t *session,
    apta_source_frame_t final_end_frame);

APTA_API apta_status_t APTA_CALL apta_session_process_base(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out);

static int apta_session_uses_bounded_results(
    const apta_session_t *session)
{
    return session != NULL &&
           (session->config.flags &
            APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u;
}

apta_status_t APTA_CALL apta_session_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out)
{
    apta_session_t *session = NULL;
    apta_internal_result_pool_control_t *pool = NULL;
    apta_result_t *initial_result = NULL;
    apta_status_t status;

    if (session_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *session_out = NULL;

    if (context == NULL || config == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if ((config->flags &
         ~APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if ((config->flags &
         APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) == 0u) {
        return apta_session_create_workspace_base(
            context,
            config,
            session_out);
    }

    if (config->static_workspace == NULL ||
        config->static_workspace_size == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_internal_workspace_session_prepare(
        context,
        config,
        &session);
    if (status < 0) {
        return status;
    }

    status = apta_internal_result_pool_create(
        context,
        config,
        &pool);
    if (status < 0) {
        apta_internal_workspace_session_abandon(session);
        return status;
    }

    status = apta_internal_result_pool_create_empty_result(
        pool,
        config,
        1u,
        APTA_SESSION_CREATED,
        0u,
        session->lineage_id_high,
        session->lineage_id_low,
        &initial_result);
    if (status < 0) {
        apta_internal_result_pool_release(pool);
        apta_internal_workspace_session_abandon(session);
        return status;
    }

    session->result_pool = pool;
    session->current_result = initial_result;
    session->generation = 1u;
    apta_internal_workspace_session_commit(session);

    *session_out = session;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_set_source(
    apta_session_t *session,
    const apta_pcm_source_t *source)
{
    if (session == NULL || source == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (apta_session_uses_bounded_results(session)) {
        return APTA_ERROR_UNSUPPORTED;
    }
    return apta_session_set_source_base(session, source);
}

apta_status_t APTA_CALL apta_session_push_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out)
{
    if (accepted_frames_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *accepted_frames_out = 0u;

    if (session == NULL || block == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (apta_session_uses_bounded_results(session)) {
        return APTA_ERROR_UNSUPPORTED;
    }
    return apta_session_push_pcm_base(
        session,
        block,
        accepted_frames_out);
}

apta_status_t APTA_CALL apta_session_signal_end_of_input(
    apta_session_t *session,
    apta_source_frame_t final_end_frame)
{
    if (session == NULL ||
        final_end_frame == APTA_TOTAL_FRAMES_UNKNOWN) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (apta_session_uses_bounded_results(session)) {
        return APTA_ERROR_UNSUPPORTED;
    }
    return apta_session_signal_end_of_input_base(
        session,
        final_end_frame);
}

apta_status_t APTA_CALL apta_session_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out)
{
    if (session == NULL || budget == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            budget,
            sizeof(*budget),
            budget->struct_size,
            budget->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (progress_out != NULL &&
        !apta_internal_validate_struct(
            progress_out,
            sizeof(*progress_out),
            progress_out->struct_size,
            progress_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (apta_session_uses_bounded_results(session)) {
        return APTA_ERROR_UNSUPPORTED;
    }
    return apta_session_process_base(
        session,
        budget,
        progress_out);
}
