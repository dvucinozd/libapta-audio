// SPDX-License-Identifier: Apache-2.0
// Unchanged C filter arithmetic plus both public publication paths.
#include "../../../src/core/apta_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "failed line %d: %s\n", __LINE__, #expression); \
            exit(2); \
        } \
    } while (0)

static uint32_t bits(float value) {
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static void public_result(const float *samples, uint32_t count, uint32_t rate,
                          uint32_t frames_per_column, int bounded) {
    apta_context_config_t cc;
    apta_context_config_init(&cc);
    cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_3BAND;
    apta_context_t *context = NULL;
    CHECK(apta_context_create(&cc, &context) == APTA_STATUS_OK);
    apta_session_config_t config;
    apta_session_config_init(&config);
    config.source_sample_rate = rate;
    config.channel_count = 1;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
    config.total_frames = count;
    config.overview_frames_per_column = frames_per_column;
    config.requested_features = cc.requested_capabilities;
    config.flags = bounded ? APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS : 0;
    void *workspace = aligned_alloc(64, 2097152);
    CHECK(workspace != NULL);
    config.static_workspace = workspace;
    config.static_workspace_size = 2097152;
    apta_session_t *session = NULL;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    apta_work_budget_t budget;
    apta_work_budget_init(&budget);
    for (uint32_t first = 0; first < count;) {
        uint32_t n = count - first;
        if (n > 4096) n = 4096;
        apta_pcm_block_t block;
        apta_pcm_block_init(&block);
        block.first_frame = first;
        block.frame_count = n;
        block.data = samples + first;
        uint32_t accepted = 0;
        CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
        CHECK(accepted == n);
        CHECK(apta_session_process(session, &budget, NULL) >= 0);
        first += accepted;
    }
    CHECK(apta_session_signal_end_of_input(session, count) == APTA_STATUS_OK);
    CHECK(apta_session_process(session, &budget, NULL) == APTA_STATUS_END_OF_INPUT);
    const apta_result_t *result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_result_info_t info;
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    apta_waveform_overview_view_t overview;
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0, &overview) == APTA_STATUS_OK);
    CHECK(overview.span_count == 1);
    printf("P %d %" PRIu64 "\n", bounded, info.available_features);
    for (uint32_t i = 0; i < overview.spans[0].column_count; ++i) {
        const apta_waveform_column_t *column = &overview.spans[0].columns[i];
        printf("C %d %d %u %u %u %u %u\n", column->minimum, column->maximum,
               column->rms, column->low, column->mid, column->high, column->flags);
    }
    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    free(workspace);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    uint32_t rate = (uint32_t)strtoul(argv[1], NULL, 10);
    uint32_t frames_per_column = (uint32_t)strtoul(argv[2], NULL, 10);
    float *samples = malloc(65536 * sizeof(float));
    CHECK(samples != NULL);
    size_t count = fread(samples, sizeof(float), 65536, stdin);
    CHECK(count != 0 && !ferror(stdin) && fgetc(stdin) == EOF);
    apta_internal_band_filter_t filter;
    apta_internal_band_filter_init(&filter, rate);
    printf("K %08" PRIx32 " %08" PRIx32 "\n", bits(filter.low_coefficient), bits(filter.mid_coefficient));
    for (size_t i = 0; i < count; ++i) {
        float output[3];
        apta_internal_band_filter_split(&filter, samples[i], output);
        printf("F %08" PRIx32 " %08" PRIx32 " %08" PRIx32 "\n",
               bits(output[0]), bits(output[1]), bits(output[2]));
    }
    public_result(samples, (uint32_t)count, rate, frames_per_column, 0);
    public_result(samples, (uint32_t)count, rate, frames_per_column, 1);
    free(samples);
    return 0;
}
