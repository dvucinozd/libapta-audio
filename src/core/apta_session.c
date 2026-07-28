// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

static void apta_session_lock(atomic_flag *lock)
{
    while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire)) {
        /* Bounded critical section. */
    }
}

static void apta_session_unlock(atomic_flag *lock)
{
    atomic_flag_clear_explicit(lock, memory_order_release);
}

static int apta_session_sample_format_is_valid(apta_sample_format_t format)
{
    return format == APTA_SAMPLE_S16_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_S24_3LE_INTERLEAVED ||
           format == APTA_SAMPLE_S32_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_F32_NATIVE_INTERLEAVED ||
           format == APTA_SAMPLE_F32_NATIVE_PLANAR;
}

static int apta_session_format_is_planar(apta_sample_format_t format)
{
    return format == APTA_SAMPLE_F32_NATIVE_PLANAR;
}

static int apta_session_config_is_valid(
    const apta_context_t *context,
    const apta_session_config_t *config)
{
    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return 0;
    }

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

    if ((config->static_workspace == NULL) != (config->static_workspace_size == 0u)) {
        return 0;
    }

    return 1;
}

static apta_status_t apta_session_transition(
    apta_session_t *session,
    apta_session_state_t new_state)
{
    apta_session_state_t old_state;
    apta_status_t status;

    old_state = atomic_load_explicit(&session->state, memory_order_acquire);
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

static apta_status_t apta_session_validate_pcm_block(
    const apta_session_t *session,
    const apta_pcm_block_t *block,
    apta_source_frame_t *end_frame_out)
{
    uint32_t channel;
    apta_source_frame_t end_frame;

    if (!apta_internal_validate_struct(
            block,
            sizeof(*block),
            block->struct_size,
            block->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (block->frame_count == 0u ||
        block->first_frame == APTA_TOTAL_FRAMES_UNKNOWN ||
        block->first_frame > UINT64_MAX - (uint64_t)block->frame_count) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    end_frame = block->first_frame + (uint64_t)block->frame_count;

    if (session->end_of_input_signalled && end_frame > session->final_end_frame) {
        return APTA_ERROR_CONFLICT;
    }

    if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        end_frame > session->config.total_frames) {
        return APTA_ERROR_CONFLICT;
    }

    if (apta_session_format_is_planar(session->config.sample_format)) {
        if (block->data != NULL) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }

        for (channel = 0u; channel < session->config.channel_count; ++channel) {
            if (block->planes[channel] == NULL) {
                return APTA_ERROR_INVALID_ARGUMENT;
            }
        }

        for (; channel < 8u; ++channel) {
            if (block->planes[channel] != NULL) {
                return APTA_ERROR_INVALID_ARGUMENT;
            }
        }
    } else {
        if (block->data == NULL) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }

        for (channel = 0u; channel < 8u; ++channel) {
            if (block->planes[channel] != NULL) {
                return APTA_ERROR_INVALID_ARGUMENT;
            }
        }
    }

    *end_frame_out = end_frame;
    return APTA_STATUS_OK;
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

    if (!apta_session_config_is_valid(context, config)) {
        if ((config->requested_features & ~context->capabilities) != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
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
    session->lineage_id_high = 0u;
    session->lineage_id_low = atomic_fetch_add_explicit(
        &context->lineage_counter,
        1u,
        memory_order_acq_rel) + 1u;

    atomic_init(&session->state, APTA_SESSION_CREATED);
    atomic_init(&session->cancel_requested, 0u);
    atomic_flag_clear(&session->process_lock);
    atomic_flag_clear(&session->result_lock);

    status = apta_internal_publish_result(session, 0u);
    if (status < 0) {
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

    apta_session_lock(&session->result_lock);
    result = session->current_result;
    session->current_result = NULL;
    apta_session_unlock(&session->result_lock);

    apta_internal_result_release(result);

    (void)atomic_fetch_sub_explicit(
        &context->session_count,
        1u,
        memory_order_acq_rel);

    apta_internal_context_deallocate(context, session);
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_set_source(
    apta_session_t *session,
    const apta_pcm_source_t *source)
{
    if (session == NULL || source == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (session->config.input_mode != APTA_INPUT_MODE_PULL) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (atomic_load_explicit(&session->state, memory_order_acquire) !=
        APTA_SESSION_CREATED) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (!apta_internal_validate_struct(
            source,
            sizeof(*source),
            source->struct_size,
            source->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (source->read_frames == NULL || source->release_frames == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    session->pull_source = *source;
    session->has_pull_source = 1u;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_push_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out)
{
    apta_status_t status;
    apta_source_frame_t end_frame;
    apta_session_state_t state;

    if (accepted_frames_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *accepted_frames_out = 0u;

    if (session == NULL || block == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (session->config.input_mode != APTA_INPUT_MODE_PUSH) {
        return APTA_ERROR_INVALID_STATE;
    }

    state = atomic_load_explicit(&session->state, memory_order_acquire);
    if (state == APTA_SESSION_CANCELLED) {
        return APTA_ERROR_CANCELLED;
    }
    if (state == APTA_SESSION_COMPLETED || state == APTA_SESSION_FAILED ||
        state == APTA_SESSION_DRAINING) {
        return APTA_ERROR_INVALID_STATE;
    }

    status = apta_session_validate_pcm_block(session, block, &end_frame);
    if (status < 0) {
        return status;
    }
    (void)end_frame;

    if (state == APTA_SESSION_CREATED) {
        status = apta_session_transition(session, APTA_SESSION_ACTIVE);
        if (status < 0) {
            return status;
        }
    }

    /* M1 prototype has no enabled analysis capability and retains no PCM. */
    *accepted_frames_out = block->frame_count;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_session_signal_end_of_input(
    apta_session_t *session,
    apta_source_frame_t final_end_frame)
{
    apta_session_state_t state;
    apta_source_frame_t old_total;
    uint32_t old_signalled;
    apta_source_frame_t old_final;
    apta_status_t status;

    if (session == NULL || final_end_frame == APTA_TOTAL_FRAMES_UNKNOWN) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (session->config.input_mode != APTA_INPUT_MODE_PUSH) {
        return APTA_ERROR_INVALID_STATE;
    }

    if (session->end_of_input_signalled) {
        return session->final_end_frame == final_end_frame
                   ? APTA_STATUS_OK
                   : APTA_ERROR_CONFLICT;
    }

    if (session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        session->config.total_frames != final_end_frame) {
        return APTA_ERROR_CONFLICT;
    }

    state = atomic_load_explicit(&session->state, memory_order_acquire);
    if (state == APTA_SESSION_COMPLETED || state == APTA_SESSION_CANCELLED ||
        state == APTA_SESSION_FAILED) {
        return APTA_ERROR_INVALID_STATE;
    }

    old_total = session->config.total_frames;
    old_signalled = session->end_of_input_signalled;
    old_final = session->final_end_frame;

    session->config.total_frames = final_end_frame;
    session->end_of_input_signalled = 1u;
    session->final_end_frame = final_end_frame;

    status = apta_session_transition(session, APTA_SESSION_DRAINING);
    if (status < 0) {
        session->config.total_frames = old_total;
        session->end_of_input_signalled = old_signalled;
        session->final_end_frame = old_final;
    }

    return status;
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

    if (focus->playhead_frame == APTA_TOTAL_FRAMES_UNKNOWN ||
        (focus->feature_mask & ~session->context->capabilities) != 0u) {
        return (focus->feature_mask & ~session->context->capabilities) != 0u
                   ? APTA_ERROR_UNSUPPORTED
                   : APTA_ERROR_INVALID_ARGUMENT;
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

    for (slot = 0u; slot < APTA_INTERNAL_MAX_REGION_REQUESTS; ++slot) {
        if (session->requests[slot].request_id == 0u) {
            break;
        }
    }

    if (slot == APTA_INTERNAL_MAX_REGION_REQUESTS) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    request_id = request->request_id;
    if (request_id == 0u) {
        request_id = session->next_request_id++;
        if (request_id == 0u) {
            request_id = session->next_request_id++;
        }
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
                    : 0u;
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

    return APTA_STATUS_NOT_AVAILABLE;
}

apta_status_t APTA_CALL apta_session_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out)
{
    apta_session_state_t state;
    apta_status_t status;

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

    if (progress_out != NULL) {
        memset(progress_out, 0, sizeof(*progress_out));
        progress_out->struct_size = (uint32_t)sizeof(*progress_out);
        progress_out->api_version = APTA_API_VERSION;
        progress_out->published_generation = session->generation;
    }

    if (atomic_load_explicit(
            &session->cancel_requested,
            memory_order_acquire) != 0u) {
        status = apta_session_transition(session, APTA_SESSION_CANCELLED);
        atomic_flag_clear_explicit(&session->process_lock, memory_order_release);
        return status < 0 ? status : APTA_ERROR_CANCELLED;
    }

    state = atomic_load_explicit(&session->state, memory_order_acquire);

    if (state == APTA_SESSION_DRAINING) {
        status = apta_session_transition(session, APTA_SESSION_COMPLETED);
        if (progress_out != NULL && status >= 0) {
            progress_out->published_generation = session->generation;
        }
        atomic_flag_clear_explicit(&session->process_lock, memory_order_release);
        return status < 0 ? status : APTA_STATUS_END_OF_INPUT;
    }

    atomic_flag_clear_explicit(&session->process_lock, memory_order_release);

    if (state == APTA_SESSION_COMPLETED) {
        return APTA_STATUS_END_OF_INPUT;
    }
    if (state == APTA_SESSION_CANCELLED) {
        return APTA_ERROR_CANCELLED;
    }
    if (state == APTA_SESSION_FAILED) {
        return APTA_ERROR_INTERNAL;
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
