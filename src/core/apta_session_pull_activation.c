// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"
#include "apta_session_pull.h"

#include <string.h>

APTA_API apta_status_t APTA_CALL apta_query_memory_requirements_base(
    const apta_session_config_t *config,
    apta_memory_requirements_t *requirements_out);

APTA_API apta_status_t APTA_CALL apta_session_create_activation_base(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

APTA_API apta_status_t APTA_CALL apta_session_process_activation_base(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out);

apta_status_t APTA_CALL apta_query_memory_requirements(
    const apta_session_config_t *config,
    apta_memory_requirements_t *requirements_out)
{
    apta_session_config_t compatible_config;

    if (config == NULL || requirements_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (config->input_mode != APTA_INPUT_MODE_PULL) {
        return apta_query_memory_requirements_base(
            config,
            requirements_out);
    }

    compatible_config = *config;
    compatible_config.input_mode = APTA_INPUT_MODE_PUSH;
    return apta_query_memory_requirements_base(
        &compatible_config,
        requirements_out);
}

apta_status_t APTA_CALL apta_session_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out)
{
    apta_session_config_t compatible_config;
    apta_session_t *session;
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

    if (config->input_mode != APTA_INPUT_MODE_PULL) {
        return apta_session_create_activation_base(
            context,
            config,
            session_out);
    }

    compatible_config = *config;
    compatible_config.input_mode = APTA_INPUT_MODE_PUSH;
    session = NULL;
    status = apta_session_create_activation_base(
        context,
        &compatible_config,
        &session);
    if (status < 0) {
        return status;
    }

    session->config.input_mode = APTA_INPUT_MODE_PULL;
    *session_out = session;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out)
{
    apta_status_t pull_status;
    apta_status_t process_status;
    uint32_t pulled_frames;

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

    if (session->config.input_mode != APTA_INPUT_MODE_PULL ||
        apta_session_is_cancel_requested(session) != 0u) {
        return apta_session_process_activation_base(
            session,
            budget,
            progress_out);
    }

    pulled_frames = 0u;
    pull_status = apta_internal_pull_pcm_before_process(
        session,
        budget,
        &pulled_frames);
    if (pull_status < 0) {
        return pull_status;
    }

    process_status = apta_session_process_activation_base(
        session,
        budget,
        progress_out);

    if (process_status == APTA_STATUS_WOULD_BLOCK &&
        pull_status == APTA_STATUS_WOULD_BLOCK) {
        return APTA_STATUS_WOULD_BLOCK;
    }

    return process_status;
}
