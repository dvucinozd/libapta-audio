// SPDX-License-Identifier: Apache-2.0
// Trace unchanged C bounded waveform publication through public interfaces.
#include <apta/apta.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "failed line %d: %s\n", __LINE__, #expression); \
            return 2; \
        } \
    } while (0)

static void trace(unsigned event, int status, unsigned accepted,
                  apta_session_t *session, const apta_result_t *retained) {
    const apta_result_t *result = retained ? retained : apta_session_acquire_result(session);
    apta_result_info_t info;
    apta_result_info_init(&info);
    if (!result || apta_result_get_info(result, &info) != APTA_STATUS_OK) {
        fprintf(stderr, "result info unavailable at event %u\n", event);
        exit(2);
    }
    apta_waveform_overview_view_t overview;
    apta_waveform_overview_view_init(&overview);
    int waveform_status = apta_result_get_waveform_overview(result, 0, &overview);
    if (waveform_status != APTA_STATUS_OK && waveform_status != APTA_STATUS_NOT_AVAILABLE) {
        fprintf(stderr, "unexpected overview status %d at event %u\n", waveform_status, event);
        exit(2);
    }
    unsigned count = waveform_status == 0 ? overview.spans[0].column_count : 0;
    const apta_waveform_column_t *column = count ? overview.spans[0].columns : NULL;
    printf("%u %d %u %d %" PRIu64 " %u %" PRIu64 " %" PRIu64
           " %u %u %d %d %u %u %u\n",
           event, status, accepted, session ? (int)apta_session_get_state(session) : -1,
           info.generation, info.session_state, info.available_features, info.changed_features,
           waveform_status == 0 ? overview.state : 0, count,
           column ? column->minimum : 0, column ? column->maximum : 0,
           column ? column->rms : 0, column ? column->flags : 0,
           waveform_status == 0 ? overview.confidence : 0);
    if (!retained) apta_result_release(result);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    unsigned scenario = (unsigned)strtoul(argv[1], NULL, 10);
    CHECK(scenario <= 4);
    apta_context_config_t context_config;
    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_context_t *context = NULL;
    CHECK(apta_context_create(&context_config, &context) == 0);

    apta_session_config_t config;
    apta_session_config_init(&config);
    config.source_sample_rate = 48000;
    config.channel_count = 1;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.total_frames = 1024;
    config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    apta_memory_requirements_t requirements;
    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(&config, &requirements) == 0);
    size_t allocation = (requirements.minimum_bytes + requirements.required_alignment - 1)
                        / requirements.required_alignment * requirements.required_alignment;
    // The C minimum query alone cannot create this bounded session. Existing C
    // bounded-slot tests also provide a 64 KiB workspace; planner parity is separate.
    if (allocation < 65536) allocation = 65536;
    void *workspace = aligned_alloc(requirements.required_alignment, allocation);
    CHECK(workspace != NULL);
    config.static_workspace = workspace;
    config.static_workspace_size = allocation;
    apta_session_t *session = NULL;
    int create_status = apta_session_create(context, &config, &session);
    if (create_status) {
        fprintf(stderr, "create=%d bytes=%zu alignment=%zu\n",
                create_status, allocation, requirements.required_alignment);
    }
    CHECK(create_status == 0);

    const apta_result_t *initial = scenario >= 1 && scenario <= 3
                                 ? apta_session_acquire_result(session) : NULL;
    trace(0, 0, 0, session, NULL);
    apta_work_budget_t budget;
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024;
    budget.maximum_steps = 4;
    apta_status_t status;
    uint32_t accepted = 0;
    if (scenario == 2) {
        apta_session_request_cancel(session);
        status = apta_session_process(session, &budget, NULL);
        trace(1, status, 0, session, NULL);
        goto retained;
    }

    int16_t pcm[1024];
    for (unsigned i = 0; i < 1024; i++) pcm[i] = (i & 1) ? INT16_MAX : INT16_MIN;
    apta_pcm_block_t block;
    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 1024;
    status = apta_session_push_pcm(session, &block, &accepted);
    trace(1, status, accepted, session, NULL);
    if (scenario == 3) {
        apta_session_request_cancel(session);
        status = apta_session_process(session, &budget, NULL);
        trace(2, status, 0, session, NULL);
        apta_result_release(initial);
        initial = NULL;
        status = apta_session_process(session, &budget, NULL);
        trace(3, status, 0, session, NULL);
        goto retained;
    }
    if (scenario != 4) {
        status = apta_session_process(session, &budget, NULL);
        trace(2, status, 0, session, NULL);
    }
    if (scenario == 1) {
        trace(20, 0, 0, session, initial);
        apta_result_release(initial);
        initial = NULL;
        status = apta_session_process(session, &budget, NULL);
        trace(3, status, 0, session, NULL);
    }
    {
        const apta_result_t *partial = scenario == 1 ? apta_session_acquire_result(session) : NULL;
        status = apta_session_signal_end_of_input(session, 1024);
        trace(4, status, 0, session, NULL);
        status = apta_session_process(session, &budget, NULL);
        trace(5, status, 0, session, NULL);
        if (partial) {
            trace(21, 0, 0, session, partial);
            apta_result_release(partial);
            status = apta_session_process(session, &budget, NULL);
            trace(6, status, 0, session, NULL);
        }
    }
retained:
    {
        const apta_result_t *final = apta_session_acquire_result(session);
        CHECK(apta_session_destroy(session) == 0);
        session = NULL;
        free(workspace);
        trace(9, 0, 0, NULL, final);
        if (initial) {
            trace(22, 0, 0, NULL, initial);
            apta_result_release(initial);
        }
        apta_result_release(final);
    }
    CHECK(apta_context_destroy(context) == 0);
    return 0;
}
