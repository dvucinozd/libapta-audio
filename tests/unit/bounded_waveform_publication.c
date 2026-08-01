// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "test_alignment.h"

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
    uint32_t allocate_calls;
    uint32_t outstanding;
} allocator_state_t;

typedef union {
    apta_test_max_align_t alignment;
    uint8_t bytes[65536 * APTA_TEST_WORKSPACE_SCALE];
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

static int result_has_metadata(
    const apta_result_t *result,
    const char *expected)
{
    apta_metadata_view_t metadata;
    size_t size = strlen(expected);

    apta_metadata_view_init(&metadata);
    return apta_result_get_metadata(result, &metadata) == APTA_STATUS_OK &&
           metadata.producer_name.size == size &&
           memcmp(metadata.producer_name.data, expected, size) == 0;
}

static int result_has_overview(
    const apta_result_t *result,
    apta_feature_state_t expected_state)
{
    apta_waveform_overview_view_t overview;
    const apta_waveform_column_t *column;

    apta_waveform_overview_view_init(&overview);
    if (apta_result_get_waveform_overview(result, 0u, &overview) !=
            APTA_STATUS_OK ||
        overview.state != expected_state ||
        overview.span_count != 1u ||
        overview.spans == NULL ||
        overview.spans[0].first_column_index != 0u ||
        overview.spans[0].column_count != 1u ||
        overview.spans[0].columns == NULL) {
        return 0;
    }

    column = &overview.spans[0].columns[0];
    return column->minimum == INT16_MIN &&
           column->maximum == INT16_MAX &&
           (column->flags & APTA_WAVEFORM_COLUMN_VALID) != 0u &&
           (column->flags & APTA_WAVEFORM_COLUMN_CLIPPED) != 0u;
}

static int result_has_detail(
    const apta_result_t *result,
    apta_feature_state_t expected_state)
{
    apta_waveform_tile_view_t tile;
    apta_frame_range_t range;
    apta_feature_state_t state = APTA_FEATURE_ABSENT;
    apta_confidence_value_t confidence = 0u;
    uint32_t column;

    apta_waveform_tile_view_init(&tile);
    if (apta_result_get_waveform_tile(result, 1u, 0u, &tile) !=
            APTA_STATUS_OK ||
        tile.level_id != 1u ||
        tile.tile_index != 0u ||
        tile.source_range.first_frame != 0u ||
        tile.source_range.end_frame != COL ||
        tile.first_column_index != 0u ||
        tile.column_count != 4u ||
        tile.state != expected_state ||
        tile.columns == NULL) {
        return 0;
    }

    for (column = 0u; column < tile.column_count; ++column) {
        if (tile.columns[column].minimum != INT16_MIN ||
            tile.columns[column].maximum != INT16_MAX ||
            (tile.columns[column].flags & APTA_WAVEFORM_COLUMN_VALID) == 0u ||
            (tile.columns[column].flags & APTA_WAVEFORM_COLUMN_CLIPPED) == 0u) {
            return 0;
        }
    }

    apta_frame_range_init(&range);
    range.first_frame = 0u;
    range.end_frame = COL;
    return apta_result_get_feature_state(
               result,
               APTA_FEATURE_WAVEFORM_DETAIL,
               &range,
               &state,
               &confidence) == APTA_STATUS_OK &&
           state == expected_state &&
           confidence == APTA_CONFIDENCE_UNKNOWN;
}

int main(void)
{
    allocator_state_t allocator_state = {0u, 0u};
    aligned_workspace_t workspace;
    apta_context_config_t context_config;
    apta_context_config_t parse_context_config;
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_context_t *context = NULL;
    apta_context_t *parse_context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *initial = NULL;
    const apta_result_t *metadata_result = NULL;
    const apta_result_t *partial_result = NULL;
    const apta_result_t *final_result = NULL;
    const apta_result_t *parsed_result = NULL;
    apta_metadata_t metadata;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_progress_t progress;
    apta_result_info_t info;
    int16_t pcm[COL];
    uint8_t *serialized = NULL;
    uint64_t serialized_size = 0u;
    size_t written = 0u;
    uint32_t accepted = 0u;
    uint32_t index;
    apta_status_t status;

    memset(&workspace, 0, sizeof(workspace));
    for (index = 0u; index < COL; ++index) {
        pcm[index] = (index & 1u) != 0u ? INT16_MAX : INT16_MIN;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = COL;
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

    initial = apta_session_acquire_result(session);
    CHECK(initial != NULL);
    CHECK(apta_result_get_generation(initial) == 1u);

    apta_metadata_init(&metadata);
    metadata.producer_name.data = "bounded-wovr";
    metadata.producer_name.size = 12u;
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);
    CHECK(allocator_state.allocate_calls == 2u);

    metadata_result = apta_session_acquire_result(session);
    CHECK(metadata_result != NULL && metadata_result != initial);
    CHECK(apta_result_get_generation(metadata_result) == 2u);
    CHECK(result_has_metadata(metadata_result, "bounded-wovr"));

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = COL;
    accepted = UINT32_MAX;
    CHECK(apta_session_push_pcm(session, &block, &accepted) ==
          APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
    CHECK(accepted == 0u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);

    apta_result_release(initial);
    initial = NULL;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == COL);
    CHECK(apta_session_get_state(session) == APTA_SESSION_ACTIVE);
    CHECK(allocator_state.allocate_calls == 2u);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = COL;
    budget.maximum_steps = 4u;
    apta_progress_init(&progress);
    CHECK(apta_session_process(session, &budget, &progress) ==
          APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
    CHECK(apta_session_get_state(session) == APTA_SESSION_ACTIVE);
    CHECK(allocator_state.allocate_calls == 2u);

    apta_result_release(metadata_result);
    metadata_result = NULL;
    apta_progress_init(&progress);
    status = apta_session_process(session, &budget, &progress);
    CHECK(status >= 0);
    CHECK(progress.published_generation == 4u);
    CHECK(allocator_state.allocate_calls == 2u);

    partial_result = apta_session_acquire_result(session);
    CHECK(partial_result != NULL);
    CHECK(apta_result_get_generation(partial_result) == 4u);
    CHECK(result_has_metadata(partial_result, "bounded-wovr"));
    CHECK(result_has_overview(partial_result, APTA_FEATURE_PARTIAL));
    CHECK(result_has_detail(partial_result, APTA_FEATURE_PARTIAL));

    CHECK(apta_session_signal_end_of_input(session, COL) == APTA_STATUS_OK);
    CHECK(apta_session_get_state(session) == APTA_SESSION_DRAINING);
    CHECK(allocator_state.allocate_calls == 2u);

    apta_progress_init(&progress);
    CHECK(apta_session_process(session, &budget, &progress) ==
          APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
    CHECK(apta_session_get_state(session) == APTA_SESSION_DRAINING);

    apta_result_release(partial_result);
    partial_result = NULL;
    apta_progress_init(&progress);
    CHECK(apta_session_process(session, &budget, &progress) ==
          APTA_STATUS_END_OF_INPUT);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);
    CHECK(allocator_state.allocate_calls == 2u);

    final_result = apta_session_acquire_result(session);
    CHECK(final_result != NULL);
    CHECK(apta_result_get_generation(final_result) == 6u);
    CHECK(result_has_metadata(final_result, "bounded-wovr"));
    CHECK(result_has_overview(final_result, APTA_FEATURE_FINAL));
    CHECK(result_has_detail(final_result, APTA_FEATURE_FINAL));
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(final_result, &info) == APTA_STATUS_OK);
    CHECK(info.session_state == APTA_SESSION_COMPLETED);
    CHECK((info.available_features & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u);
    CHECK((info.available_features & APTA_FEATURE_WAVEFORM_DETAIL) != 0u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    memset(workspace.bytes, 0xA5, sizeof(workspace.bytes));
    CHECK(result_has_metadata(final_result, "bounded-wovr"));
    CHECK(result_has_overview(final_result, APTA_FEATURE_FINAL));
    CHECK(result_has_detail(final_result, APTA_FEATURE_FINAL));

    CHECK(apta_result_query_serialized_size(
              final_result,
              NULL,
              &serialized_size) == APTA_STATUS_OK);
    CHECK(serialized_size > 0u && serialized_size <= SIZE_MAX);
    serialized = (uint8_t *)malloc((size_t)serialized_size);
    CHECK(serialized != NULL);
    CHECK(apta_result_serialize(
              final_result,
              NULL,
              serialized,
              (size_t)serialized_size,
              &written) == APTA_STATUS_OK);
    CHECK(written == (size_t)serialized_size);

    apta_context_config_init(&parse_context_config);
    parse_context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_context_create(&parse_context_config, &parse_context) ==
          APTA_STATUS_OK);
    CHECK(apta_result_parse(
              parse_context,
              NULL,
              serialized,
              written,
              &parsed_result) == APTA_STATUS_OK);
    CHECK(result_has_metadata(parsed_result, "bounded-wovr"));
    CHECK(result_has_overview(parsed_result, APTA_FEATURE_FINAL));
    CHECK(result_has_detail(parsed_result, APTA_FEATURE_FINAL));
    apta_result_release(parsed_result);
    parsed_result = NULL;
    CHECK(apta_context_destroy(parse_context) == APTA_STATUS_OK);
    parse_context = NULL;
    free(serialized);
    serialized = NULL;

    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);
    apta_result_release(final_result);
    final_result = NULL;
    CHECK(allocator_state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(allocator_state.outstanding == 0u);
    return 0;
}
