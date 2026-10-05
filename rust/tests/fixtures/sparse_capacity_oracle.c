// SPDX-License-Identifier: Apache-2.0
// Fragmented real PCM capacity and merging through unchanged public C.
#include <apta/apta.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "line %d: %s\n", __LINE__, #e); exit(2); } } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2 || argc == 3);
    unsigned unknown = argc == 3 ? (unsigned)strtoul(argv[2], NULL, 10) : 0; CHECK(unknown < 2);
    unsigned count = (unsigned)strtoul(argv[1], NULL, 10); CHECK(count && count <= 4096);
    apta_context_config_t cc; apta_context_config_init(&cc);
    cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_context_t *ctx = NULL; CHECK(apta_context_create(&cc, &ctx) == 0);
    apta_session_config_t c; apta_session_config_init(&c);
    c.source_sample_rate = 8000; c.channel_count = 1; c.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    c.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    c.total_frames = unknown ? APTA_TOTAL_FRAMES_UNKNOWN : count * 128u; c.overview_frames_per_column = 64;
    c.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_session_t *s = NULL; CHECK(apta_session_create(ctx, &c, &s) == 0);
    const apta_result_t *retained = NULL;
    apta_waveform_column_t *saved = malloc(count * sizeof(*saved)); CHECK(saved);
    apta_work_budget_t budget; apta_work_budget_init(&budget);
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned i = 0; i < count; ++i) {
            int16_t pcm[64]; for (unsigned j = 0; j < 64; ++j) pcm[j] = (int16_t)((i * 71 + j * 113 + pass * 17000) % 60000 - 30000);
            apta_pcm_block_t b; apta_pcm_block_init(&b);
            b.first_frame = i * 128u + pass * 64u; b.frame_count = 64; b.data = pcm;
            uint32_t accepted = 0; CHECK(apta_session_push_pcm(s, &b, &accepted) == 0 && accepted == 64);
            CHECK(apta_session_process(s, &budget, NULL) >= 0);
        }
        if (pass == 0) {
            retained = apta_session_acquire_result(s); CHECK(retained);
            apta_waveform_overview_view_t view; apta_waveform_overview_view_init(&view);
            CHECK(apta_result_get_waveform_overview(retained, 0, &view) == 0);
            CHECK(view.span_count == count);
            for (unsigned i = 0; i < count; ++i) {
                CHECK(view.spans[i].column_count == 1);
                saved[i] = view.spans[i].columns[0];
            }
        }
    }
    CHECK(apta_session_signal_end_of_input(s, count * 128u - 1) == APTA_ERROR_CONFLICT);
    CHECK(apta_session_signal_end_of_input(s, count * 128u) == 0);
    CHECK(apta_session_process(s, &budget, NULL) == APTA_STATUS_END_OF_INPUT);
    const apta_result_t *r = apta_session_acquire_result(s); CHECK(r);
    uint64_t size = 0; CHECK(apta_result_query_serialized_size(r, NULL, &size) == 0);
    void *bytes = malloc((size_t)size); CHECK(bytes);
    size_t written = 0; CHECK(apta_result_serialize(r, NULL, bytes, (size_t)size, &written) == 0);
    CHECK(fwrite(bytes, 1, written, stdout) == written);
    free(bytes); apta_result_release(r);
    CHECK(apta_session_destroy(s) == 0);
    CHECK(apta_context_destroy(ctx) == APTA_ERROR_BUSY);
    // Growing/merging/EOF and writer destruction must not rewrite this snapshot.
    apta_source_info_t source; apta_source_info_init(&source);
    CHECK(apta_result_get_source_info(retained, &source) == 0);
    CHECK(source.total_frames == c.total_frames);
    apta_waveform_overview_view_t view; apta_waveform_overview_view_init(&view);
    CHECK(apta_result_get_waveform_overview(retained, 0, &view) == 0);
    CHECK(view.span_count == count);
    for (unsigned i = 0; i < count; ++i) {
        CHECK(view.spans[i].source_range.first_frame == i * 128u);
        CHECK(view.spans[i].source_range.end_frame == i * 128u + 64);
        CHECK(view.spans[i].column_count == 1);
        CHECK(memcmp(&saved[i], &view.spans[i].columns[0], sizeof(*saved)) == 0);
    }
    free(saved);
    apta_result_release(retained);
    CHECK(apta_context_destroy(ctx) == 0);
    return 0;
}
