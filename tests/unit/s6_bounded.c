// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define SAMPLE_RATE 48000u
#define TEMPO_MILLIBPM 117188u
#define BEAT_FRAMES 24576u
#define TOTAL_FRAMES 524288u
#define BLOCK_FRAMES 4096u
#define WORKSPACE_BYTES 1572864u

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
} allocator_state_t;

typedef union {
    max_align_t alignment;
    uint8_t bytes[WORKSPACE_BYTES];
} aligned_workspace_t;

static aligned_workspace_t workspace;

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
    memory = malloc(size);
    if (memory != NULL) {
        state->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL test_deallocate(void *user_data, void *memory)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

int main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE;
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_grid_view_t grid;
    apta_grid_revision_view_t revision;
    apta_work_budget_t budget;
    allocator_state_t allocator_state;
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    uint32_t allocator_calls_after_create;
    apta_status_t status;

    memset(&workspace, 0, sizeof(workspace));
    memset(&allocator_state, 0, sizeof(allocator_state));

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = features;
    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    session_config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(&session_config, &requirements) ==
          APTA_STATUS_OK);
    CHECK((requirements.flags &
           APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL) != 0u);

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &allocator_state;
    context_config.allocator.allocate = test_allocate;
    context_config.allocator.deallocate = test_deallocate;
    context_config.requested_capabilities = features;
    context_config.memory_limit_bytes = requirements.minimum_bytes;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(session == (apta_session_t *)(void *)workspace.bytes);
    allocator_calls_after_create = allocator_state.allocate_calls;
    CHECK(allocator_calls_after_create == 2u);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t accepted = 0u;
        uint32_t index;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            samples[index] = ((first + index) % BEAT_FRAMES) < 192u
                                 ? (int16_t)30000
                                 : 0;
        }
        apta_pcm_block_init(&block);
        block.data = samples;
        block.first_frame = first;
        block.frame_count = count;
        CHECK(apta_session_push_pcm(session, &block, &accepted) ==
              APTA_STATUS_OK);
        CHECK(accepted == count);
        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        CHECK(allocator_state.allocate_calls == allocator_calls_after_create);
        first += count;
    }

    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);
    CHECK(allocator_state.allocate_calls == allocator_calls_after_create);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_GLOBAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.state == APTA_FEATURE_FINAL);
    CHECK(grid.segment_count == 1u);
    CHECK(grid.segments[0].nominal_tempo_millibpm >=
          TEMPO_MILLIBPM - 2u);
    CHECK(grid.segments[0].nominal_tempo_millibpm <=
          TEMPO_MILLIBPM + 2u);
    apta_grid_revision_view_init(&revision);
    CHECK(apta_result_get_grid_revision(result, &revision) == APTA_STATUS_OK);
    CHECK(revision.state == APTA_GRID_REVISION_APPLIED);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    memset(workspace.bytes, 0xa5, sizeof(workspace.bytes));
    CHECK(allocator_state.allocate_calls == allocator_calls_after_create);

    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_GLOBAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.state == APTA_FEATURE_FINAL);
    CHECK(grid.segment_count == 1u);

    apta_result_release(result);
    result = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(allocator_state.outstanding == 0u);
    return 0;
}
