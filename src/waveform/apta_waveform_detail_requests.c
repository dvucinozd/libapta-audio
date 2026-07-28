// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

void apta_internal_detail_update_request_states(apta_session_t *session)
{
    uint32_t slot;

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        apta_internal_request_t *request = &session->requests[slot];
        int overview_satisfied;
        int overview_has_output;
        int detail_satisfied;
        int detail_has_output;

        if (request->request_id == 0u ||
            request->state == APTA_REQUEST_CANCELLED ||
            request->state == APTA_REQUEST_FAILED ||
            request->state == APTA_REQUEST_SATISFIED ||
            (request->request.feature_mask &
             APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
            continue;
        }

        overview_satisfied =
            (request->request.feature_mask &
             APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u ||
            request->state == APTA_REQUEST_SATISFIED;
        overview_has_output =
            (request->request.feature_mask &
             APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u ||
            request->state == APTA_REQUEST_PARTIALLY_SATISFIED ||
            request->state == APTA_REQUEST_SATISFIED;

        detail_satisfied = apta_internal_detail_range_complete(
            session,
            request->request.range.first_frame,
            request->request.range.end_frame);
        detail_has_output = apta_internal_detail_range_has_output(
            session,
            request->request.range.first_frame,
            request->request.range.end_frame);

        if (overview_satisfied && detail_satisfied) {
            request->state = APTA_REQUEST_SATISFIED;
        } else if (overview_has_output || detail_has_output) {
            request->state = APTA_REQUEST_PARTIALLY_SATISFIED;
        } else {
            request->state = APTA_REQUEST_WAITING_FOR_PCM;
        }
    }
}
