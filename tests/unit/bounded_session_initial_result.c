// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

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

typedef union {
    max_align_t alignment;
    uint8_t bytes[65536];
} aligned_workspace_t;

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
    config->requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
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
    config->requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    config->memory_limit_bytes = memory_limit;
}

static int verify_initial_result(const apta_result_t *result)
{
    apta_result_info_t info;

    apta_result_info_init(&info);
    return result != NULL &&
           apta_result_get_info(result, &info) == APTA_STATUS_OK &&
           info.generation == 1u &&
           info.session_state == APTA_SESSION_CREATED &&
           info.available_features == 0u &&
           info.changed_features == 0u &&
           apta_result_get_generation(result) == 1u &&
           apta_result_get_available_features(result) == 0u;
}

int main(void)
{
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_metadata_t metadata;
    apta_pcm_source_t source;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_progress_t progress;
    aligned_workspace_t workspace;
    allocator_state_t state;
    int16_t pcm[16] = {0};
    uint32_t accepted;

    memset(&workspace, 0, sizeof(workspace));
    configure_session(&session_config);
    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(
              &session_config,
              &requirements) == APTA_STATUS_OK);
    CHECK(requirements.minimum_bytes == requirements.recommended_bytes);
    CHECK(requirements.flags ==
          APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL);

    memset(&state, 0, sizeof(state));
    configure_context(
        &context_config,
        &state,
        (uint64_t)requirements.minimum_bytes);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(state.allocate_calls == 1u);
    CHECK(state.outstanding == 1u);

    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(session == (apta_session_t *)(void *)workspace.bytes);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 2u);

    result = apta_session_acquire_result(session);
    CHECK(verify_initial_result(result));

    apta_metadata_init(&metadata);
    metadata.producer_name.data = "bounded";
    metadata.producer_name.size = 7u;
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);

    apta_pcm_source_init(&source);
    CHECK(apta_session_set_source(session, &source) ==
          APTA_ERROR_INVALID_STATE);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 16u;
    accepted = UINT32_MAX;
    CHECK(apta_session_push_pcm(session, &block, &accepted) ==
          APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
    CHECK(accepted == 0u);

    CHECK(apta_session_signal_end_of_input(session, 4096u) ==
          APTA_ERROR_RESULT_SLOTS_EXHAUSTED);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 16u;
    budget.maximum_steps = 1u;
    apta_progress_init(&progress);
    CHECK(apta_session_process(session, &budget, &progress) ==
          APTA_STATUS_WOULD_BLOCK);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 2u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    memset(workspace.bytes, 0xA5, sizeof(workspace.bytes));
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 2u);
    CHECK(verify_initial_result(result));
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_result_release(result);
    result = NULL;
    CHECK(state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(state.outstanding == 0u);

    memset(&workspace, 0, sizeof(workspace));
    memset(&state, 0, sizeof(state));
    state.fail_at = 2u;
    configure_context(&context_config, &state, 0u);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    configure_session(&session_config);
    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(state.outstanding == 0u);

    memset(&workspace, 0, sizeof(workspace));
    memset(&state, 0, sizeof(state));
    configure_context(
        &context_config,
        &state,
        (uint64_t)requirements.minimum_bytes - 1u);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    configure_session(&session_config);
    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);
    CHECK(state.allocate_calls == 1u);
    CHECK(state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(state.outstanding == 0u);

    configure_session(&session_config);
    session_config.static_workspace = NULL;
    session_config.static_workspace_size = 0u;
    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    session_config.total_frames = APTA_TOTAL_FRAMES_UNKNOWN;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    session_config.total_frames = 4096u;
    session_config.requested_features = 0u;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
