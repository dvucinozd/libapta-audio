// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"
#include "apta_result_pool.h"

APTA_API apta_status_t apta_internal_publish_result_base(
    apta_session_t *session,
    apta_feature_mask_t changed_features);

static void apta_pool_publication_lock(atomic_flag *lock)
{
    while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire)) {
    }
}

static void apta_pool_publication_unlock(atomic_flag *lock)
{
    atomic_flag_clear_explicit(lock, memory_order_release);
}

static void apta_pool_mark_overview_publication_pending(
    apta_session_t *session)
{
    uint32_t index;

    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        session->overview_accumulators[index].complete = 0u;
    }
    session->overview_complete_count = 0u;
}

apta_status_t apta_internal_publish_result(
    apta_session_t *session,
    apta_feature_mask_t changed_features)
{
    apta_result_t *new_result = NULL;
    apta_result_t *old_result;
    apta_generation_t next_generation;
    apta_status_t status;

    if (session == NULL || session->context == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((session->config.flags &
         APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) == 0u) {
        return apta_internal_publish_result_base(
            session,
            changed_features);
    }
    if (session->result_pool == NULL) {
        return APTA_ERROR_INTERNAL;
    }
    if (session->generation == UINT64_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    next_generation = session->generation + 1u;
    status = apta_internal_result_pool_create_session_result(
        session->result_pool,
        session,
        next_generation,
        changed_features,
        &new_result);
    if (status < 0) {
        if (status == APTA_ERROR_RESULT_SLOTS_EXHAUSTED &&
            (changed_features & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
            apta_pool_mark_overview_publication_pending(session);
        }
        return status;
    }

    apta_pool_publication_lock(&session->result_lock);
    old_result = session->current_result;
    session->current_result = new_result;
    session->generation = next_generation;
    apta_pool_publication_unlock(&session->result_lock);

    apta_internal_result_release(old_result);
    return APTA_STATUS_OK;
}
