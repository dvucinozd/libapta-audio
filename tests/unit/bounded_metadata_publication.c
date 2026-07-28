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

static int metadata_equals_a(const apta_result_t *result)
{
    apta_metadata_view_t view;

    apta_metadata_view_init(&view);
    return apta_result_get_metadata(result, &view) == APTA_STATUS_OK &&
           view.producer_name.size == 5u &&
           memcmp(view.producer_name.data, "alpha", 5u) == 0 &&
           view.backend_version.size == 0u &&
           (view.flags & APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT) != 0u &&
           view.comments.size == 3u &&
           memcmp(view.comments.data, "one", 3u) == 0;
}

static int metadata_equals_b(const apta_result_t *result)
{
    static const uint8_t expected_source[] = {1u, 2u, 3u, 4u};
    apta_metadata_view_t view;

    apta_metadata_view_init(&view);
    return apta_result_get_metadata(result, &view) == APTA_STATUS_OK &&
           view.producer_name.size == 4u &&
           memcmp(view.producer_name.data, "beta", 4u) == 0 &&
           view.application_source_id_kind == APTA_METADATA_SOURCE_ID_BYTES &&
           view.application_source_id.size == sizeof(expected_source) &&
           memcmp(
               view.application_source_id.data,
               expected_source,
               sizeof(expected_source)) == 0 &&
           view.comments.size == 3u &&
           memcmp(view.comments.data, "two", 3u) == 0;
}

int main(void)
{
    allocator_state_t state = {0u, 0u};
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *initial = NULL;
    const apta_result_t *second = NULL;
    const apta_result_t *third = NULL;
    const apta_result_t *fourth = NULL;
    const apta_result_t *probe = NULL;
    apta_metadata_t metadata;
    apta_result_info_t info;
    aligned_workspace_t workspace;
    char producer_a[] = "alpha";
    char comments_a[] = "one";
    char producer_b[] = "beta";
    char comments_b[] = "two";
    uint8_t source_b[] = {1u, 2u, 3u, 4u};
    const char invalid_utf8[] = {(char)0xC0, (char)0x80};

    memset(&workspace, 0, sizeof(workspace));
    configure_session(&session_config);
    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(
              &session_config,
              &requirements) == APTA_STATUS_OK);

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &state;
    context_config.allocator.allocate = test_allocate;
    context_config.allocator.deallocate = test_deallocate;
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW;
    context_config.memory_limit_bytes = requirements.minimum_bytes;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(state.allocate_calls == 2u);
    CHECK(state.outstanding == 2u);

    initial = apta_session_acquire_result(session);
    CHECK(initial != NULL);
    CHECK(apta_result_get_generation(initial) == 1u);

    apta_metadata_init(&metadata);
    metadata.producer_name.data = invalid_utf8;
    metadata.producer_name.size = sizeof(invalid_utf8);
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_get_generation(
              apta_session_acquire_result(session)) == 1u);
    probe = apta_session_acquire_result(session);
    CHECK(probe == initial);
    apta_result_release(probe);
    probe = NULL;
    CHECK(state.allocate_calls == 2u);

    apta_metadata_init(&metadata);
    metadata.producer_name.data = producer_a;
    metadata.producer_name.size = 5u;
    metadata.backend_version.data = NULL;
    metadata.backend_version.size = 0u;
    metadata.comments.data = comments_a;
    metadata.comments.size = 3u;
    metadata.flags =
        APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
        APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT |
        APTA_METADATA_FLAG_COMMENTS_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);
    producer_a[0] = 'X';
    comments_a[0] = 'Y';
    CHECK(state.allocate_calls == 2u);

    second = apta_session_acquire_result(session);
    CHECK(second != NULL && second != initial);
    CHECK(apta_result_get_generation(second) == 2u);
    CHECK(metadata_equals_a(second));

    apta_metadata_init(&metadata);
    metadata.producer_name.data = producer_b;
    metadata.producer_name.size = 4u;
    metadata.application_source_id.data = source_b;
    metadata.application_source_id.size = sizeof(source_b);
    metadata.application_source_id_kind = APTA_METADATA_SOURCE_ID_BYTES;
    metadata.comments.data = comments_b;
    metadata.comments.size = 3u;
    metadata.flags =
        APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
        APTA_METADATA_FLAG_COMMENTS_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) ==
          APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
    CHECK(state.allocate_calls == 2u);
    probe = apta_session_acquire_result(session);
    CHECK(probe == second);
    CHECK(apta_result_get_generation(probe) == 2u);
    CHECK(metadata_equals_a(probe));
    apta_result_release(probe);
    probe = NULL;

    apta_result_release(initial);
    initial = NULL;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);
    producer_b[0] = 'Z';
    comments_b[0] = 'Q';
    source_b[0] = 9u;
    CHECK(state.allocate_calls == 2u);

    third = apta_session_acquire_result(session);
    CHECK(third != NULL);
    CHECK(apta_result_get_generation(third) == 3u);
    CHECK(metadata_equals_b(third));

    CHECK(apta_session_set_metadata(session, NULL) ==
          APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
    CHECK(apta_result_get_generation(third) == 3u);

    apta_result_release(second);
    second = NULL;
    CHECK(apta_session_set_metadata(session, NULL) == APTA_STATUS_OK);
    CHECK(state.allocate_calls == 2u);

    fourth = apta_session_acquire_result(session);
    CHECK(fourth != NULL && fourth != third);
    CHECK(apta_result_get_generation(fourth) == 4u);
    {
        apta_metadata_view_t view;
        apta_metadata_view_init(&view);
        CHECK(apta_result_get_metadata(fourth, &view) ==
              APTA_STATUS_NOT_AVAILABLE);
    }

    apta_result_info_init(&info);
    CHECK(apta_result_get_info(fourth, &info) == APTA_STATUS_OK);
    CHECK(info.session_state == APTA_SESSION_CREATED);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    memset(workspace.bytes, 0xA5, sizeof(workspace.bytes));
    CHECK(metadata_equals_b(third));
    {
        apta_metadata_view_t view;
        apta_metadata_view_init(&view);
        CHECK(apta_result_get_metadata(fourth, &view) ==
              APTA_STATUS_NOT_AVAILABLE);
    }
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_result_release(third);
    third = NULL;
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);
    apta_result_release(fourth);
    fourth = NULL;
    CHECK(state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(state.outstanding == 0u);
    return 0;
}
