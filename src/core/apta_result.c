// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <limits.h>
#include <stdalign.h>
#include <string.h>

static void apta_internal_lock(atomic_flag *lock)
{
    while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire)) {
        /* Portable core spin lock: critical sections are pointer-sized and bounded. */
    }
}

static void apta_internal_unlock(atomic_flag *lock)
{
    atomic_flag_clear_explicit(lock, memory_order_release);
}

void apta_internal_result_retain(apta_result_t *result)
{
    if (result != NULL) {
        (void)atomic_fetch_add_explicit(
            &result->reference_count,
            1u,
            memory_order_relaxed);
    }
}

void apta_internal_result_release(apta_result_t *result)
{
    apta_context_t *context;

    if (result == NULL) {
        return;
    }

    if (atomic_fetch_sub_explicit(
            &result->reference_count,
            1u,
            memory_order_acq_rel) != 1u) {
        return;
    }

    context = result->context;
    (void)atomic_fetch_sub_explicit(
        &context->result_count,
        1u,
        memory_order_acq_rel);
    apta_internal_context_deallocate(context, result);
}

apta_status_t apta_internal_publish_result(
    apta_session_t *session,
    apta_feature_mask_t changed_features)
{
    apta_result_t *result;
    apta_result_t *old_result;
    apta_generation_t next_generation;

    if (session == NULL || session->context == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (session->generation == UINT64_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    result = (apta_result_t *)apta_internal_context_allocate(
        session->context,
        sizeof(*result),
        alignof(apta_result_t),
        APTA_MEMORY_PERSISTENT);
    if (result == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(result, 0, sizeof(*result));
    result->context = session->context;
    atomic_init(&result->reference_count, 1u);

    next_generation = session->generation + 1u;
    result->info.struct_size = (uint32_t)sizeof(result->info);
    result->info.api_version = APTA_API_VERSION;
    result->info.specification_major = APTA_SPEC_VERSION_MAJOR;
    result->info.specification_minor = APTA_SPEC_VERSION_MINOR;
    result->info.producer_api_version = APTA_API_VERSION;
    result->info.container_version = 0u;
    result->info.generation = next_generation;
    result->info.available_features = 0u;
    result->info.changed_features = changed_features;
    result->info.session_state = atomic_load_explicit(
        &session->state,
        memory_order_acquire);
    result->info.diagnostic_count = 0u;
    result->info.flags = 0u;
    result->info.lineage_id_high = session->lineage_id_high;
    result->info.lineage_id_low = session->lineage_id_low;

    (void)atomic_fetch_add_explicit(
        &session->context->result_count,
        1u,
        memory_order_acq_rel);

    apta_internal_lock(&session->result_lock);
    old_result = session->current_result;
    session->current_result = result;
    session->generation = next_generation;
    apta_internal_unlock(&session->result_lock);

    apta_internal_result_release(old_result);
    return APTA_STATUS_OK;
}

const apta_result_t *APTA_CALL apta_session_acquire_result(
    const apta_session_t *session)
{
    apta_result_t *result;
    apta_session_t *mutable_session;

    if (session == NULL) {
        return NULL;
    }

    mutable_session = (apta_session_t *)session;
    apta_internal_lock(&mutable_session->result_lock);
    result = mutable_session->current_result;
    apta_internal_result_retain(result);
    apta_internal_unlock(&mutable_session->result_lock);

    return result;
}

void APTA_CALL apta_result_release(const apta_result_t *result)
{
    apta_internal_result_release((apta_result_t *)result);
}

apta_status_t APTA_CALL apta_result_get_info(
    const apta_result_t *result,
    apta_result_info_t *info_out)
{
    if (result == NULL || info_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (!apta_internal_validate_struct(
            info_out,
            sizeof(*info_out),
            info_out->struct_size,
            info_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    memcpy(info_out, &result->info, sizeof(*info_out));
    return APTA_STATUS_OK;
}

apta_generation_t APTA_CALL apta_result_get_generation(
    const apta_result_t *result)
{
    return result != NULL ? result->info.generation : 0u;
}

apta_feature_mask_t APTA_CALL apta_result_get_available_features(
    const apta_result_t *result)
{
    return result != NULL ? result->info.available_features : 0u;
}

apta_status_t APTA_CALL apta_result_get_feature_state(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out)
{
    if (result == NULL || feature == 0u || state_out == NULL || confidence_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (range != NULL &&
        !apta_internal_validate_struct(
            range,
            sizeof(*range),
            range->struct_size,
            range->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    *state_out = APTA_FEATURE_ABSENT;
    *confidence_out = APTA_CONFIDENCE_UNKNOWN;
    return APTA_STATUS_NOT_AVAILABLE;
}

static apta_status_t apta_internal_prepare_unavailable_view(
    void *view,
    size_t view_size,
    uint32_t struct_size,
    uint32_t api_version)
{
    if (view == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (!apta_internal_validate_struct(
            view,
            view_size,
            struct_size,
            api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    memset(view, 0, view_size);
    *(uint32_t *)view = (uint32_t)view_size;
    *((uint32_t *)view + 1) = APTA_API_VERSION;
    return APTA_STATUS_NOT_AVAILABLE;
}

apta_status_t APTA_CALL apta_result_get_waveform_overview(
    const apta_result_t *result,
    uint32_t level_id,
    apta_waveform_overview_view_t *view_out)
{
    (void)level_id;
    if (result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    return apta_internal_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}

apta_status_t APTA_CALL apta_result_get_waveform_tile(
    const apta_result_t *result,
    uint32_t level_id,
    uint32_t tile_index,
    apta_waveform_tile_view_t *view_out)
{
    (void)level_id;
    (void)tile_index;
    if (result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    return apta_internal_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}

apta_status_t APTA_CALL apta_result_get_tempo(
    const apta_result_t *result,
    const apta_frame_range_t *range,
    apta_tempo_view_t *view_out)
{
    if (result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (range != NULL &&
        !apta_internal_validate_struct(
            range,
            sizeof(*range),
            range->struct_size,
            range->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    return apta_internal_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}

apta_status_t APTA_CALL apta_result_get_beatgrid(
    const apta_result_t *result,
    apta_feature_mask_t grid_feature,
    const apta_frame_range_t *range,
    apta_grid_view_t *view_out)
{
    if (result == NULL ||
        (grid_feature != APTA_FEATURE_LOCAL_BEATGRID &&
         grid_feature != APTA_FEATURE_GLOBAL_BEATGRID)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (range != NULL &&
        !apta_internal_validate_struct(
            range,
            sizeof(*range),
            range->struct_size,
            range->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    return apta_internal_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}

uint32_t APTA_CALL apta_result_get_diagnostic_count(
    const apta_result_t *result)
{
    return result != NULL ? result->info.diagnostic_count : 0u;
}

apta_status_t APTA_CALL apta_result_get_diagnostic(
    const apta_result_t *result,
    uint32_t index,
    apta_diagnostic_view_t *view_out)
{
    (void)index;
    if (result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    return apta_internal_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}
