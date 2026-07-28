// SPDX-License-Identifier: Apache-2.0
#include "apta_result_pool.h"

#include <stdalign.h>
#include <string.h>

apta_status_t apta_internal_result_pool_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_internal_result_pool_control_t **pool_out)
{
    apta_internal_result_pool_control_t *pool;
    apta_internal_result_pool_layout_t layout;
    apta_status_t status;
    uint32_t slot_index;

    if (pool_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *pool_out = NULL;

    if (context == NULL || config == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_internal_result_pool_calculate_layout(config, &layout);
    if (status < 0) {
        return status;
    }

    pool = (apta_internal_result_pool_control_t *)
        apta_internal_context_allocate(
            context,
            layout.total_bytes,
            alignof(max_align_t),
            APTA_MEMORY_PERSISTENT);
    if (pool == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(pool, 0, layout.total_bytes);
    pool->context = context;
    atomic_init(&pool->reference_count, 1u);
    pool->allocation_size = layout.total_bytes;
    pool->slot_count = layout.slot_count;
    pool->layout = layout;

    for (slot_index = 0u; slot_index < layout.slot_count; ++slot_index) {
        atomic_init(&pool->slots[slot_index].active, 0u);
        pool->slots[slot_index].storage_offset =
            layout.slot_offsets[slot_index];
        pool->slots[slot_index].storage_size = layout.slot_bytes;
    }

    *pool_out = pool;
    return APTA_STATUS_OK;
}

void apta_internal_result_pool_retain(
    apta_internal_result_pool_control_t *pool)
{
    if (pool != NULL) {
        (void)atomic_fetch_add_explicit(
            &pool->reference_count,
            1u,
            memory_order_relaxed);
    }
}

void apta_internal_result_pool_release(
    apta_internal_result_pool_control_t *pool)
{
    apta_context_t *context;

    if (pool == NULL) {
        return;
    }

    if (atomic_fetch_sub_explicit(
            &pool->reference_count,
            1u,
            memory_order_acq_rel) != 1u) {
        return;
    }

    context = pool->context;
    apta_internal_context_deallocate(context, pool);
}

apta_status_t apta_internal_result_pool_create_empty_result(
    apta_internal_result_pool_control_t *pool,
    const apta_session_config_t *config,
    apta_generation_t generation,
    apta_session_state_t session_state,
    apta_feature_mask_t changed_features,
    uint64_t lineage_id_high,
    uint64_t lineage_id_low,
    apta_result_t **result_out)
{
    uint32_t slot_index;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;

    if (pool == NULL || config == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    for (slot_index = 0u; slot_index < pool->slot_count; ++slot_index) {
        unsigned expected = 0u;

        if (atomic_compare_exchange_strong_explicit(
                &pool->slots[slot_index].active,
                &expected,
                1u,
                memory_order_acq_rel,
                memory_order_acquire)) {
            uint8_t *slot_storage =
                (uint8_t *)apta_internal_result_pool_get_slot_storage(
                    pool,
                    slot_index);
            apta_result_t *result;

            if (slot_storage == NULL ||
                pool->layout.result_offset >
                    pool->slots[slot_index].storage_size ||
                sizeof(apta_result_t) >
                    pool->slots[slot_index].storage_size -
                        pool->layout.result_offset) {
                atomic_store_explicit(
                    &pool->slots[slot_index].active,
                    0u,
                    memory_order_release);
                return APTA_ERROR_INTERNAL;
            }

            memset(
                slot_storage,
                0,
                pool->slots[slot_index].storage_size);
            result = (apta_result_t *)(
                slot_storage + pool->layout.result_offset);

            result->context = pool->context;
            atomic_init(&result->reference_count, 1u);
            result->result_pool = pool;
            result->result_pool_slot_index = slot_index;
            result->result_flags = APTA_INTERNAL_RESULT_FLAG_POOLED;
            apta_metadata_view_init(&result->metadata.view);

            result->total_source_frames = config->total_frames;
            result->source_sample_rate = config->source_sample_rate;
            result->source_channel_count = config->channel_count;
            result->source_channel_layout = config->channel_layout;

            result->info.struct_size = (uint32_t)sizeof(result->info);
            result->info.api_version = APTA_API_VERSION;
            result->info.specification_major = APTA_SPEC_VERSION_MAJOR;
            result->info.specification_minor = APTA_SPEC_VERSION_MINOR;
            result->info.producer_api_version = APTA_API_VERSION;
            result->info.container_version = 0u;
            result->info.generation = generation;
            result->info.changed_features = changed_features;
            result->info.session_state = session_state;
            result->info.lineage_id_high = lineage_id_high;
            result->info.lineage_id_low = lineage_id_low;

            apta_internal_result_pool_retain(pool);
            (void)atomic_fetch_add_explicit(
                &pool->context->result_count,
                1u,
                memory_order_acq_rel);

            *result_out = result;
            return APTA_STATUS_OK;
        }
    }

    return APTA_ERROR_RESULT_SLOTS_EXHAUSTED;
}

void apta_internal_result_pool_release_result_slot(
    apta_internal_result_pool_control_t *pool,
    uint32_t slot_index)
{
    if (pool == NULL || slot_index >= pool->slot_count) {
        return;
    }

    if (atomic_exchange_explicit(
            &pool->slots[slot_index].active,
            0u,
            memory_order_acq_rel) == 1u) {
        apta_internal_result_pool_release(pool);
    }
}

size_t apta_internal_result_pool_get_allocation_size(
    const apta_internal_result_pool_control_t *pool)
{
    return pool != NULL ? pool->allocation_size : 0u;
}

uint32_t apta_internal_result_pool_get_slot_count(
    const apta_internal_result_pool_control_t *pool)
{
    return pool != NULL ? pool->slot_count : 0u;
}

const apta_internal_result_pool_layout_t *
apta_internal_result_pool_get_layout(
    const apta_internal_result_pool_control_t *pool)
{
    return pool != NULL ? &pool->layout : NULL;
}

void *apta_internal_result_pool_get_slot_storage(
    apta_internal_result_pool_control_t *pool,
    uint32_t slot_index)
{
    size_t offset;
    size_t size;

    if (pool == NULL || slot_index >= pool->slot_count) {
        return NULL;
    }

    offset = pool->slots[slot_index].storage_offset;
    size = pool->slots[slot_index].storage_size;
    if (offset > pool->allocation_size ||
        size > pool->allocation_size - offset) {
        return NULL;
    }

    return (uint8_t *)pool + offset;
}
