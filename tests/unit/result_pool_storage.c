// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_result_pool.h"

#include "apta_test_geometry.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t allocate_calls;
    uint32_t outstanding;
    uint32_t fail_at;
} allocator_state_t;

static void *APTA_CALL test_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    void *memory;

    (void)alignment;
    (void)flags;
    state->allocate_calls += 1u;
    if (state->fail_at != 0u &&
        state->allocate_calls == state->fail_at) {
        return NULL;
    }

    memory = malloc(size);
    if (memory != NULL) {
        state->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL test_deallocate(
    void *user_data,
    void *memory)
{
    allocator_state_t *state = (allocator_state_t *)user_data;

    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

static void configure_session(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = 48000u;
    config->channel_count = 1u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config->total_frames = 4096u;
    config->requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    config->flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
}

static void configure_context(
    apta_context_config_t *config,
    allocator_state_t *state,
    uint64_t memory_limit)
{
    apta_context_config_init(config);
    config->allocator.user_data = state;
    config->allocator.allocate = test_allocate;
    config->allocator.deallocate = test_deallocate;
    config->requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    config->memory_limit_bytes = memory_limit;
}

int main(void)
{
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_internal_result_pool_control_t *pool = NULL;
    const apta_internal_result_pool_layout_t *layout;
    allocator_state_t state;
    void *slot0;
    void *slot1;

    configure_session(&session_config);
    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(
              &session_config,
              &requirements) == APTA_STATUS_OK);
    CHECK(requirements.minimum_bytes == requirements.recommended_bytes);
    CHECK(requirements.minimum_bytes > 0u);

    memset(&state, 0, sizeof(state));
    configure_context(&context_config, &state, 0u);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(state.allocate_calls == 1u);
    CHECK(state.outstanding == 1u);

    CHECK(apta_internal_result_pool_create(
              context,
              &session_config,
              &pool) == APTA_STATUS_OK);
    CHECK(pool != NULL);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 2u);
    CHECK(apta_internal_result_pool_get_allocation_size(pool) ==
          requirements.minimum_bytes);
    CHECK(apta_internal_result_pool_get_slot_count(pool) == 2u);

    layout = apta_internal_result_pool_get_layout(pool);
    CHECK(layout != NULL);
    CHECK(layout->total_bytes == requirements.minimum_bytes);
    CHECK(layout->slot_count == 2u);
    CHECK(layout->slot_bytes > sizeof(apta_result_t));
    CHECK(layout->overview_column_capacity ==
          (4096u + APTA_TEST_COLUMN_FRAMES - 1u) / APTA_TEST_COLUMN_FRAMES);
    CHECK(layout->overview_span_capacity == 4u);
    CHECK(layout->detail_tile_capacity == 4u);
    CHECK(layout->detail_column_capacity == 256u);
    CHECK(layout->metadata_capacity == APTA_METADATA_MAX_TOTAL_BYTES);

    slot0 = apta_internal_result_pool_get_slot_storage(pool, 0u);
    slot1 = apta_internal_result_pool_get_slot_storage(pool, 1u);
    CHECK(slot0 != NULL);
    CHECK(slot1 != NULL);
    CHECK(slot0 != slot1);
    CHECK(((uintptr_t)slot0 & (uintptr_t)(requirements.required_alignment - 1u)) == 0u);
    CHECK(((uintptr_t)slot1 & (uintptr_t)(requirements.required_alignment - 1u)) == 0u);
    CHECK((uintptr_t)slot1 - (uintptr_t)slot0 == layout->slot_bytes);
    CHECK(apta_internal_result_pool_get_slot_storage(pool, 2u) == NULL);

    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);
    apta_internal_result_pool_retain(pool);
    apta_internal_result_pool_release(pool);
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);
    apta_internal_result_pool_release(pool);
    pool = NULL;
    CHECK(state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(state.outstanding == 0u);

    memset(&state, 0, sizeof(state));
    state.fail_at = 2u;
    configure_context(&context_config, &state, 0u);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_internal_result_pool_create(
              context,
              &session_config,
              &pool) == APTA_ERROR_OUT_OF_MEMORY);
    CHECK(pool == NULL);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(state.outstanding == 0u);

    memset(&state, 0, sizeof(state));
    configure_context(
        &context_config,
        &state,
        (uint64_t)requirements.minimum_bytes - 1u);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_internal_result_pool_create(
              context,
              &session_config,
              &pool) == APTA_ERROR_OUT_OF_MEMORY);
    CHECK(pool == NULL);
    CHECK(state.allocate_calls == 1u);
    CHECK(state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(state.outstanding == 0u);
    return 0;
}
