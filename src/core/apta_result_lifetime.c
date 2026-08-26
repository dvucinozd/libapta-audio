// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"
#include "apta_result_pool.h"

#include <stdalign.h>
#include <string.h>

static void apta_result_lock(atomic_flag *lock)
{
    while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire)) {
    }
}

static void apta_result_unlock(atomic_flag *lock)
{
    atomic_flag_clear_explicit(lock, memory_order_release);
}

static void apta_result_mark_waveform_publication_pending(
    apta_session_t *session)
{
    uint32_t index;

    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        session->overview_accumulators[index].complete = 0u;
    }
    session->overview_complete_count = 0u;
}

void apta_internal_result_init_absent_views(apta_result_t *result)
{
    if (result != NULL) {
        apta_metadata_view_init(&result->metadata.view);
        apta_result_provenance_init(&result->provenance);
        result->provenance_storage = NULL;
        result->provenance_storage_size = 0u;
        apta_key_view_init(&result->key);
        apta_meter_view_init(&result->meter);
        result->key_candidates = NULL;
        result->meter_segments = NULL;
        result->quality_count = 0u;
        result->quality = NULL;
    }
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
    if ((result->result_flags &
         APTA_INTERNAL_RESULT_FLAG_POOLED) != 0u) {
        apta_internal_result_pool_control_t *pool =
            result->result_pool;
        uint32_t slot_index = result->result_pool_slot_index;

        (void)atomic_fetch_sub_explicit(
            &context->result_count,
            1u,
            memory_order_acq_rel);
        apta_internal_result_pool_release_result_slot(
            pool,
            slot_index);
        return;
    }

    apta_internal_metadata_cleanup(context, &result->metadata);
    apta_internal_context_deallocate(context, result->provenance_storage);
    apta_internal_context_deallocate(context, result->key_candidates);
    apta_internal_context_deallocate(context, result->meter_segments);
    apta_internal_waveform_cleanup_result(result);
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
    apta_status_t status;

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
        apta_result_mark_waveform_publication_pending(session);
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(result, 0, sizeof(*result));
    result->context = session->context;
    atomic_init(&result->reference_count, 1u);
    apta_internal_result_init_absent_views(result);

    apta_source_info_init(&result->source_info);
    result->source_info.total_frames = session->config.total_frames;
    result->source_info.sample_rate = session->config.source_sample_rate;
    result->source_info.channel_count = session->config.channel_count;
    result->source_info.channel_layout = session->config.channel_layout;
    result->source_info.fingerprint_kind =
        session->config.source_fingerprint_kind;
    memcpy(
        result->source_info.fingerprint,
        session->config.source_fingerprint,
        APTA_SOURCE_FINGERPRINT_SIZE);

    result->total_source_frames = result->source_info.total_frames;
    result->source_sample_rate = result->source_info.sample_rate;
    result->source_channel_count = result->source_info.channel_count;
    result->source_channel_layout = result->source_info.channel_layout;

    next_generation = session->generation + 1u;
    result->info.struct_size = (uint32_t)sizeof(result->info);
    result->info.api_version = APTA_API_VERSION;
    result->info.specification_major = APTA_SPEC_VERSION_MAJOR;
    result->info.specification_minor = APTA_SPEC_VERSION_MINOR;
    result->info.producer_api_version = APTA_API_VERSION;
    result->info.container_version = 0u;
    result->info.generation = next_generation;
    result->info.changed_features = changed_features;
    result->info.session_state = atomic_load_explicit(
        &session->state,
        memory_order_acquire);
    result->info.lineage_id_high = session->lineage_id_high;
    result->info.lineage_id_low = session->lineage_id_low;

    if (apta_internal_metadata_is_present(&session->metadata)) {
        status = apta_internal_metadata_copy_from_view(
            session->context,
            &session->metadata.view,
            &result->metadata);
        if (status < 0) {
            apta_internal_context_deallocate(session->context, result);
            if (status == APTA_ERROR_OUT_OF_MEMORY) {
                apta_result_mark_waveform_publication_pending(session);
            }
            return status;
        }
    }

    status = apta_internal_waveform_build_snapshot(session, result);
    if (status < 0) {
        apta_internal_metadata_cleanup(
            session->context,
            &result->metadata);
        apta_internal_waveform_cleanup_result(result);
        apta_internal_context_deallocate(session->context, result);
        if (status == APTA_ERROR_OUT_OF_MEMORY) {
            apta_result_mark_waveform_publication_pending(session);
        }
        return status;
    }

    (void)atomic_fetch_add_explicit(
        &session->context->result_count,
        1u,
        memory_order_acq_rel);

    apta_result_lock(&session->result_lock);
    old_result = session->current_result;
    session->current_result = result;
    session->generation = next_generation;
    apta_result_unlock(&session->result_lock);

    apta_internal_result_release(old_result);
    return APTA_STATUS_OK;
}

const apta_result_t *APTA_CALL apta_session_acquire_result(
    const apta_session_t *session)
{
    apta_session_t *mutable_session;
    apta_result_t *result;

    if (session == NULL) {
        return NULL;
    }

    mutable_session = (apta_session_t *)session;
    apta_result_lock(&mutable_session->result_lock);
    result = mutable_session->current_result;
    apta_internal_result_retain(result);
    apta_result_unlock(&mutable_session->result_lock);
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

apta_status_t APTA_CALL apta_result_get_source_info(
    const apta_result_t *result,
    apta_source_info_t *info_out)
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

    memcpy(info_out, &result->source_info, sizeof(*info_out));
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
