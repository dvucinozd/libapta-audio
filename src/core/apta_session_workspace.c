// SPDX-License-Identifier: Apache-2.0
#include "apta_session_workspace.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

typedef union apta_internal_workspace_block
    apta_internal_workspace_block_t;

union apta_internal_workspace_block {
    struct {
        apta_internal_workspace_block_t *next;
        size_t capacity;
        size_t requested_size;
        uint32_t free;
        uint32_t reserved32;
    } state;
    max_align_t force_alignment;
};

static size_t apta_workspace_align_up(size_t value, size_t alignment)
{
    if (value > SIZE_MAX - (alignment - 1u)) {
        return SIZE_MAX;
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static size_t apta_workspace_payload_prefix_size(void)
{
    size_t payload_offset;

    payload_offset = apta_workspace_align_up(
        sizeof(apta_internal_workspace_block_t) +
            sizeof(apta_allocation_header_t),
        alignof(max_align_t));
    if (payload_offset == SIZE_MAX) {
        return SIZE_MAX;
    }
    return payload_offset - sizeof(apta_internal_workspace_block_t);
}

static void *apta_workspace_block_payload(
    apta_internal_workspace_block_t *block)
{
    size_t prefix_size = apta_workspace_payload_prefix_size();

    if (block == NULL || prefix_size == SIZE_MAX) {
        return NULL;
    }
    return (uint8_t *)(block + 1) + prefix_size;
}

static apta_internal_workspace_block_t *apta_workspace_first_block(
    const apta_session_t *session)
{
    uintptr_t base;
    size_t offset;

    base = (uintptr_t)session->config.static_workspace;
    offset = apta_workspace_align_up(
        sizeof(*session),
        alignof(max_align_t));
    if (offset == SIZE_MAX) {
        return NULL;
    }
    return (apta_internal_workspace_block_t *)(base + offset);
}

size_t apta_internal_session_workspace_minimum_size(void)
{
    size_t offset;
    size_t prefix_size;

    offset = apta_workspace_align_up(
        sizeof(apta_session_t),
        alignof(max_align_t));
    prefix_size = apta_workspace_payload_prefix_size();
    if (offset == SIZE_MAX || prefix_size == SIZE_MAX ||
        offset > SIZE_MAX - sizeof(apta_internal_workspace_block_t) -
                     prefix_size - alignof(max_align_t)) {
        return SIZE_MAX;
    }

    return offset + sizeof(apta_internal_workspace_block_t) +
           prefix_size + alignof(max_align_t);
}

int apta_internal_session_uses_workspace(
    const apta_session_t *session)
{
    return session != NULL &&
           session->config.static_workspace != NULL &&
           session->config.static_workspace == (const void *)session;
}

apta_status_t apta_internal_session_workspace_initialize(
    apta_session_t *session)
{
    apta_internal_workspace_block_t *block;
    size_t minimum_size;
    size_t offset;

    if (session == NULL || !apta_internal_session_uses_workspace(session)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    minimum_size = apta_internal_session_workspace_minimum_size();
    if (minimum_size == SIZE_MAX ||
        session->config.static_workspace_size < minimum_size) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    if (((uintptr_t)session->config.static_workspace &
         (uintptr_t)(alignof(max_align_t) - 1u)) != 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    offset = apta_workspace_align_up(
        sizeof(*session),
        alignof(max_align_t));
    block = apta_workspace_first_block(session);
    if (block == NULL) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    memset(block, 0, sizeof(*block));
    block->state.capacity =
        session->config.static_workspace_size - offset - sizeof(*block);
    block->state.free = 1u;
    return APTA_STATUS_OK;
}

void *apta_internal_session_allocate(
    apta_session_t *session,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    apta_internal_workspace_block_t *block;
    size_t padded_size;
    size_t prefix_size;
    size_t required;

    if (session == NULL) {
        return NULL;
    }

    if (!apta_internal_session_uses_workspace(session)) {
        return apta_internal_context_allocate(
            session->context,
            size,
            alignment,
            flags);
    }

    (void)flags;
    if (size == 0u) {
        size = 1u;
    }
    if (alignment < alignof(void *)) {
        alignment = alignof(void *);
    }
    if (!apta_internal_is_power_of_two(alignment) ||
        alignment > alignof(max_align_t)) {
        return NULL;
    }

    padded_size = apta_workspace_align_up(size, alignof(max_align_t));
    prefix_size = apta_workspace_payload_prefix_size();
    if (padded_size == SIZE_MAX || prefix_size == SIZE_MAX ||
        prefix_size > SIZE_MAX - padded_size) {
        return NULL;
    }
    required = prefix_size + padded_size;

    for (block = apta_workspace_first_block(session);
         block != NULL;
         block = block->state.next) {
        size_t remaining;

        if (block->state.free == 0u ||
            block->state.capacity < required) {
            continue;
        }

        remaining = block->state.capacity - required;
        if (remaining >= sizeof(*block) +
                             prefix_size + alignof(max_align_t)) {
            apta_internal_workspace_block_t *next;
            uint8_t *next_address;

            next_address = (uint8_t *)(block + 1) + required;
            next = (apta_internal_workspace_block_t *)next_address;
            memset(next, 0, sizeof(*next));
            next->state.next = block->state.next;
            next->state.capacity = remaining - sizeof(*next);
            next->state.free = 1u;

            block->state.next = next;
            block->state.capacity = required;
        }

        block->state.requested_size = size;
        block->state.free = 0u;
        {
            void *payload = apta_workspace_block_payload(block);
            apta_allocation_header_t *header;

            if (payload == NULL) {
                block->state.requested_size = 0u;
                block->state.free = 1u;
                return NULL;
            }
            header = (apta_allocation_header_t *)
                ((uintptr_t)payload - sizeof(*header));
            header->raw_memory = session;
            header->allocated_size = 0u;
            header->requested_size = size;
            return payload;
        }
    }

    return NULL;
}

void apta_internal_session_deallocate(
    apta_session_t *session,
    void *memory)
{
    apta_internal_workspace_block_t *block;
    apta_internal_workspace_block_t *previous;

    if (session == NULL || memory == NULL) {
        return;
    }

    if (!apta_internal_session_uses_workspace(session)) {
        apta_internal_context_deallocate(session->context, memory);
        return;
    }

    previous = NULL;
    for (block = apta_workspace_first_block(session);
         block != NULL;
         block = block->state.next) {
        if (apta_workspace_block_payload(block) == memory) {
            break;
        }
        previous = block;
    }

    if (block == NULL || block->state.free != 0u) {
        return;
    }

    block->state.requested_size = 0u;
    block->state.free = 1u;

    if (block->state.next != NULL &&
        block->state.next->state.free != 0u) {
        apta_internal_workspace_block_t *next = block->state.next;
        block->state.capacity += sizeof(*next) + next->state.capacity;
        block->state.next = next->state.next;
    }

    if (previous != NULL && previous->state.free != 0u) {
        previous->state.capacity += sizeof(*block) + block->state.capacity;
        previous->state.next = block->state.next;
    }
}
