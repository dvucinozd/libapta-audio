// SPDX-License-Identifier: Apache-2.0
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t allocation_count;
    uint32_t deallocation_count;
    uint32_t outstanding;
} tracking_allocator_t;

static void *APTA_CALL tracking_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    tracking_allocator_t *tracking = (tracking_allocator_t *)user_data;
    void *memory;

    (void)alignment;
    (void)flags;
    memory = malloc(size);
    if (memory != NULL) {
        tracking->allocation_count += 1u;
        tracking->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL tracking_deallocate(
    void *user_data,
    void *memory)
{
    tracking_allocator_t *tracking = (tracking_allocator_t *)user_data;

    if (memory != NULL) {
        free(memory);
        tracking->deallocation_count += 1u;
        tracking->outstanding -= 1u;
    }
}

int main(void)
{
    tracking_allocator_t tracking = {0u, 0u, 0u};
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_result_t *result;
    uint64_t expected_bytes;
    uint64_t allocation_bytes = 0u;

    apta_context_config_init(&config);
    config.allocator.user_data = &tracking;
    config.allocator.allocate = tracking_allocate;
    config.allocator.deallocate = tracking_deallocate;
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    CHECK(tracking.outstanding == 1u);

    result = (apta_result_t *)apta_internal_context_allocate(
        context, sizeof(*result), alignof(apta_result_t),
        APTA_MEMORY_PERSISTENT);
    CHECK(result != NULL);
    memset(result, 0, sizeof(*result));
    result->context = context;
    atomic_init(&result->reference_count, 1u);
    apta_metadata_view_init(&result->metadata.view);
    apta_key_view_init(&result->key);
    apta_meter_view_init(&result->meter);

    result->key.candidate_count = 2u;
    result->key_candidates =
        (apta_key_candidate_t *)apta_internal_context_allocate(
            context,
            2u * sizeof(*result->key_candidates),
            alignof(apta_key_candidate_t),
            APTA_MEMORY_PERSISTENT);
    CHECK(result->key_candidates != NULL);
    result->key.candidates = result->key_candidates;

    result->meter.segment_count = 3u;
    result->meter_segments =
        (apta_meter_segment_t *)apta_internal_context_allocate(
            context,
            3u * sizeof(*result->meter_segments),
            alignof(apta_meter_segment_t),
            APTA_MEMORY_PERSISTENT);
    CHECK(result->meter_segments != NULL);
    result->meter.segments = result->meter_segments;

    result->quality_count = 2u;
    result->quality = (apta_quality_view_t *)apta_internal_context_allocate(
        context,
        2u * sizeof(*result->quality),
        alignof(apta_quality_view_t),
        APTA_MEMORY_PERSISTENT);
    CHECK(result->quality != NULL);

    expected_bytes = sizeof(*result) +
                     2u * sizeof(*result->key_candidates) +
                     3u * sizeof(*result->meter_segments) +
                     2u * sizeof(*result->quality);
    CHECK(apta_internal_result_allocation_bytes(
              result, &allocation_bytes));
    CHECK(allocation_bytes == expected_bytes);
    CHECK(apta_internal_result_allocation_fits(
              result, 0u, expected_bytes));
    CHECK(!apta_internal_result_allocation_fits(
              result, 1u, expected_bytes));
    CHECK(!apta_internal_result_allocation_fits(
              result, UINT64_MAX, UINT64_MAX));
    CHECK(!apta_internal_result_allocation_bytes(NULL, &allocation_bytes));
    CHECK(!apta_internal_result_allocation_bytes(result, NULL));

    (void)atomic_fetch_add_explicit(
        &context->result_count, 1u, memory_order_acq_rel);
    CHECK(tracking.outstanding == 5u);
    apta_result_release(result);
    CHECK(tracking.outstanding == 1u);
    CHECK(tracking.deallocation_count == 4u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(tracking.outstanding == 0u);
    CHECK(tracking.deallocation_count == tracking.allocation_count);
    return 0;
}
