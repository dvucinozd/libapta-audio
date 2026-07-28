// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <limits.h>
#include <string.h>

static int apta_scheduler_request_is_active(
    const apta_internal_request_t *request)
{
    return request->request_id != 0u &&
           request->state != APTA_REQUEST_CANCELLED &&
           request->state != APTA_REQUEST_FAILED &&
           request->state != APTA_REQUEST_SATISFIED;
}

apta_status_t apta_internal_scheduler_register_request(
    apta_session_t *session,
    apta_internal_request_t *request)
{
    if (session == NULL || request == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (session->next_scheduler_enqueue_serial == UINT64_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    session->next_scheduler_enqueue_serial += 1u;
    request->scheduler_enqueue_serial =
        session->next_scheduler_enqueue_serial;
    request->scheduler_skip_count = 0u;
    request->reserved32 = 0u;
    return APTA_STATUS_OK;
}

void apta_internal_scheduler_score_request(
    const apta_internal_request_t *request,
    apta_internal_schedule_score_t *score_out)
{
    uint32_t base_priority;
    uint32_t boost;
    uint32_t remaining;

    if (score_out == NULL) {
        return;
    }

    memset(score_out, 0, sizeof(*score_out));
    score_out->enqueue_serial = UINT64_MAX;

    if (request == NULL || !apta_scheduler_request_is_active(request)) {
        return;
    }

    base_priority = request->request.priority;
    remaining = UINT8_MAX - base_priority;
    boost = request->scheduler_skip_count *
            APTA_INTERNAL_SCHEDULER_AGE_STEP;
    if (boost > remaining) {
        boost = remaining;
    }

    score_out->effective_priority = base_priority + boost;
    score_out->request_id = request->request_id;
    score_out->soft_deadline_monotonic_ns =
        request->request.soft_deadline_monotonic_ns;
    score_out->enqueue_serial = request->scheduler_enqueue_serial;
}

int apta_internal_scheduler_score_better(
    const apta_internal_schedule_score_t *candidate,
    const apta_internal_schedule_score_t *current)
{
    int candidate_has_deadline;
    int current_has_deadline;

    if (candidate == NULL) {
        return 0;
    }
    if (current == NULL) {
        return 1;
    }

    if (candidate->effective_priority != current->effective_priority) {
        return candidate->effective_priority > current->effective_priority;
    }

    candidate_has_deadline =
        candidate->soft_deadline_monotonic_ns != 0u;
    current_has_deadline =
        current->soft_deadline_monotonic_ns != 0u;
    if (candidate_has_deadline != current_has_deadline) {
        return candidate_has_deadline;
    }
    if (candidate_has_deadline &&
        candidate->soft_deadline_monotonic_ns !=
            current->soft_deadline_monotonic_ns) {
        return candidate->soft_deadline_monotonic_ns <
               current->soft_deadline_monotonic_ns;
    }

    if (candidate->enqueue_serial != current->enqueue_serial) {
        return candidate->enqueue_serial < current->enqueue_serial;
    }
    return candidate->request_id < current->request_id;
}

void apta_internal_scheduler_note_choice(
    apta_session_t *session,
    uint32_t selected_request_id)
{
    uint32_t slot;

    if (session == NULL) {
        return;
    }

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        apta_internal_request_t *request = &session->requests[slot];

        if (!apta_scheduler_request_is_active(request)) {
            continue;
        }
        if (request->request_id == selected_request_id) {
            request->scheduler_skip_count = 0u;
        } else if (request->scheduler_skip_count <
                   APTA_INTERNAL_SCHEDULER_MAX_SKIPS) {
            request->scheduler_skip_count += 1u;
        }
    }
}
