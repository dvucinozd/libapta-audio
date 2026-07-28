// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    uint32_t call_count;
    uint32_t fail_at;
    uint32_t outstanding;
} fail_allocator_state_t;

static void *APTA_CALL fail_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    fail_allocator_state_t *state = (fail_allocator_state_t *)user_data;
    void *memory;

    (void)alignment;
    (void)flags;

    state->call_count += 1u;
    if (state->call_count == state->fail_at) {
        return NULL;
    }

    memory = malloc(size);
    if (memory != NULL) {
        state->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL fail_deallocate(
    void *user_data,
    void *memory)
{
    fail_allocator_state_t *state = (fail_allocator_state_t *)user_data;

    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

static void configure_context(
    apta_context_config_t *config,
    fail_allocator_state_t *state)
{
    apta_context_config_init(config);
    config->allocator.user_data = state;
    config->allocator.allocate = fail_allocate;
    config->allocator.deallocate = fail_deallocate;
}

static void configure_session(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = 48000u;
    config->channel_count = 2u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
}

int main(void)
{
    fail_allocator_state_t state = {0u, 1u, 0u};
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    int16_t pcm[2] = {0, 0};
    uint32_t accepted = 99u;

    configure_context(&context_config, &state);
    CHECK(apta_context_create(&context_config, &context) == APTA_ERROR_OUT_OF_MEMORY);
    CHECK(context == NULL);
    CHECK(state.outstanding == 0u);

    state.call_count = 0u;
    state.fail_at = 2u;
    configure_context(&context_config, &state);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    configure_session(&session_config);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(state.outstanding == 0u);

    state.call_count = 0u;
    state.fail_at = 3u;
    configure_context(&context_config, &state);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    configure_session(&session_config);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(state.outstanding == 0u);

    state.call_count = 0u;
    state.fail_at = 4u;
    configure_context(&context_config, &state);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    configure_session(&session_config);
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 1u;

    CHECK(apta_session_push_pcm(session, &block, &accepted) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(accepted == 0u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(state.outstanding == 0u);

    return 0;
}
