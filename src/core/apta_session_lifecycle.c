// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <stdalign.h>
#include <string.h>

static int apta_session_sample_format_is_valid(apta_sample_format_t format)
{
    return format == APTA_SAMPLE_S16_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_S24_3LE_INTERLEAVED ||
           format == APTA_SAMPLE_S32_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_F32_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_F32_NATIVE_PLANAR;
}

static int apta_session_config_is_valid(
    const apta_context_t *context,
    const apta_session_config_t *config)
{
    if (config->input_mode != APTA_INPUT_MODE_PUSH &&
        config->input_mode != APTA_INPUT_MODE_PULL) {
        return 0;
    }

    if (config->source_sample_rate == 0u ||
        config->channel_count == 0u ||
        config->channel_count > 8u ||
        !apta_session_sample_format_is_valid(config->sample_format)) {
        return 0;
    }

    if (config->channel_layout == APTA_CHANNEL_LAYOUT_MONO &&
        config->channel_count != 1u) {
        return 0;
    }

    if (config->channel_layout == APTA_CHANNEL_LAYOUT_STEREO &&
        config->channel_count != 2u) {
        return 0;
    }

    if ((config->requested_features & ~context->capabilities) != 0u) {
        return 0;
    }

    if (!apta_internal_overview_resolution_is_valid(config)) {
        return 0;
    }

    if ((config->requested_features & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u &&
        config->channel_count > 2u) {
        return 0;
    }

    return 1;
}

static int apta_session_transition_is_allowed(
    apta_session_state_t old_state,
    apta_session_state_t new_state)
{
    if (old_state == new_state) {
        return 1;
    }

    switch (old_state) {
    case APTA_SESSION_CREATED:
        return new_state == APTA_SESSION_ACTIVE ||
               new_state == APTA_SESSION_CANCELLED ||
               new_state == APTA_SESSION_FAILED;
    case APTA_SESSION_ACTIVE:
        return new_state == APTA_SESSION_DRAINING ||
               new_state == APTA_SESSION_CANCELLED ||
               new_state == APTA_SESSION_FAILED;
    case APTA_SESSION_DRAINING:
        return new_state == APTA_SESSION_COMPLETED ||
               new_state == APTA_SESSION_CANCELLED ||
               new_state == APTA_SESSION_FAILED;
    default:
        return 0;
    }
}

apta_status_t apta_internal_session_transition(
    apta_session_t *session,
    apta_session_state_t new_state)
{
    apta_session_state_t old_state;
    apta_status_t status;

    if (session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    old_state = atomic_load_explicit(&session->state, memory_order_acquire);
    if (!apta_session_transition_is_allowed(old_state, new_state)) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (old_state == new_state) {
        return APTA_STATUS_OK;
    }

    atomic_store_explicit(&session->state, new_state, memory_order_release);
    status = apta_internal_publish_result(session, 0u);
    if (status < 0) {
        atomic_store_explicit(&session->state, old_state, memory_order_release);
    }

    return status;
}

apta_status_t APTA_CALL apta_session_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out)
{
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

    if ((config->requested_features & ~context->capabilities) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    if (config->input_mode == APTA_INPUT_MODE_PULL &&
        (config->requested_features & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    if (config->static_workspace != NULL ||
        config->static_workspace_size != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    if (!apta_session_config_is_valid(context, config)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    session = (apta_session_t *)apta_internal_context_allocate(
        context,
        sizeof(*session),
        alignof(apta_session_t),
        APTA_MEMORY_PERSISTENT);
    if (session == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(session, 0, sizeof(*session));
    session->context = context;
    session->config = *config;
    session->final_end_frame = APTA_TOTAL_FRAMES_UNKNOWN;
    session->next_request_id = 1u;
    /* C2: the host may choose the overview resolution; zero means default. */
    session->overview_frames_per_column =
        config->overview_frames_per_column != 0u
            ? config->overview_frames_per_column
            : APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN;
    apta_internal_waveform_init_bands(session);
    session->lineage_id_low = atomic_fetch_add_explicit(
        &context->lineage_counter,
        1u,
        memory_order_acq_rel) + 1u;
    apta_metadata_view_init(&session->metadata.view);

    atomic_init(&session->state, APTA_SESSION_CREATED);
    atomic_init(&session->cancel_requested, 0u);
    atomic_flag_clear(&session->process_lock);
    atomic_flag_clear(&session->result_lock);

    status = apta_internal_publish_result(session, 0u);
    if (status < 0) {
        apta_internal_metadata_cleanup(context, &session->metadata);
        apta_internal_context_deallocate(context, session);
        return status;
    }

    (void)atomic_fetch_add_explicit(
        &context->session_count,
        1u,
        memory_order_acq_rel);

    *session_out = session;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_destroy(apta_session_t *session)
{
    apta_result_t *result;
    apta_context_t *context;

    if (session == NULL) {
        return APTA_STATUS_OK;
    }

    if (atomic_flag_test_and_set_explicit(
            &session->process_lock,
            memory_order_acquire)) {
        return APTA_ERROR_BUSY;
    }

    context = session->context;

    while (atomic_flag_test_and_set_explicit(
        &session->result_lock,
        memory_order_acquire)) {
    }
    result = session->current_result;
    session->current_result = NULL;
    atomic_flag_clear_explicit(&session->result_lock, memory_order_release);

    apta_internal_result_release(result);
    apta_internal_metadata_cleanup(context, &session->metadata);
    apta_internal_waveform_cleanup_session(session);

    (void)atomic_fetch_sub_explicit(
        &context->session_count,
        1u,
        memory_order_acq_rel);

    apta_internal_context_deallocate(context, session);
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out)
{
    apta_session_state_t state;
    apta_status_t status;
    apta_status_t work_status;
    uint32_t did_work;
    uint32_t published_output;

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

    if (atomic_flag_test_and_set_explicit(
            &session->process_lock,
            memory_order_acquire)) {
        return APTA_ERROR_BUSY;
    }

    session->process_deadline_ns = 0u;
    if (budget->soft_time_budget_us != 0u &&
        session->context->clock.monotonic_time_ns != NULL) {
        const uint64_t now = session->context->clock.monotonic_time_ns(
            session->context->clock.user_data);
        const uint64_t duration =
            (uint64_t)budget->soft_time_budget_us * UINT64_C(1000);

        if (now != 0u) {
            session->process_deadline_ns =
                UINT64_MAX - now < duration ? UINT64_MAX : now + duration;
        }
    }

    if (progress_out != NULL) {
        memset(progress_out, 0, sizeof(*progress_out));
        progress_out->struct_size = (uint32_t)sizeof(*progress_out);
        progress_out->api_version = APTA_API_VERSION;
        progress_out->published_generation = session->generation;
    }

    if (atomic_load_explicit(
            &session->cancel_requested,
            memory_order_acquire) != 0u) {
        status = apta_internal_session_transition(
            session,
            APTA_SESSION_CANCELLED);
        session->process_deadline_ns = 0u;
        atomic_flag_clear_explicit(&session->process_lock, memory_order_release);
        return status < 0 ? status : APTA_ERROR_CANCELLED;
    }

    state = atomic_load_explicit(&session->state, memory_order_acquire);
    if (state == APTA_SESSION_COMPLETED) {
        session->process_deadline_ns = 0u;
        atomic_flag_clear_explicit(&session->process_lock, memory_order_release);
        return APTA_STATUS_END_OF_INPUT;
    }
    if (state == APTA_SESSION_CANCELLED) {
        session->process_deadline_ns = 0u;
        atomic_flag_clear_explicit(&session->process_lock, memory_order_release);
        return APTA_ERROR_CANCELLED;
    }
    if (state == APTA_SESSION_FAILED) {
        session->process_deadline_ns = 0u;
        atomic_flag_clear_explicit(&session->process_lock, memory_order_release);
        return APTA_ERROR_INTERNAL;
    }

    did_work = 0u;
    published_output = 0u;
    work_status = APTA_STATUS_WOULD_BLOCK;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u) {
        work_status = apta_internal_waveform_process(
            session,
            budget,
            progress_out,
            &did_work,
            &published_output);
        if (work_status < 0) {
            session->process_deadline_ns = 0u;
            atomic_flag_clear_explicit(
                &session->process_lock,
                memory_order_release);
            return work_status;
        }
    }

    state = atomic_load_explicit(&session->state, memory_order_acquire);
    if (state == APTA_SESSION_DRAINING && session->pcm_head == NULL &&
        !apta_internal_analysis_pending(session)) {
        status = apta_internal_session_transition(
            session,
            APTA_SESSION_COMPLETED);
        if (progress_out != NULL && status >= 0) {
            progress_out->published_generation = session->generation;
        }
        session->process_deadline_ns = 0u;
        atomic_flag_clear_explicit(&session->process_lock, memory_order_release);
        return status < 0 ? status : APTA_STATUS_END_OF_INPUT;
    }

    session->process_deadline_ns = 0u;
    atomic_flag_clear_explicit(&session->process_lock, memory_order_release);

    if (did_work != 0u || published_output != 0u) {
        return work_status;
    }
    if (work_status == APTA_STATUS_MORE_WORK) {
        return APTA_STATUS_MORE_WORK;
    }

    return APTA_STATUS_WOULD_BLOCK;
}

void APTA_CALL apta_session_request_cancel(apta_session_t *session)
{
    if (session != NULL) {
        atomic_store_explicit(
            &session->cancel_requested,
            1u,
            memory_order_release);
    }
}

uint32_t APTA_CALL apta_session_is_cancel_requested(
    const apta_session_t *session)
{
    return session != NULL
               ? atomic_load_explicit(
                     &session->cancel_requested,
                     memory_order_acquire)
               : 0u;
}

apta_session_state_t APTA_CALL apta_session_get_state(
    const apta_session_t *session)
{
    return session != NULL
               ? atomic_load_explicit(&session->state, memory_order_acquire)
               : APTA_SESSION_FAILED;
}
