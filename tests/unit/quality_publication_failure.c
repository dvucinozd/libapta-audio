// SPDX-License-Identifier: Apache-2.0
#include "../../src/core/apta_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t calls;
    uint32_t fail_at;
    uint32_t outstanding;
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
    ++state->calls;
    if (state->calls == state->fail_at) {
        return NULL;
    }
    memory = malloc(size);
    if (memory != NULL) {
        ++state->outstanding;
    }
    return memory;
}

static void APTA_CALL test_deallocate(void *user_data, void *memory)
{
    allocator_state_t *state = (allocator_state_t *)user_data;

    if (memory != NULL) {
        free(memory);
        --state->outstanding;
    }
}

static void prepare_snapshot(
    apta_context_t *context,
    apta_session_t *session,
    apta_result_t *result)
{
    memset(session, 0, sizeof(*session));
    memset(result, 0, sizeof(*result));
    session->context = context;
    session->config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_CALIBRATED_QUALITY;
    atomic_init(&session->state, APTA_SESSION_COMPLETED);
    session->has_tempo = 1u;
    session->final_end_frame = 1000u;
    session->greatest_accepted_end = 750u;
    session->tempo_value.confidence = 80u;
    session->tempo_value.state = APTA_FEATURE_FINAL;
    result->context = context;
    atomic_init(&result->reference_count, 1u);
}

int main(void)
{
    allocator_state_t state = {0u, 0u, 0u};
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_session_t session;
    apta_result_t result;

    apta_context_config_init(&config);
    config.allocator.user_data = &state;
    config.allocator.allocate = test_allocate;
    config.allocator.deallocate = test_deallocate;
    config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_CALIBRATED_QUALITY;
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    CHECK(state.outstanding == 1u);

    prepare_snapshot(context, &session, &result);
    state.fail_at = state.calls + 1u;
    CHECK(apta_internal_s4_build_snapshot(&session, &result) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(result.quality == NULL);
    CHECK(state.outstanding == 1u);

    prepare_snapshot(context, &session, &result);
    state.fail_at = 0u;
    CHECK(apta_internal_s4_build_snapshot(&session, &result) ==
          APTA_STATUS_OK);
    CHECK(result.quality != NULL);
    CHECK(result.quality_count == 1u);
    CHECK(result.quality[0].evidence_coverage_permille == 750u);
    apta_internal_s4_cleanup_result(&result);
    CHECK(state.outstanding == 1u);

    /* If a later S4 allocation fails, rollback must also release the quality
     * record that was already allocated. */
    prepare_snapshot(context, &session, &result);
    session.has_local_grid = 1u;
    state.fail_at = state.calls + 2u;
    CHECK(apta_internal_s4_build_snapshot(&session, &result) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(result.quality == NULL);
    CHECK(state.outstanding == 1u);

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(state.outstanding == 0u);
    return 0;
}
