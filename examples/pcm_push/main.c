// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

static int fail(const char *operation, apta_status_t status)
{
    fprintf(stderr, "%s failed with status %d\n", operation, (int)status);
    return 1;
}

int main(void)
{
    enum { FRAME_COUNT = 4096, CHUNK_FRAMES = 512 };
    int16_t pcm[FRAME_COUNT];
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_work_budget_t budget;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_waveform_overview_view_t overview;
    apta_status_t status;
    uint32_t offset;
    uint32_t total_columns = 0u;

    for (offset = 0u; offset < FRAME_COUNT; ++offset) {
        const int32_t phase = (int32_t)(offset % 256u) - 128;
        pcm[offset] = (int16_t)(phase * 200);
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    status = apta_context_create(&context_config, &context);
    if (status != APTA_STATUS_OK) {
        return fail("apta_context_create", status);
    }

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PUSH;
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

    for (offset = 0u; offset < FRAME_COUNT; offset += CHUNK_FRAMES) {
        apta_pcm_block_t block;
        uint32_t accepted = 0u;

        apta_pcm_block_init(&block);
        block.data = &pcm[offset];
        block.first_frame = offset;
        block.frame_count = CHUNK_FRAMES;

        status = apta_session_push_pcm(session, &block, &accepted);
        if (status != APTA_STATUS_OK || accepted != CHUNK_FRAMES) {
            (void)apta_session_destroy(session);
            (void)apta_context_destroy(context);
            return fail("apta_session_push_pcm", status);
        }
    }

    status = apta_session_signal_end_of_input(session, FRAME_COUNT);
    if (status != APTA_STATUS_OK) {
        (void)apta_session_destroy(session);
        (void)apta_context_destroy(context);
        return fail("apta_session_signal_end_of_input", status);
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = CHUNK_FRAMES;
    budget.maximum_steps = 64u;

    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);

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

    for (offset = 0u; offset < overview.span_count; ++offset) {
        total_columns += overview.spans[offset].column_count;
    }

    printf("APTA package %s produced %u waveform columns (%u frames/column).\n",
           APTA_PACKAGE_VERSION_STRING,
           total_columns,
           overview.level.frames_per_column);

    apta_result_release(result);
    (void)apta_session_destroy(session);
    (void)apta_context_destroy(context);
    return 0;
}
