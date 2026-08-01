// SPDX-License-Identifier: Apache-2.0
#include "apta_session_workspace.h"

#include "../beatgrid/apta_s6_internal.h"

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
    apta_internal_max_align_t force_alignment;
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
        APTA_INTERNAL_MAX_ALIGNMENT);
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
        APTA_INTERNAL_MAX_ALIGNMENT);
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
        APTA_INTERNAL_MAX_ALIGNMENT);
    prefix_size = apta_workspace_payload_prefix_size();
    if (offset == SIZE_MAX || prefix_size == SIZE_MAX ||
        offset > SIZE_MAX - sizeof(apta_internal_workspace_block_t) -
                     prefix_size - APTA_INTERNAL_MAX_ALIGNMENT) {
        return SIZE_MAX;
    }

    return offset + sizeof(apta_internal_workspace_block_t) +
           prefix_size + APTA_INTERNAL_MAX_ALIGNMENT;
}

/* A5: cost of one workspace allocation of `size` bytes, including the block
 * header and payload prefix the allocator places in front of every payload and
 * the padding it applies to the payload itself. Derived from
 * apta_internal_session_allocate() rather than tabulated by hand. */
static size_t apta_workspace_allocation_cost(size_t size)
{
    size_t padded;
    size_t prefix;

    if (size == 0u) {
        size = 1u;
    }
    padded = apta_workspace_align_up(size, APTA_INTERNAL_MAX_ALIGNMENT);
    prefix = apta_workspace_payload_prefix_size();
    if (padded == SIZE_MAX || prefix == SIZE_MAX) {
        return SIZE_MAX;
    }
    return sizeof(apta_internal_workspace_block_t) + prefix + padded;
}

static size_t apta_workspace_add(size_t total, size_t addend)
{
    if (total == SIZE_MAX || addend == SIZE_MAX ||
        total > SIZE_MAX - addend) {
        return SIZE_MAX;
    }
    return total + addend;
}

/* Both growable arrays double and copy: the replacement is allocated before the
 * previous array is released. The freed fragments cannot serve the next
 * request, because each request is strictly larger than everything released so
 * far -- a doubling sequence 16, 32, ... C frees a total of C - 16, always less
 * than the C the final growth asks for. So the whole sequence has to be
 * charged, not just the last step. Measured: charging only the last two steps
 * under-reports a 5-minute full-feature configuration by about 230 KiB against
 * a bisected true requirement. */
static size_t apta_workspace_growable_cost(
    uint64_t needed_entries,
    size_t entry_size,
    uint32_t initial_capacity)
{
    uint64_t capacity = initial_capacity;
    size_t cost = 0u;

    if (needed_entries == 0u) {
        return 0u;
    }
    for (;;) {
        if (capacity > (uint64_t)(SIZE_MAX / entry_size)) {
            return SIZE_MAX;
        }
        cost = apta_workspace_add(
            cost,
            apta_workspace_allocation_cost((size_t)capacity * entry_size));
        if (cost == SIZE_MAX || capacity >= needed_entries) {
            break;
        }
        if (capacity > (uint64_t)UINT32_MAX / 2u) {
            return SIZE_MAX;
        }
        capacity *= 2u;
    }
    return cost;
}

