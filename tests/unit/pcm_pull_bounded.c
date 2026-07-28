// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",               \
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
    uint8_t bytes[65536];
} aligned_workspace_t;

typedef struct {
    const int16_t *samples;
    uint32_t total_frames;
    uint32_t read_calls;
    uint32_t release_calls;
} source_state_t;

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

static uint64_t APTA_CALL source_get_total(void *user_data)
{
    source_state_t *state = (source_state_t *)user_data;
    return state->total_frames;
}

static apta_status_t APTA_CALL source_read(
    void *user_data,
    apta_source_frame_t first_frame,
    uint32_t requested_frames,
    apta_pcm_block_t *block_out)
{
    source_state_t *state = (source_state_t *)user_data;
    uint32_t remaining;
    uint32_t count;

    state->read_calls += 1u;
    if (first_frame >= state->total_frames) {
        return APTA_STATUS_END_OF_INPUT;
    }

    remaining = state->total_frames - (uint32_t)first_frame;
    count = requested_frames < remaining ? requested_frames : remaining;
    if (count > 256u) {
        count = 256u;
    }

    block_out->data = &state->samples[first_frame];
    block_out->first_frame = first_frame;
    block_out->frame_count = count;
    return APTA_STATUS_OK;
}

static void APTA_CALL source_release(
    void *user_data,
    apta_pcm_block_t *block)
{
    source_state_t *state = (source_state_t *)user_data;
    state->release_calls += 1u;
    block->data = NULL;
}

int main(void)
{
    enum { FRAME_COUNT = 2048 };
    int16_t samples[FRAME_COUNT];
    aligned_workspace_t workspace;
    allocator_state_t allocator_state = {0u, 0u};
    source_state_t source_state;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_pcm_source_t source;
    apta_work_budget_t budget;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_waveform_overview_view_t overview;
    uint32_t index;
    uint32_t calls;

    memset(&workspace, 0, sizeof(workspace));
    memset(&source_state, 0, sizeof(source_state));
    source_state.samples = samples;
    source_state.total_frames = FRAME_COUNT;
    for (index = 0u; index < FRAME_COUNT; ++index) {
        samples[index] = (index & 1u) != 0u ? INT16_MAX : INT16_MIN;
    }

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = FRAME_COUNT;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    session_config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(&session_config, &requirements) ==
          APTA_STATUS_OK);
    CHECK(requirements.flags ==
          APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL);

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &allocator_state;
    context_config.allocator.allocate = test_allocate;
    context_config.allocator.deallocate = test_deallocate;
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    context_config.memory_limit_bytes = requirements.minimum_bytes;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(session == (apta_session_t *)(void *)workspace.bytes);
    CHECK(allocator_state.allocate_calls == 2u);

    apta_pcm_source_init(&source);
    source.user_data = &source_state;
    source.read_frames = source_read;
    source.release_frames = source_release;
    source.get_total_frames = source_get_total;
    CHECK(apta_session_set_source(session, &source) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 256u;
    budget.maximum_steps = 2u;
    for (calls = 0u; calls < 64u; ++calls) {
        apta_status_t status = apta_session_process(session, &budget, NULL);
        CHECK(status >= 0);
        CHECK(allocator_state.allocate_calls == 2u);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
    }
    CHECK(calls < 64u);
    CHECK(source_state.read_calls == source_state.release_calls);
    CHECK(source_state.read_calls == FRAME_COUNT / 256u);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.state == APTA_FEATURE_FINAL);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].column_count == 2u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    memset(workspace.bytes, 0xA5, sizeof(workspace.bytes));
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.state == APTA_FEATURE_FINAL);
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_result_release(result);
    CHECK(allocator_state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(allocator_state.outstanding == 0u);
    return 0;
}
