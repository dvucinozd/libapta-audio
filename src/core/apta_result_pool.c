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
