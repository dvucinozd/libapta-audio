// SPDX-License-Identifier: Apache-2.0
// Public unchanged-C traces for sparse overview input and publication.
#include <apta/apta.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "failed line %d: %s\n", __LINE__, #expression); \
            exit(2); \
        } \
    } while (0)

static void snapshot(apta_session_t *session, unsigned event, int status, unsigned accepted) {
    const apta_result_t *result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_result_info_t info;
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    apta_waveform_overview_view_t overview;
    apta_waveform_overview_view_init(&overview);
    int found = apta_result_get_waveform_overview(result, 0, &overview);
    CHECK(found == APTA_STATUS_OK || found == APTA_STATUS_NOT_AVAILABLE);
    printf("H %u %d %u %u %" PRIu64 " %u %" PRIu64 " %" PRIu64 " %u %u %u\n",
           event, status, accepted, apta_session_get_state(session), info.generation,
           info.session_state, info.available_features, info.changed_features,
           found == APTA_STATUS_OK ? overview.state : 0,
           found == APTA_STATUS_OK ? overview.confidence : 0,
           found == APTA_STATUS_OK ? overview.span_count : 0);
    if (found == APTA_STATUS_OK) {
        for (uint32_t i = 0; i < overview.span_count; ++i) {
            const apta_waveform_span_t *span = &overview.spans[i];
            printf("S %" PRIu64 " %" PRIu64 " %u\n", span->source_range.first_frame,
                   span->source_range.end_frame, span->column_count);
            for (uint32_t j = 0; j < span->column_count; ++j) {
                const apta_waveform_column_t *column = &span->columns[j];
                printf("C %d %d %u %u %u %u %u\n", column->minimum, column->maximum,
                       column->rms, column->low, column->mid, column->high, column->flags);
            }
        }
    }
    apta_result_release(result);
}

static void push(apta_session_t *session, unsigned event, uint64_t first, uint32_t count) {
    int16_t pcm[8192];
    CHECK(count <= 8192);
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t frame = first + i;
        pcm[i] = frame % 1024 < 512 ? (int16_t)(-30000 + (frame / 1024) * 1000)
                                  : (int16_t)(25000 - (frame / 1024) * 1000);
    }
    apta_pcm_block_t block;
    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = first;
    block.frame_count = count;
    uint32_t accepted = 0;
    int status = apta_session_push_pcm(session, &block, &accepted);
    snapshot(session, event, status, accepted);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    unsigned scenario = (unsigned)strtoul(argv[1], NULL, 10);
    CHECK(scenario <= 9);
    uint64_t total = scenario == 3 ? 2500 : scenario == 4 ? 8192 : 4096;
    apta_context_config_t context_config;
    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_context_t *context = NULL;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    apta_session_config_t config;
    apta_session_config_init(&config);
    config.source_sample_rate = 48000;
    config.channel_count = 1;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.total_frames = total;
    config.overview_frames_per_column = 1024;
    config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    // Keep allocator capacity out of these semantic cases. Exact allocator
    // workspace exhaustion has a separate contract from caller slice limits.
    void *workspace = aligned_alloc(64, 262144);
    CHECK(workspace != NULL);
    config.static_workspace = workspace;
    config.static_workspace_size = 262144;
    apta_session_t *session = NULL;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    snapshot(session, 0, APTA_STATUS_OK, 0);
    const apta_result_t *initial = scenario >= 7 ? apta_session_acquire_result(session) : NULL;
    switch (scenario) {
        case 0:
            push(session, 1, 2048, 1024);
            push(session, 2, 0, 1024);
            push(session, 3, 3072, 1024);
            push(session, 4, 1024, 1024);
            break;
        case 1:
            push(session, 1, 0, 1024);
            push(session, 2, 3072, 1024);
            break;
        case 2:
            push(session, 1, 1024, 1024);
            push(session, 2, 0, 2048);
            push(session, 3, 512, 128);
            push(session, 4, 2048, 2048);
            break;
        case 3:
            push(session, 1, 2048, 452);
            push(session, 2, 0, 1024);
            push(session, 3, 1024, 1024);
            break;
        case 4:
            push(session, 1, 0, 8192);
            push(session, 2, 4096, 4096);
            break;
        case 9:
        case 8:
        case 7:
            push(session, 1, 0, 4096);
            break;
        case 6:
            push(session, 1, 4096, 1);
            push(session, 2, UINT64_MAX, 1);
            push(session, 3, 0, 0);
            push(session, 4, 0, 4096);
            break;
        case 5:
            push(session, 1, 0, 512);
            push(session, 2, 1024, 1024);
            push(session, 3, 3072, 1024);
            break;
    }
    if (initial) {
        apta_work_budget_t retry_budget;
        apta_work_budget_init(&retry_budget);
        retry_budget.maximum_input_frames = 1024;
        retry_budget.maximum_steps = 4;
        snapshot(session, 12, apta_session_process(session, &retry_budget, NULL), 0);
        apta_result_release(initial);
        if (scenario == 8) {
            apta_session_request_cancel(session);
            snapshot(session, 14, apta_session_process(session, &retry_budget, NULL), 0);
            snapshot(session, 15, apta_session_process(session, &retry_budget, NULL), 0);
            CHECK(apta_session_get_state(session) == APTA_SESSION_CANCELLED);
            goto cleanup;
        }
        if (scenario != 9) {
            snapshot(session, 13, apta_session_process(session, &retry_budget, NULL), 0);
        }
    }
    int status = apta_session_signal_end_of_input(session, total);
    snapshot(session, 10, status, 0);
    snapshot(session, 11, apta_session_signal_end_of_input(session, total), 0);
    apta_work_budget_t budget;
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024;
    budget.maximum_steps = 4;
    for (unsigned event = 20; event < 40; ++event) {
        status = apta_session_process(session, &budget, NULL);
        snapshot(session, event, status, 0);
        CHECK(status >= 0);
        if (status == APTA_STATUS_END_OF_INPUT) break;
    }
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);
    snapshot(session, 40, apta_session_signal_end_of_input(session, total), 0);
cleanup:
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    free(workspace);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
