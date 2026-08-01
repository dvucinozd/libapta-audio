// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define COL APTA_TEST_COLUMN_FRAMES

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",               \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    const int16_t *samples;
    uint64_t total_frames;
    uint32_t maximum_chunk;
    uint32_t known_total;
    uint32_t would_block_once;
    uint32_t did_block;
    uint32_t bad_offset;
    uint32_t fail_read;
    uint32_t read_calls;
    uint32_t successful_reads;
    uint32_t release_calls;
    uint32_t maximum_requested;
} pull_source_state_t;

static uint64_t APTA_CALL source_get_total_frames(void *user_data)
{
    pull_source_state_t *state = (pull_source_state_t *)user_data;
    return state->known_total != 0u
               ? state->total_frames
               : APTA_TOTAL_FRAMES_UNKNOWN;
}

static apta_status_t APTA_CALL source_read_frames(
    void *user_data,
    apta_source_frame_t first_frame,
    uint32_t requested_frames,
    apta_pcm_block_t *block_out)
{
    pull_source_state_t *state = (pull_source_state_t *)user_data;
    uint64_t remaining;
    uint32_t count;

    state->read_calls += 1u;
    if (requested_frames > state->maximum_requested) {
        state->maximum_requested = requested_frames;
    }

    if (state->would_block_once != 0u && state->did_block == 0u) {
        state->did_block = 1u;
        return APTA_STATUS_WOULD_BLOCK;
    }
    if (state->fail_read != 0u) {
        return APTA_ERROR_SOURCE;
    }
    if (first_frame >= state->total_frames) {
        return APTA_STATUS_END_OF_INPUT;
    }

    remaining = state->total_frames - first_frame;
    count = requested_frames;
    if ((uint64_t)count > remaining) {
        count = (uint32_t)remaining;
    }
    if (state->maximum_chunk != 0u && count > state->maximum_chunk) {
        count = state->maximum_chunk;
    }

    block_out->data = &state->samples[first_frame];
    block_out->first_frame = first_frame + (state->bad_offset != 0u ? 1u : 0u);
    block_out->frame_count = count;
    state->successful_reads += 1u;
    return APTA_STATUS_OK;
}

static void APTA_CALL source_release_frames(
    void *user_data,
    apta_pcm_block_t *block)
{
    pull_source_state_t *state = (pull_source_state_t *)user_data;
    state->release_calls += 1u;
    block->data = NULL;
}

static void init_source(
    apta_pcm_source_t *source,
    pull_source_state_t *state)
{
    apta_pcm_source_init(source);
    source->user_data = state;
    source->read_frames = source_read_frames;
    source->release_frames = source_release_frames;
    source->get_total_frames = source_get_total_frames;
}

static int run_to_completion(
    apta_session_t *session,
    const apta_work_budget_t *budget)
{
    uint32_t call;

    for (call = 0u; call < 256u; ++call) {
        apta_status_t status = apta_session_process(session, budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            return 1;
        }
        if (status < 0) {
            return 0;
        }
    }
    return 0;
}

static int verify_final_overview(
    const apta_result_t *result,
    uint64_t total_frames,
    uint32_t expected_columns)
{
    apta_result_info_t info;
    apta_waveform_overview_view_t overview;

    apta_result_info_init(&info);
    if (apta_result_get_info(result, &info) != APTA_STATUS_OK ||
        info.session_state != APTA_SESSION_COMPLETED) {
        return 0;
    }

    apta_waveform_overview_view_init(&overview);
    if (apta_result_get_waveform_overview(result, 0u, &overview) !=
            APTA_STATUS_OK ||
        overview.state != APTA_FEATURE_FINAL ||
        overview.span_count != 1u ||
        overview.spans == NULL ||
        overview.level.frames_per_column == 0u ||
        overview.spans[0].column_count !=
            (uint32_t)((total_frames +
                        overview.level.frames_per_column - 1u) /
                       overview.level.frames_per_column) ||
        overview.spans[0].source_range.first_frame != 0u ||
        overview.spans[0].source_range.end_frame != total_frames) {
        return 0;
    }

    return 1;
}

