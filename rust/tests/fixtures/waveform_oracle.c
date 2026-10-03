// SPDX-License-Identifier: Apache-2.0
// Public C session oracle: stdin contains native interleaved f32 samples.
#include <apta/apta.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) return 2; } while (0)
int main(int argc, char **argv) {
    uint8_t samples[32768];
    uint32_t channels = argc > 1 ? (uint32_t)atoi(argv[1]) : 1;
    uint32_t format = argc > 2 ? (uint32_t)atoi(argv[2]) : 4;
    uint32_t width = format == 1 ? 2 : format == 2 ? 3 : 4;
    size_t bytes = fread(samples, 1, sizeof(samples), stdin);
    CHECK(format >= 1 && format <= 5 && bytes % width == 0);
    size_t n = bytes / width;
    CHECK(channels >= 1 && channels <= 2 && n > 0 && n % channels == 0);
    apta_context_config_t cc; apta_context_config_init(&cc);
    cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_context_t *ctx = NULL;
    CHECK(apta_context_create(&cc, &ctx) == APTA_STATUS_OK);
    apta_session_config_t sc; apta_session_config_init(&sc);
    sc.source_sample_rate = 48000; sc.channel_count = channels;
    sc.channel_layout = channels == 1 ? APTA_CHANNEL_LAYOUT_MONO : APTA_CHANNEL_LAYOUT_STEREO;
    sc.sample_format = format;
    sc.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    sc.total_frames = n / channels;
    apta_session_t *session = NULL;
    CHECK(apta_session_create(ctx, &sc, &session) == APTA_STATUS_OK);
    apta_pcm_block_t block; apta_pcm_block_init(&block);
    block.frame_count = n / channels;
    if (format == 5) {
        block.planes[0] = samples;
        if (channels == 2) block.planes[1] = samples + block.frame_count * width;
    } else {
        block.data = samples;
    }
    uint32_t accepted = 0;
    CHECK(apta_session_push_pcm(session, &block, &accepted) >= 0 && accepted == block.frame_count);
    CHECK(apta_session_signal_end_of_input(session, sc.total_frames) == APTA_STATUS_OK);
    apta_work_budget_t budget; apta_work_budget_init(&budget);
    for (unsigned i = 0; i < 10000; ++i) {
        apta_status_t status = apta_session_process(session, &budget, NULL);
        CHECK(status >= 0);
        if (status == APTA_STATUS_END_OF_INPUT) break;
        CHECK(i != 9999);
    }
    const apta_result_t *result = NULL;
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_t view; apta_waveform_overview_view_init(&view);
    CHECK(apta_result_get_waveform_overview(result, 0, &view) == APTA_STATUS_OK);
    printf("%u\n", view.level.frames_per_column);
    for (uint32_t s = 0; s < view.span_count; ++s) {
        for (uint32_t c = 0; c < view.spans[s].column_count; ++c) {
            const apta_waveform_column_t *col = &view.spans[s].columns[c];
            printf("%d %d %u %u %u %u %u\n", col->minimum, col->maximum, col->rms, col->low, col->mid, col->high, col->flags);
        }
    }
    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(ctx) == APTA_STATUS_OK);
    return 0;
}
