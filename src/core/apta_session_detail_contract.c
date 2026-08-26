// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"
#include "../waveform/apta_waveform_detail_internal.h"

#include <string.h>

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

static int apta_session_mask_is_coherent(apta_feature_mask_t feature_mask)
{
    const apta_feature_mask_t waveform_dependency =
        APTA_FEATURE_WAVEFORM_DETAIL |
        APTA_FEATURE_WAVEFORM_3BAND |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_MUSICAL_KEY |
        APTA_FEATURE_METER_DOWNBEAT |
        APTA_FEATURE_CALIBRATED_QUALITY |
        APTA_INTERNAL_S4_FEATURES |
        APTA_INTERNAL_S6_FEATURES;

    if ((feature_mask & waveform_dependency) != 0u &&
        (feature_mask & APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
        return 0;
    }
    if ((feature_mask & APTA_FEATURE_LOCAL_BEATGRID) != 0u &&
        (feature_mask & APTA_FEATURE_BPM) == 0u) {
        return 0;
    }
    if ((feature_mask & APTA_FEATURE_GLOBAL_BEATGRID) != 0u &&
        (feature_mask & APTA_FEATURE_BPM) == 0u) {
        return 0;
    }
    if ((feature_mask & APTA_FEATURE_DYNAMIC_TEMPO) != 0u &&
        (feature_mask & APTA_FEATURE_GLOBAL_BEATGRID) == 0u) {
        return 0;
    }
    /* A4: the overview now carries its own confidence, so it is one of the
     * features CONFIDENCE can qualify. */
    if ((feature_mask & APTA_FEATURE_CONFIDENCE) != 0u &&
        (feature_mask &
         (APTA_FEATURE_WAVEFORM_OVERVIEW |
          APTA_FEATURE_BPM |
          APTA_FEATURE_LOCAL_BEATGRID |
          APTA_FEATURE_GLOBAL_BEATGRID |
          APTA_FEATURE_MUSICAL_KEY |
          APTA_FEATURE_METER_DOWNBEAT)) == 0u) {
        return 0;
    }
    if ((feature_mask & APTA_FEATURE_GRID_LOCKING) != 0u &&
        (feature_mask & APTA_FEATURE_LOCAL_BEATGRID) == 0u) {
        return 0;
    }
    if ((feature_mask & APTA_FEATURE_METER_DOWNBEAT) != 0u &&
        (feature_mask & APTA_FEATURE_LOCAL_BEATGRID) == 0u) {
        return 0;
    }
    if ((feature_mask & APTA_FEATURE_CALIBRATED_QUALITY) != 0u &&
        (feature_mask & APTA_FEATURE_BPM) == 0u) {
        return 0;
    }
    return 1;
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
    if (!apta_session_mask_is_coherent(config->requested_features)) {
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
    apta_feature_mask_t saved_focus_mask;
    apta_feature_mask_t saved_request_masks[APTA_INTERNAL_MAX_REGION_REQUESTS];
    apta_status_t status;
    uint32_t slot;

    if (session == NULL || request_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            request_out,
            sizeof(*request_out),
            request_out->struct_size,
            request_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    memset(request_out, 0, sizeof(*request_out));
    request_out->struct_size = (uint32_t)sizeof(*request_out);
    request_out->api_version = APTA_API_VERSION;
    request_out->range.struct_size = (uint32_t)sizeof(request_out->range);
    request_out->range.api_version = APTA_API_VERSION;

    status = apta_internal_detail_next_pcm_request(session, request_out);
    if (status == APTA_STATUS_OK || status < 0) {
        return status;
    }

    saved_focus_mask = session->focus.feature_mask;
    if ((saved_focus_mask &
         (APTA_INTERNAL_S4_FEATURES | APTA_INTERNAL_S6_FEATURES |
          APTA_FEATURE_MUSICAL_KEY | APTA_FEATURE_METER_DOWNBEAT)) != 0u) {
        session->focus.feature_mask |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    }
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        saved_request_masks[slot] =
            session->requests[slot].request.feature_mask;
        if ((saved_request_masks[slot] &
             (APTA_INTERNAL_S4_FEATURES | APTA_INTERNAL_S6_FEATURES |
          APTA_FEATURE_MUSICAL_KEY | APTA_FEATURE_METER_DOWNBEAT)) != 0u) {
            session->requests[slot].request.feature_mask |=
                APTA_FEATURE_WAVEFORM_OVERVIEW;
        }
    }

    status = apta_session_next_pcm_request_base(session, request_out);
    session->focus.feature_mask = saved_focus_mask;
    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        session->requests[slot].request.feature_mask = saved_request_masks[slot];
    }
    return status;
}
