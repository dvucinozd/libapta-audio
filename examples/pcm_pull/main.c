// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

typedef struct {
    const int16_t *samples;
    uint64_t total_frames;
    uint32_t read_calls;
} memory_source_t;

static uint64_t APTA_CALL get_total_frames(void *user_data)
{
    const memory_source_t *source = (const memory_source_t *)user_data;
    return source->total_frames;
}

static apta_status_t APTA_CALL read_frames(
    void *user_data,
    apta_source_frame_t first_frame,
    uint32_t requested_frames,
    apta_pcm_block_t *block_out)
{
    memory_source_t *source = (memory_source_t *)user_data;
    uint64_t remaining;
    uint32_t count;

    source->read_calls += 1u;
    if (first_frame >= source->total_frames) {
        return APTA_STATUS_END_OF_INPUT;
    }

    remaining = source->total_frames - first_frame;
    count = requested_frames;
    if ((uint64_t)count > remaining) {
        count = (uint32_t)remaining;
    }

    block_out->data = &source->samples[(size_t)first_frame];
    block_out->first_frame = first_frame;
    block_out->frame_count = count;
    return APTA_STATUS_OK;
}

static void APTA_CALL release_frames(void *user_data, apta_pcm_block_t *block)
{
    (void)user_data;
    block->data = NULL;
}

static int fail(const char *operation, apta_status_t status)
{
    fprintf(stderr, "%s failed with status %d\n", operation, (int)status);
    return 1;
}

int main(void)
{
    enum { FRAME_COUNT = 4096 };
    int16_t pcm[FRAME_COUNT];
    memory_source_t source_state;
    apta_pcm_source_t source;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_work_budget_t budget;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_waveform_overview_view_t overview;
    apta_status_t status;
    uint32_t index;
    uint32_t total_columns = 0u;

    for (index = 0u; index < FRAME_COUNT; ++index) {
        pcm[index] = (index & 1u) != 0u ? INT16_MAX : INT16_MIN;
    }

    source_state.samples = pcm;
    source_state.total_frames = FRAME_COUNT;
    source_state.read_calls = 0u;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    status = apta_context_create(&context_config, &context);
    if (status != APTA_STATUS_OK) {
        return fail("apta_context_create", status);
    }

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = FRAME_COUNT;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;

    status = apta_session_create(context, &session_config, &session);
    if (status != APTA_STATUS_OK) {
        (void)apta_context_destroy(context);
        return fail("apta_session_create", status);
    }

    apta_pcm_source_init(&source);
    source.user_data = &source_state;
    source.read_frames = read_frames;
    source.release_frames = release_frames;
    source.get_total_frames = get_total_frames;

    status = apta_session_set_source(session, &source);
    if (status != APTA_STATUS_OK) {
        (void)apta_session_destroy(session);
        (void)apta_context_destroy(context);
        return fail("apta_session_set_source", status);
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 512u;
    budget.maximum_steps = 8u;

    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK ||
             status == APTA_STATUS_MORE_WORK ||
             status == APTA_STATUS_WOULD_BLOCK);

    if (status != APTA_STATUS_END_OF_INPUT) {
        (void)apta_session_destroy(session);
        (void)apta_context_destroy(context);
        return fail("apta_session_process", status);
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        (void)apta_session_destroy(session);
        (void)apta_context_destroy(context);
        fputs("apta_session_acquire_result returned NULL\n", stderr);
        return 1;
    }

    apta_waveform_overview_view_init(&overview);
    status = apta_result_get_waveform_overview(result, 0u, &overview);
    if (status != APTA_STATUS_OK) {
        apta_result_release(result);
        (void)apta_session_destroy(session);
        (void)apta_context_destroy(context);
        return fail("apta_result_get_waveform_overview", status);
    }

    for (index = 0u; index < overview.span_count; ++index) {
        total_columns += overview.spans[index].column_count;
    }

    printf("Pull source served %u reads and produced %u waveform columns.\n",
           source_state.read_calls,
           total_columns);

    apta_result_release(result);
    (void)apta_session_destroy(session);
    (void)apta_context_destroy(context);
    return 0;
}