size_t apta_internal_session_workspace_requirement(
    const apta_session_config_t *config)
{
    const apta_feature_mask_t features = config->requested_features;
    /* C2: follow the session's chosen resolution. */
    const uint32_t frames_per_column =
        config->overview_frames_per_column != 0u
            ? config->overview_frames_per_column
            : APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN;
    uint64_t overview_columns;
    size_t total;

    total = apta_workspace_align_up(
        sizeof(apta_session_t),
        APTA_INTERNAL_MAX_ALIGNMENT);
    if (total == SIZE_MAX) {
        return SIZE_MAX;
    }

    /* Overview accumulators: one per column across the track. This is the only
     * contributor that scales with duration, and for a multi-minute track it is
     * the largest single item. */
    if (config->total_frames != 0u) {
        overview_columns =
            (config->total_frames + (uint64_t)frames_per_column - 1u) /
            (uint64_t)frames_per_column;
    } else {
        overview_columns = 0u;
    }
    total = apta_workspace_add(
        total,
        apta_workspace_growable_cost(
            overview_columns,
            sizeof(apta_internal_waveform_accumulator_t),
            16u));

    /* C1: the band sums grow alongside the accumulators, and only exist when
     * bands were requested. */
    if ((features & APTA_FEATURE_WAVEFORM_3BAND) != 0u) {
        total = apta_workspace_add(
            total,
            apta_workspace_growable_cost(
                overview_columns,
                APTA_INTERNAL_BAND_COUNT * sizeof(uint32_t),
                16u));
    }

    /* Accepted-range table. One range per contiguous accepted run; a host that
     * pushes in order needs one, but fragmentation costs more. Charge the
     * scheduler's request capacity as a working bound. */
    total = apta_workspace_add(
        total,
        apta_workspace_growable_cost(
            APTA_INTERNAL_MAX_REGION_REQUESTS,
            sizeof(apta_internal_range_t),
            8u));

    /* PCM queue high-water mark. Nodes are released as they are consumed, but
     * a host may push a full block before the next process() call. */
    total = apta_workspace_add(
        total,
        apta_workspace_allocation_cost(
            sizeof(apta_internal_pcm_node_t) +
            (size_t)APTA_INTERNAL_MAX_PUSH_FRAMES * sizeof(float)));

    if ((features & APTA_FEATURE_WAVEFORM_DETAIL) != 0u) {
        total = apta_workspace_add(
            total,
            apta_workspace_allocation_cost(
                (size_t)APTA_INTERNAL_MAX_DETAIL_TILES *
                APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE *
                sizeof(apta_internal_waveform_accumulator_t)));
    }

    if ((features & APTA_INTERNAL_S4_FEATURES) != 0u) {
        total = apta_workspace_add(
            total,
            apta_workspace_allocation_cost(
                (size_t)APTA_INTERNAL_ONSET_BIN_CAPACITY *
                sizeof(apta_internal_onset_bin_t)));
        total = apta_workspace_add(
            total,
            apta_workspace_allocation_cost(
                (size_t)APTA_INTERNAL_ONSET_BIN_CAPACITY * sizeof(float)));
    }

    if ((features & APTA_INTERNAL_S6_FEATURES) != 0u) {
        total = apta_workspace_add(
            total,
            apta_workspace_allocation_cost(
                sizeof(apta_internal_s6_session_state_t)));
        total = apta_workspace_add(
            total,
            apta_workspace_allocation_cost(
                (size_t)APTA_INTERNAL_GLOBAL_BIN_CAPACITY *
                sizeof(apta_internal_onset_bin_t)));
        total = apta_workspace_add(
            total,
            apta_workspace_allocation_cost(
                (size_t)APTA_INTERNAL_GLOBAL_BIN_CAPACITY * sizeof(float)));
        total = apta_workspace_add(
            total,
            apta_workspace_allocation_cost(
                (size_t)APTA_INTERNAL_GLOBAL_MAX_BEATS *
                sizeof(apta_beat_t)));
    }

    /* Session metadata storage, which a host may set at any time. */
    total = apta_workspace_add(
        total,
        apta_workspace_allocation_cost(APTA_METADATA_MAX_TOTAL_BYTES));

    /* One spare block header plus alignment, matching the tail slack the
     * constant minimum already reserved. */
    total = apta_workspace_add(
        total,
        sizeof(apta_internal_workspace_block_t) + APTA_INTERNAL_MAX_ALIGNMENT);

    return total;
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
         (uintptr_t)(APTA_INTERNAL_MAX_ALIGNMENT - 1u)) != 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    offset = apta_workspace_align_up(
        sizeof(*session),
        APTA_INTERNAL_MAX_ALIGNMENT);
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
        alignment > APTA_INTERNAL_MAX_ALIGNMENT) {
        return NULL;
    }

    padded_size = apta_workspace_align_up(
        size,
        APTA_INTERNAL_MAX_ALIGNMENT);
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
                             prefix_size + APTA_INTERNAL_MAX_ALIGNMENT) {
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
