// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "test_alignment.h"

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
    apta_test_max_align_t alignment;
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
    config->requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
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
    apta_metadata_t metadata;
    apta_metadata_view_t view;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    aligned_workspace_t workspace;
    char producer[] = "workspace";
    char comments[] = "owned";
    const char invalid_utf8[] = {(char)0xC0, (char)0x80};

    memset(&workspace, 0, sizeof(workspace));
    configure_context(&context_config, &state);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(state.allocate_calls == 1u);
    CHECK(state.outstanding == 1u);

    configure_session(&session_config);
    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 2u);

    apta_metadata_init(&metadata);
    metadata.producer_name.data = invalid_utf8;
    metadata.producer_name.size = sizeof(invalid_utf8);
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 2u);

    apta_metadata_init(&metadata);
    metadata.producer_name.data = producer;
    metadata.producer_name.size = (uint32_t)strlen(producer);
    metadata.comments.data = comments;
    metadata.comments.size = (uint32_t)strlen(comments);
    metadata.flags =
        APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
        APTA_METADATA_FLAG_COMMENTS_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);
    CHECK(state.allocate_calls == 4u);
    CHECK(state.outstanding == 3u);

    producer[0] = 'X';
    comments[0] = 'Y';

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(result, &view) == APTA_STATUS_OK);
    CHECK(view.producer_name.size == 9u);
    CHECK(memcmp(view.producer_name.data, "workspace", 9u) == 0);
    CHECK(view.comments.size == 5u);
    CHECK(memcmp(view.comments.data, "owned", 5u) == 0);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(state.outstanding == 3u);

    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(result, &view) == APTA_STATUS_OK);
    CHECK(memcmp(view.producer_name.data, "workspace", 9u) == 0);
    CHECK(memcmp(view.comments.data, "owned", 5u) == 0);

    apta_result_release(result);
    result = NULL;
    CHECK(state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(state.outstanding == 0u);
    return 0;
}