static int test_known_length_and_would_block(void)
{
    enum { FRAME_COUNT = 2500 };
    int16_t *samples = (int16_t *)malloc(FRAME_COUNT * sizeof(*samples));
    pull_source_state_t source_state;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_pcm_source_t source;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    apta_pcm_block_t push_block;
    uint32_t accepted = UINT32_MAX;
    uint32_t index;

    CHECK(samples != NULL);
    for (index = 0u; index < FRAME_COUNT; ++index) {
        samples[index] = (index & 1u) != 0u ? INT16_MAX : INT16_MIN;
    }
    memset(&source_state, 0, sizeof(source_state));
    source_state.samples = samples;
    source_state.total_frames = FRAME_COUNT;
    source_state.maximum_chunk = 137u;
    source_state.known_total = 1u;
    source_state.would_block_once = 1u;

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;

    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(&session_config, &requirements) ==
          APTA_STATUS_OK);
    CHECK(requirements.minimum_bytes != 0u);

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 300u;
    budget.maximum_steps = 8u;
    CHECK(apta_session_process(session, &budget, NULL) ==
          APTA_ERROR_INVALID_STATE);

    init_source(&source, &source_state);
    CHECK(apta_session_set_source(session, &source) == APTA_STATUS_OK);

    apta_pcm_block_init(&push_block);
    push_block.data = samples;
    push_block.frame_count = 1u;
    CHECK(apta_session_push_pcm(session, &push_block, &accepted) ==
          APTA_ERROR_INVALID_STATE);
    CHECK(accepted == 0u);
    CHECK(apta_session_signal_end_of_input(session, FRAME_COUNT) ==
          APTA_ERROR_INVALID_STATE);

    CHECK(apta_session_process(session, &budget, NULL) ==
          APTA_STATUS_WOULD_BLOCK);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);
    CHECK(source_state.release_calls == 0u);

    CHECK(run_to_completion(session, &budget));
    CHECK(source_state.maximum_requested <= 300u);
    CHECK(source_state.successful_reads == source_state.release_calls);
    CHECK(source_state.successful_reads != 0u);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(verify_final_overview(result, FRAME_COUNT, 3u));
    CHECK(apta_session_set_source(session, &source) == APTA_ERROR_INVALID_STATE);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    free(samples);
    return 0;
}

static int test_unknown_length_end_callback(void)
{
    enum { FRAME_COUNT = 1300 };
    int16_t samples[FRAME_COUNT];
    pull_source_state_t source_state;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_pcm_source_t source;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    uint32_t index;

    for (index = 0u; index < FRAME_COUNT; ++index) {
        samples[index] = (int16_t)(index - 650);
    }
    memset(&source_state, 0, sizeof(source_state));
    source_state.samples = samples;
    source_state.total_frames = FRAME_COUNT;
    source_state.maximum_chunk = 333u;
    source_state.known_total = 0u;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = 44100u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    init_source(&source, &source_state);
    CHECK(apta_session_set_source(session, &source) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 512u;
    budget.maximum_steps = 8u;
    CHECK(run_to_completion(session, &budget));
    CHECK(source_state.read_calls == source_state.successful_reads + 1u);
    CHECK(source_state.release_calls == source_state.successful_reads);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(verify_final_overview(result, FRAME_COUNT, 2u));

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

static int test_source_contract_failures(void)
{
    int16_t samples[COL] = {0};
    pull_source_state_t source_state;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_pcm_source_t source;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = COL;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    memset(&source_state, 0, sizeof(source_state));
    source_state.samples = samples;
    source_state.total_frames = (2u * COL);
    source_state.known_total = 1u;
    init_source(&source, &source_state);
    CHECK(apta_session_set_source(session, &source) == APTA_ERROR_CONFLICT);

    source_state.total_frames = COL;
    source_state.bad_offset = 1u;
    CHECK(apta_session_set_source(session, &source) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 256u;
    budget.maximum_steps = 2u;
    CHECK(apta_session_process(session, &budget, NULL) == APTA_ERROR_SOURCE);
    CHECK(apta_session_get_state(session) == APTA_SESSION_FAILED);
    CHECK(source_state.successful_reads == 1u);
    CHECK(source_state.release_calls == 1u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

static int test_empty_known_source(void)
{
    pull_source_state_t source_state;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_pcm_source_t source;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;

    memset(&source_state, 0, sizeof(source_state));
    source_state.total_frames = 0u;
    source_state.known_total = 1u;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    init_source(&source, &source_state);
    CHECK(apta_session_set_source(session, &source) == APTA_STATUS_OK);
    apta_work_budget_init(&budget);
    CHECK(apta_session_process(session, &budget, NULL) ==
          APTA_STATUS_END_OF_INPUT);
    CHECK(source_state.read_calls == 0u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

int main(void)
{
    CHECK(test_known_length_and_would_block() == 0);
    CHECK(test_unknown_length_end_callback() == 0);
    CHECK(test_source_contract_failures() == 0);
    CHECK(test_empty_known_source() == 0);
    return 0;
}
