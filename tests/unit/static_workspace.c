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
} counting_allocator_state_t;

typedef union {
    max_align_t alignment;
    uint8_t bytes[65536];
} aligned_workspace_t;

static void *APTA_CALL counting_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    counting_allocator_state_t *state =
        (counting_allocator_state_t *)user_data;
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

static void APTA_CALL counting_deallocate(
    void *user_data,
    void *memory)
{
    counting_allocator_state_t *state =
        (counting_allocator_state_t *)user_data;

    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

static void configure_context(
    apta_context_config_t *config,
    counting_allocator_state_t *state)
{
    apta_context_config_init(config);
    config->allocator.user_data = state;
    config->allocator.allocate = counting_allocate;
    config->allocator.deallocate = counting_deallocate;
    config->requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
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
}

int main(void)
{
    counting_allocator_state_t state = {0u, 0u};
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_result_info_t info;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    aligned_workspace_t workspace;
    aligned_workspace_t tiny_workspace;
    int16_t pcm[128];
    uint32_t cycle;

    memset(&workspace, 0, sizeof(workspace));
    memset(&tiny_workspace, 0, sizeof(tiny_workspace));
    memset(pcm, 0, sizeof(pcm));
    configure_context(&context_config, &state);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(state.allocate_calls == 1u);
    CHECK(state.outstanding == 1u);

    configure_session(&session_config);
    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(session == (apta_session_t *)(void *)workspace.bytes);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 2u);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 128u;
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 128u;
    budget.maximum_steps = 1u;

    for (cycle = 0u; cycle < 7u; ++cycle) {
        uint32_t accepted = 0u;
        apta_status_t status;

        block.first_frame = (apta_source_frame_t)cycle * 128u;
        status = apta_session_push_pcm(session, &block, &accepted);
        CHECK(status == APTA_STATUS_OK);
        CHECK(accepted == 128u);
        CHECK(state.allocate_calls == 3u);

        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        CHECK(state.allocate_calls == 3u);
    }

    CHECK(apta_session_get_state(session) == APTA_SESSION_ACTIVE);
    CHECK(state.outstanding == 2u);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.session_state == APTA_SESSION_ACTIVE);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(state.outstanding == 2u);

    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.session_state == APTA_SESSION_ACTIVE);
    apta_result_release(result);
    result = NULL;
    CHECK(state.outstanding == 1u);

    configure_session(&session_config);
    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = 0u;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    configure_session(&session_config);
    session_config.static_workspace = NULL;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    configure_session(&session_config);
    session_config.static_workspace = workspace.bytes + 1u;
    session_config.static_workspace_size = sizeof(workspace.bytes) - 1u;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    configure_session(&session_config);
    session_config.static_workspace = tiny_workspace.bytes;
    session_config.static_workspace_size = 64u;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);

    configure_session(&session_config);
    session_config.requested_features = APTA_FEATURE_WAVEFORM_DETAIL;
    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    CHECK(state.allocate_calls == 3u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(state.outstanding == 0u);
    return 0;
}
