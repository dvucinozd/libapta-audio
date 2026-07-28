// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"
#include "apta_result_pool.h"
#include "apta_session_workspace.h"

APTA_API apta_status_t APTA_CALL apta_session_set_metadata_workspace_base(
    apta_session_t *session,
    const apta_metadata_t *metadata);

static void apta_bounded_metadata_result_lock(atomic_flag *lock)
{
    while (atomic_flag_test_and_set_explicit(
        lock,
        memory_order_acquire)) {
    }
}

static void apta_bounded_metadata_result_unlock(atomic_flag *lock)
{
    atomic_flag_clear_explicit(lock, memory_order_release);
}

static apta_status_t apta_bounded_session_set_metadata(
    apta_session_t *session,
    const apta_metadata_t *metadata)
{
    apta_internal_metadata_t replacement;
    apta_internal_metadata_t previous;
    apta_result_t *new_result = NULL;
    apta_result_t *old_result;
    apta_generation_t next_generation;
    apta_status_t status;

    if (atomic_flag_test_and_set_explicit(
            &session->process_lock,
            memory_order_acquire)) {
        return APTA_ERROR_BUSY;
    }
    if (atomic_load_explicit(&session->state, memory_order_acquire) !=
        APTA_SESSION_CREATED) {
        atomic_flag_clear_explicit(
            &session->process_lock,
            memory_order_release);
        return APTA_ERROR_INVALID_STATE;
    }
    if (session->result_pool == NULL) {
        atomic_flag_clear_explicit(
            &session->process_lock,
            memory_order_release);
        return APTA_ERROR_INTERNAL;
    }
    if (session->generation == UINT64_MAX) {
        atomic_flag_clear_explicit(
            &session->process_lock,
            memory_order_release);
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    memset(&replacement, 0, sizeof(replacement));
    apta_metadata_view_init(&replacement.view);
    if (metadata != NULL) {
        status = apta_internal_workspace_metadata_copy_input(
            session,
            metadata,
            &replacement);
        if (status < 0) {
            atomic_flag_clear_explicit(
                &session->process_lock,
                memory_order_release);
            return status;
        }
    }

    previous = session->metadata;
    session->metadata = replacement;
    next_generation = session->generation + 1u;

    status = apta_internal_result_pool_create_metadata_result(
        session->result_pool,
        &session->config,
        &session->metadata,
        next_generation,
        APTA_SESSION_CREATED,
        0u,
        session->lineage_id_high,
        session->lineage_id_low,
        &new_result);
    if (status < 0) {
        session->metadata = previous;
        apta_internal_metadata_cleanup(
            session->context,
            &replacement);
        atomic_flag_clear_explicit(
            &session->process_lock,
            memory_order_release);
        return status;
    }

    apta_bounded_metadata_result_lock(&session->result_lock);
    old_result = session->current_result;
    session->current_result = new_result;
    session->generation = next_generation;
    apta_bounded_metadata_result_unlock(&session->result_lock);

    apta_internal_metadata_cleanup(
        session->context,
        &previous);
    apta_internal_result_release(old_result);

    atomic_flag_clear_explicit(
        &session->process_lock,
        memory_order_release);
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_set_metadata(
    apta_session_t *session,
    const apta_metadata_t *metadata)
{
    if (session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((session->config.flags &
         APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u) {
        return apta_bounded_session_set_metadata(session, metadata);
    }

    return apta_session_set_metadata_workspace_base(
        session,
        metadata);
}
