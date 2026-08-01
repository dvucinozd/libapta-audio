// SPDX-License-Identifier: Apache-2.0
#include "apta_session_workspace.h"
#include "apta_result_pool.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

APTA_API apta_status_t APTA_CALL apta_session_create_contract_base(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

APTA_API apta_status_t APTA_CALL apta_session_destroy_heap_base(
    apta_session_t *session);

static int apta_workspace_sample_format_is_valid(apta_sample_format_t format)
{
    return format == APTA_SAMPLE_S16_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_S24_3LE_INTERLEAVED ||
           format == APTA_SAMPLE_S32_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_F32_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_F32_NATIVE_PLANAR;
}

static int apta_workspace_feature_mask_is_coherent(
    apta_feature_mask_t feature_mask)
{
    return (feature_mask & APTA_FEATURE_WAVEFORM_DETAIL) == 0u ||
           (feature_mask & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u;
}

static int apta_workspace_config_is_valid(
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
        !apta_workspace_sample_format_is_valid(config->sample_format)) {
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
    if (!apta_workspace_feature_mask_is_coherent(
            config->requested_features)) {
        return 0;
    }
    if ((config->requested_features & ~context->capabilities) != 0u) {
        return 0;
    }
    if ((config->requested_features & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u &&
        config->channel_count > 2u) {
        return 0;
    }

    return 1;
}

static apta_status_t apta_workspace_validate_config(
    apta_context_t *context,
    const apta_session_config_t *config)
{
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
    if (!apta_workspace_config_is_valid(context, config)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (((uintptr_t)config->static_workspace &
         (uintptr_t)(APTA_INTERNAL_MAX_ALIGNMENT - 1u)) != 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (config->static_workspace_size <
        apta_internal_session_workspace_minimum_size()) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    /* A5: the check above is a floor that ignores total_frames and
     * requested_features, so it accepted a 12 KiB buffer for a full-feature
     * multi-minute track and failed much later inside process(). Reject that
     * here instead, where it is a diagnosable configuration error. */
    {
        const size_t required =
            apta_internal_session_workspace_requirement(config);
        if (required == SIZE_MAX) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        if (config->static_workspace_size < required) {
            return APTA_ERROR_OUT_OF_MEMORY;
        }
    }

    return APTA_STATUS_OK;
}

apta_status_t apta_internal_workspace_session_prepare(
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

    if (context == NULL || config == NULL ||
        config->static_workspace == NULL ||
        config->static_workspace_size == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_workspace_validate_config(context, config);
    if (status < 0) {
        return status;
    }

    session = (apta_session_t *)config->static_workspace;
    memset(session, 0, sizeof(*session));
    session->context = context;
    session->config = *config;
    session->final_end_frame = APTA_TOTAL_FRAMES_UNKNOWN;
    session->next_request_id = 1u;
    session->overview_frames_per_column =
        APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN;
    session->lineage_id_low = atomic_fetch_add_explicit(
        &context->lineage_counter,
        1u,
        memory_order_acq_rel) + 1u;
    apta_metadata_view_init(&session->metadata.view);

    atomic_init(&session->state, APTA_SESSION_CREATED);
    atomic_init(&session->cancel_requested, 0u);
    atomic_flag_clear(&session->process_lock);
    atomic_flag_clear(&session->result_lock);

    status = apta_internal_session_workspace_initialize(session);
    if (status < 0) {
        memset(session, 0, sizeof(*session));
        return status;
    }

    *session_out = session;
    return APTA_STATUS_OK;
}

void apta_internal_workspace_session_commit(apta_session_t *session)
{
    if (session != NULL && session->context != NULL) {
        (void)atomic_fetch_add_explicit(
            &session->context->session_count,
            1u,
            memory_order_acq_rel);
    }
}

void apta_internal_workspace_session_abandon(apta_session_t *session)
{
    apta_context_t *context;

    if (session == NULL || session->context == NULL) {
        return;
    }

    context = session->context;
    apta_internal_metadata_cleanup(context, &session->metadata);
    apta_internal_waveform_cleanup_session(session);
    memset(session, 0, sizeof(*session));
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

    if (config->static_workspace == NULL &&
        config->static_workspace_size == 0u) {
        return apta_session_create_contract_base(
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

    status = apta_internal_publish_result(session, 0u);
    if (status < 0) {
        apta_internal_workspace_session_abandon(session);
        return status;
    }

    apta_internal_workspace_session_commit(session);
    *session_out = session;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_destroy(apta_session_t *session)
{
    apta_result_t *result;
    apta_internal_result_pool_control_t *result_pool;
    apta_context_t *context;

    if (session == NULL) {
        return APTA_STATUS_OK;
    }
    if (!apta_internal_session_uses_workspace(session)) {
        return apta_session_destroy_heap_base(session);
    }

    if (atomic_flag_test_and_set_explicit(
            &session->process_lock,
            memory_order_acquire)) {
        return APTA_ERROR_BUSY;
    }

    context = session->context;
    result_pool = session->result_pool;
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

    apta_internal_result_pool_release(result_pool);
    memset(session, 0, sizeof(*session));
    return APTA_STATUS_OK;
}
