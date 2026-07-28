// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <string.h>

static int apta_scheduler_request_id_exists(
    const apta_session_t *session,
    uint32_t request_id)
{
    uint32_t slot;

    if (request_id == 0u) {
        return 0;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        if (session->requests[slot].request_id == request_id) {
            return 1;
        }
    }

    return 0;
}

static uint32_t apta_scheduler_allocate_request_id(apta_session_t *session)
{
    uint32_t attempts;
    uint32_t candidate;

    for (attempts = 0u; attempts < UINT32_MAX; ++attempts) {
        candidate = session->next_request_id++;
        if (candidate == 0u) {
            continue;
        }
        if (!apta_scheduler_request_id_exists(session, candidate)) {
            return candidate;
        }
    }

    return 0u;
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

    if (focus->playhead_frame == APTA_TOTAL_FRAMES_UNKNOWN) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if ((focus->feature_mask & ~session->context->capabilities) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        focus->playhead_frame > session->config.total_frames) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    session->focus = *focus;
    session->has_focus = 1u;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_request_region(
    apta_session_t *session,
    const apta_region_request_t *request,
    uint32_t *request_id_out)
{
    uint32_t slot;
    uint32_t request_id;

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
            request->api_version) ||
        !apta_internal_validate_struct(
            &request->range,
            sizeof(request->range),
            request->range.struct_size,
            request->range.api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (request->range.first_frame >= request->range.end_frame ||
        request->feature_mask == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if ((request->feature_mask & ~session->context->capabilities) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    if ((request->feature_mask & ~session->config.requested_features) != 0u) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (request->request_id != 0u &&
        apta_scheduler_request_id_exists(session, request->request_id)) {
        return APTA_ERROR_CONFLICT;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        if (session->requests[slot].request_id == 0u) {
            break;
        }
    }

    if (slot == APTA_INTERNAL_MAX_REGION_REQUESTS) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    request_id = request->request_id != 0u
                     ? request->request_id
                     : apta_scheduler_allocate_request_id(session);
    if (request_id == 0u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    session->requests[slot].request_id = request_id;
    session->requests[slot].request = *request;
    session->requests[slot].request.request_id = request_id;
    session->requests[slot].state = APTA_REQUEST_QUEUED;
    session->requests[slot].diagnostic_code = 0u;

    *request_id_out = request_id;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_cancel_region_request(
    apta_session_t *session,
    uint32_t request_id)
{
    uint32_t slot;

    if (session == NULL || request_id == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        if (session->requests[slot].request_id == request_id) {
            session->requests[slot].state = APTA_REQUEST_CANCELLED;
            return APTA_STATUS_OK;
        }
    }

    return APTA_STATUS_NOT_AVAILABLE;
}

apta_status_t APTA_CALL apta_session_get_request_progress(
    const apta_session_t *session,
    uint32_t request_id,
    apta_request_progress_t *progress_out)
{
    uint32_t slot;

    if (session == NULL || request_id == 0u || progress_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (!apta_internal_validate_struct(
            progress_out,
            sizeof(*progress_out),
            progress_out->struct_size,
            progress_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        if (session->requests[slot].request_id == request_id) {
            memset(progress_out, 0, sizeof(*progress_out));
            progress_out->struct_size = (uint32_t)sizeof(*progress_out);
            progress_out->api_version = APTA_API_VERSION;
            progress_out->request_id = request_id;
            progress_out->state = session->requests[slot].state;
            progress_out->requested_range = session->requests[slot].request.range;
            progress_out->requested_features =
                session->requests[slot].request.feature_mask;
            progress_out->satisfied_features =
                session->requests[slot].state == APTA_REQUEST_SATISFIED
                    ? session->requests[slot].request.feature_mask
                    : 0u;
            progress_out->progress_permille =
                session->requests[slot].state == APTA_REQUEST_SATISFIED
                    ? 1000u
                    : (session->requests[slot].state ==
                               APTA_REQUEST_PARTIALLY_SATISFIED
                           ? 500u
                           : 0u);
            progress_out->diagnostic_code =
                session->requests[slot].diagnostic_code;
            return APTA_STATUS_OK;
        }
    }

    return APTA_STATUS_NOT_AVAILABLE;
}

apta_status_t APTA_CALL apta_session_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out)
{
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

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
        return apta_internal_waveform_next_pcm_request(session, request_out);
    }

    return APTA_STATUS_NOT_AVAILABLE;
}
