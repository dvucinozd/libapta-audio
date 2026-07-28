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

static int verify_final_detail(const apta_result_t *result)
{
    apta_waveform_tile_view_t tile;
    apta_frame_range_t range;
    apta_feature_state_t state = APTA_FEATURE_ABSENT;
    apta_confidence_value_t confidence = 0u;
    uint32_t column;

    if (result == NULL ||
        (apta_result_get_available_features(result) &
         APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u ||
        (apta_result_get_available_features(result) &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
        return 0;
    }

    apta_waveform_tile_view_init(&tile);
    if (apta_result_get_waveform_tile(result, 1u, 0u, &tile) !=
            APTA_STATUS_OK ||
        tile.level_id != 1u ||
        tile.tile_index != 0u ||
        tile.source_range.first_frame != 0u ||
        tile.source_range.end_frame != 1024u ||
        tile.first_column_index != 0u ||
        tile.column_count != 4u ||
        tile.state != APTA_FEATURE_FINAL ||
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

    apta_frame_range_init(&range);
    range.first_frame = 0u;
    range.end_frame = 1024u;
    if (apta_result_get_feature_state(
            result,
            APTA_FEATURE_WAVEFORM_DETAIL,
            &range,
            &state,
            &confidence) != APTA_STATUS_OK ||
        state != APTA_FEATURE_FINAL ||
        confidence != APTA_CONFIDENCE_UNKNOWN) {
        return 0;
    }

    apta_waveform_tile_view_init(&tile);
    return apta_result_get_waveform_tile(result, 1u, 1u, &tile) ==
               APTA_STATUS_NOT_AVAILABLE &&
           tile.confidence == APTA_CONFIDENCE_UNKNOWN;
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
    apta_pcm_request_t request;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    int16_t pcm[1024] = {0};
    uint32_t accepted = 0u;

    memset(&workspace, 0, sizeof(workspace));

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
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
    CHECK(allocator_state.outstanding == 2u);

    apta_pcm_request_init(&request);
    CHECK(apta_session_next_pcm_request(session, &request) == APTA_STATUS_OK);
    CHECK(request.range.first_frame == 0u);
    CHECK(request.range.end_frame == 1024u);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 1024u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 1024u);
    CHECK(apta_session_signal_end_of_input(session, 1024u) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    CHECK(apta_session_process(session, &budget, NULL) ==
          APTA_STATUS_END_OF_INPUT);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);
    CHECK(allocator_state.allocate_calls == 2u);

    result = apta_session_acquire_result(session);
    CHECK(verify_final_detail(result));

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    memset(workspace.bytes, 0xA5, sizeof(workspace.bytes));
    CHECK(verify_final_detail(result));
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_result_release(result);
    result = NULL;
    CHECK(allocator_state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(allocator_state.outstanding == 0u);
    return 0;
}
