// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

apta_status_t APTA_CALL apta_session_create_base(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

apta_status_t APTA_CALL apta_session_set_focus_base(
    apta_session_t *session,
    const apta_focus_t *focus);

apta_status_t APTA_CALL apta_session_request_region_base(
    apta_session_t *session,
    const apta_region_request_t *request,
    uint32_t *request_id_out);

apta_status_t APTA_CALL apta_session_next_pcm_request_base(
    apta_session_t *session,
    apta_pcm_request_t *request_out);

static int apta_detail_session_mask_is_coherent(
    apta_feature_mask_t feature_mask)
{
    return (feature_mask & APTA_FEATURE_WAVEFORM_DETAIL) == 0u ||
           (feature_mask & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u;
}

apta_status_t APTA_CALL apta_session_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out)
{
    if (session_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *session_out = NULL;

    if (config == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (!apta_detail_session_mask_is_coherent(config->requested_features)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    return apta_session_create_base(context, config, session_out);
}

apta_status_t APTA_CALL apta_session_set_focus(
    apta_session_t *session,
    const apta_focus_t *focus)
{
    if (session == NULL || focus == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            focus,
            sizeof(*focus),
            focus->struct_size,
            focus->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if ((focus->feature_mask & ~session->config.requested_features) != 0u) {
        return APTA_ERROR_INVALID_STATE;
    }

    return apta_session_set_focus_base(session, focus);
}

apta_status_t APTA_CALL apta_session_request_region(
    apta_session_t *session,
    const apta_region_request_t *request,
    uint32_t *request_id_out)
{
    if (request_id_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *request_id_out = 0u;

    if (session == NULL || request == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            request,
            sizeof(*request),
            request->struct_size,
            request->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if ((request->feature_mask & ~session->config.requested_features) != 0u) {
        return APTA_ERROR_INVALID_STATE;
    }

    return apta_session_request_region_base(
        session,
        request,
        request_id_out);
}

apta_status_t APTA_CALL apta_session_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out)
{
    apta_feature_mask_t saved_focus_mask = 0u;
    apta_feature_mask_t saved_request_masks[APTA_INTERNAL_MAX_REGION_REQUESTS];
    uint32_t slot;
    apta_status_t status;

    if (session == NULL) {
        return apta_session_next_pcm_request_base(session, request_out);
    }

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

    status = apta_session_next_pcm_request_base(session, request_out);

    session->focus.feature_mask = saved_focus_mask;
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        session->requests[slot].request.feature_mask =
            saved_request_masks[slot];
    }

    if (status == APTA_STATUS_OK &&
        (session->config.requested_features &
         APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
        request_out->feature_mask |= APTA_FEATURE_WAVEFORM_DETAIL;
    }
    return status;
}
