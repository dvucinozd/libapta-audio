// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define COL APTA_TEST_COLUMN_FRAMES

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t armed;
    uint32_t armed_allocation_count;
    uint32_t failed_once;
    uint32_t outstanding;
} retry_allocator_state_t;

static void *APTA_CALL retry_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    retry_allocator_state_t *state = (retry_allocator_state_t *)user_data;
    void *memory;

    (void)alignment;
    (void)flags;

    if (state->armed && !state->failed_once) {
        state->armed_allocation_count += 1u;

        /*
         * The first allocation is the new immutable result object. The second
         * is the waveform-column payload. Failing by allocation order avoids
         * depending on the allocator wrapper's private header size, pointer
         * width or alignment padding.
         */
        if (state->armed_allocation_count == 2u) {
            state->failed_once = 1u;
            return NULL;
        }
    }

    memory = malloc(size);
    if (memory != NULL) {
        state->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL retry_deallocate(
    void *user_data,
    void *memory)
{
    retry_allocator_state_t *state = (retry_allocator_state_t *)user_data;

    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

int main(void)
{
    retry_allocator_state_t allocator_state = {0u, 0u, 0u, 0u};
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_waveform_overview_view_t overview;
    const apta_result_t *result = NULL;
    int16_t pcm[4u * COL] = {0};
    uint32_t accepted = 0u;

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &allocator_state;
    context_config.allocator.allocate = retry_allocate;
    context_config.allocator.deallocate = retry_deallocate;
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = (4u * COL);
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = (4u * COL);
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == (4u * COL));
    CHECK(apta_session_signal_end_of_input(session, (4u * COL)) == APTA_STATUS_OK);

    /*
     * The portable processor consumes at most 256 frames per step. Process
     * exactly 3072 frames (three complete overview columns) before arming the
     * allocator, leaving exactly one 1024-frame column in the PCM queue.
     */
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 3072u;
    budget.maximum_steps = 12u;
    CHECK(apta_session_process(session, &budget, NULL) == APTA_STATUS_MORE_WORK);
    CHECK(apta_session_get_state(session) == APTA_SESSION_DRAINING);

    allocator_state.armed = 1u;
    allocator_state.armed_allocation_count = 0u;

    /* Four 256-frame steps consume and complete the final overview column. */
    budget.maximum_input_frames = COL;
    budget.maximum_steps = 4u;
    CHECK(apta_session_process(session, &budget, NULL) == APTA_ERROR_OUT_OF_MEMORY);
    CHECK(allocator_state.failed_once == 1u);
    CHECK(allocator_state.armed_allocation_count == 2u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_DRAINING);

    /* No PCM remains. The retry must reconstruct completion state and publish. */
    CHECK(apta_session_process(session, &budget, NULL) == APTA_STATUS_END_OF_INPUT);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.state == APTA_FEATURE_FINAL);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].column_count == 4u);
    apta_result_release(result);
    result = NULL;

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(allocator_state.outstanding == 0u);
    return 0;
}
