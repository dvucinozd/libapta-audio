// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

int apta_internal_validate_struct(
    const void *structure,
    size_t minimum_size,
    uint32_t structure_size,
    uint32_t api_version)
{
    return structure != NULL &&
           structure_size >= minimum_size &&
           api_version == APTA_API_VERSION;
}

int apta_internal_is_power_of_two(size_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static int apta_internal_reserve_bytes(
    apta_context_t *context,
    size_t requested_size)
{
    size_t current;
    size_t desired;

    if (context->memory_limit_bytes == 0u) {
        (void)atomic_fetch_add_explicit(
            &context->allocated_bytes,
            requested_size,
            memory_order_relaxed);
        return 1;
    }

    if ((uint64_t)requested_size > context->memory_limit_bytes) {
        return 0;
    }

    current = atomic_load_explicit(
        &context->allocated_bytes,
        memory_order_relaxed);

    for (;;) {
        if ((uint64_t)current > context->memory_limit_bytes - (uint64_t)requested_size) {
            return 0;
        }

        desired = current + requested_size;
        if (atomic_compare_exchange_weak_explicit(
                &context->allocated_bytes,
                &current,
                desired,
                memory_order_acq_rel,
                memory_order_relaxed)) {
            return 1;
        }
    }
}

void *apta_internal_context_allocate(
    apta_context_t *context,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    apta_allocation_header_t *header;
    uintptr_t start;
    uintptr_t aligned;
    void *raw_memory;
    size_t total_size;

    if (context == NULL) {
        return NULL;
    }

    if (size == 0u) {
        size = 1u;
    }

    if (alignment < alignof(void *)) {
        alignment = alignof(void *);
    }

    if (!apta_internal_is_power_of_two(alignment)) {
        return NULL;
    }

    if (size > SIZE_MAX - sizeof(*header) - (alignment - 1u)) {
        return NULL;
    }

    total_size = size + sizeof(*header) + alignment - 1u;

    if (!apta_internal_reserve_bytes(context, size)) {
        return NULL;
    }

    if (context->allocator.allocate != NULL) {
        raw_memory = context->allocator.allocate(
            context->allocator.user_data,
            total_size,
            alignof(max_align_t),
            flags);
    } else {
        (void)flags;
        raw_memory = malloc(total_size);
    }

    if (raw_memory == NULL) {
        (void)atomic_fetch_sub_explicit(
            &context->allocated_bytes,
            size,
            memory_order_relaxed);
        return NULL;
    }

    start = (uintptr_t)raw_memory + sizeof(*header);
    aligned = (start + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
    header = (apta_allocation_header_t *)(aligned - sizeof(*header));

    header->raw_memory = raw_memory;
    header->allocated_size = total_size;
    header->requested_size = size;

    return (void *)aligned;
}

void apta_internal_context_deallocate(
    apta_context_t *context,
    void *memory)
{
    apta_allocation_header_t *header;

    if (context == NULL || memory == NULL) {
        return;
    }

    header = (apta_allocation_header_t *)((uintptr_t)memory - sizeof(*header));

    if (context->allocator.deallocate != NULL) {
        context->allocator.deallocate(
            context->allocator.user_data,
            header->raw_memory);
    } else {
        free(header->raw_memory);
    }

    (void)atomic_fetch_sub_explicit(
        &context->allocated_bytes,
        header->requested_size,
        memory_order_relaxed);
}

void apta_internal_log(
    apta_context_t *context,
    apta_log_level_t level,
    uint32_t diagnostic_code,
    const char *message)
{
    if (context != NULL && context->logger.write != NULL) {
        context->logger.write(
            context->logger.user_data,
            level,
            diagnostic_code,
            message);
    }
}
