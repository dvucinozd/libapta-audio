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

#define TOTAL_FRAMES (4u * 64u * 256u)
#define BLOCK_FRAMES 4096u

typedef struct {
    uint32_t allocate_calls;
    uint32_t outstanding;
} allocator_state_t;

typedef union {
    max_align_t alignment;
    uint8_t bytes[131072];
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

static void fill_ascii(char *buffer, size_t size, char value)
{
    memset(buffer, (unsigned char)value, size);
}

static int verify_metadata(
    const apta_result_t *result,
    const char *producer,
    const char *producer_version,
    const char *backend,
    const char *backend_version,
    const uint8_t *source_id,
    const char *comments)
{
    apta_metadata_view_t view;

    apta_metadata_view_init(&view);
    return apta_result_get_metadata(result, &view) == APTA_STATUS_OK &&
           view.producer_name.size == APTA_METADATA_MAX_PRODUCER_NAME_BYTES &&
           memcmp(view.producer_name.data, producer,
                  APTA_METADATA_MAX_PRODUCER_NAME_BYTES) == 0 &&
           view.producer_version_string.size ==
               APTA_METADATA_MAX_VERSION_STRING_BYTES &&
           memcmp(view.producer_version_string.data, producer_version,
                  APTA_METADATA_MAX_VERSION_STRING_BYTES) == 0 &&
           view.backend_name.size == APTA_METADATA_MAX_BACKEND_NAME_BYTES &&
           memcmp(view.backend_name.data, backend,
                  APTA_METADATA_MAX_BACKEND_NAME_BYTES) == 0 &&
           view.backend_version.size ==
               APTA_METADATA_MAX_VERSION_STRING_BYTES &&
           memcmp(view.backend_version.data, backend_version,
                  APTA_METADATA_MAX_VERSION_STRING_BYTES) == 0 &&
           view.application_source_id_kind == APTA_METADATA_SOURCE_ID_BYTES &&
           view.application_source_id.size == APTA_METADATA_MAX_SOURCE_ID_BYTES &&
           memcmp(view.application_source_id.data, source_id,
                  APTA_METADATA_MAX_SOURCE_ID_BYTES) == 0 &&
           view.comments.size == APTA_METADATA_MAX_COMMENTS_BYTES &&
           memcmp(view.comments.data, comments,
                  APTA_METADATA_MAX_COMMENTS_BYTES) == 0 &&
           view.creation_unix_time == UINT64_C(1800000000);
}

static int verify_detail_capacity(const apta_result_t *result)
{
    uint32_t tile_index;

    for (tile_index = 0u; tile_index < 4u; ++tile_index) {
        apta_waveform_tile_view_t tile;
        uint32_t column;

        apta_waveform_tile_view_init(&tile);
        if (apta_result_get_waveform_tile(
                result,
                1u,
                tile_index,
                &tile) != APTA_STATUS_OK ||
            tile.tile_index != tile_index ||
            tile.first_column_index != tile_index * 64u ||
            tile.column_count != 64u ||
            tile.state != APTA_FEATURE_FINAL ||
            tile.source_range.first_frame !=
                (apta_source_frame_t)tile_index * 64u * 256u ||
            tile.source_range.end_frame !=
                (apta_source_frame_t)(tile_index + 1u) * 64u * 256u ||
            tile.columns == NULL) {
            return 0;
        }

        for (column = 0u; column < tile.column_count; ++column) {
            if (tile.columns[column].minimum != 0 ||
                tile.columns[column].maximum != 0 ||
                tile.columns[column].rms != 0u ||
                tile.columns[column].flags != APTA_WAVEFORM_COLUMN_VALID) {
                return 0;
            }
        }
    }

    return 1;
}

int main(void)
{
    allocator_state_t allocator_state = {0u, 0u};
    aligned_workspace_t workspace;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_metadata_t metadata;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    int16_t pcm[BLOCK_FRAMES] = {0};
    char producer[APTA_METADATA_MAX_PRODUCER_NAME_BYTES];
    char producer_version[APTA_METADATA_MAX_VERSION_STRING_BYTES];
    char backend[APTA_METADATA_MAX_BACKEND_NAME_BYTES];
    char backend_version[APTA_METADATA_MAX_VERSION_STRING_BYTES];
    uint8_t source_id[APTA_METADATA_MAX_SOURCE_ID_BYTES];
    char comments[APTA_METADATA_MAX_COMMENTS_BYTES];
    uint32_t block_index;
    uint32_t accepted;
    apta_status_t status = APTA_STATUS_OK;

    memset(&workspace, 0, sizeof(workspace));
    fill_ascii(producer, sizeof(producer), 'P');
    fill_ascii(producer_version, sizeof(producer_version), 'V');
    fill_ascii(backend, sizeof(backend), 'B');
    fill_ascii(backend_version, sizeof(backend_version), 'R');
    memset(source_id, 0xA5, sizeof(source_id));
    fill_ascii(comments, sizeof(comments), 'C');

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    session_config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(
              &session_config,
              &requirements) == APTA_STATUS_OK);

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &allocator_state;
    context_config.allocator.allocate = test_allocate;
    context_config.allocator.deallocate = test_deallocate;
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    context_config.memory_limit_bytes = requirements.minimum_bytes;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(allocator_state.allocate_calls == 2u);

    apta_metadata_init(&metadata);
    metadata.producer_name.data = producer;
    metadata.producer_name.size = sizeof(producer);
    metadata.producer_version_string.data = producer_version;
    metadata.producer_version_string.size = sizeof(producer_version);
    metadata.backend_name.data = backend;
    metadata.backend_name.size = sizeof(backend);
    metadata.backend_version.data = backend_version;
    metadata.backend_version.size = sizeof(backend_version);
    metadata.creation_unix_time = UINT64_C(1800000000);
    metadata.application_source_id.data = source_id;
    metadata.application_source_id.size = sizeof(source_id);
    metadata.application_source_id_kind = APTA_METADATA_SOURCE_ID_BYTES;
    metadata.comments.data = comments;
    metadata.comments.size = sizeof(comments);
    metadata.flags =
        APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
        APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT |
        APTA_METADATA_FLAG_BACKEND_NAME_PRESENT |
        APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT |
        APTA_METADATA_FLAG_CREATION_TIME_PRESENT |
        APTA_METADATA_FLAG_COMMENTS_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);
    CHECK(allocator_state.allocate_calls == 2u);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 16u;

    for (block_index = 0u;
         block_index < TOTAL_FRAMES / BLOCK_FRAMES;
         ++block_index) {
        apta_pcm_block_init(&block);
        block.data = pcm;
        block.first_frame =
            (apta_source_frame_t)block_index * BLOCK_FRAMES;
        block.frame_count = BLOCK_FRAMES;
        accepted = 0u;
        CHECK(apta_session_push_pcm(session, &block, &accepted) ==
              APTA_STATUS_OK);
        CHECK(accepted == BLOCK_FRAMES);
        status = apta_session_process(session, &budget, NULL);
        CHECK(status >= 0);
        CHECK(allocator_state.allocate_calls == 2u);
    }

    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    for (block_index = 0u; block_index < 8u; ++block_index) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        CHECK(status >= 0);
    }
    CHECK(status == APTA_STATUS_END_OF_INPUT);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);
    CHECK(allocator_state.allocate_calls == 2u);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(verify_metadata(
        result,
        producer,
        producer_version,
        backend,
        backend_version,
        source_id,
        comments));
    CHECK(verify_detail_capacity(result));

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    memset(workspace.bytes, 0xA5, sizeof(workspace.bytes));
    CHECK(verify_metadata(
        result,
        producer,
        producer_version,
        backend,
        backend_version,
        source_id,
        comments));
    CHECK(verify_detail_capacity(result));
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_result_release(result);
    result = NULL;
    CHECK(allocator_state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(allocator_state.outstanding == 0u);
    return 0;
}
