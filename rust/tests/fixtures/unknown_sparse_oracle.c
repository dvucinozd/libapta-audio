// SPDX-License-Identifier: Apache-2.0
// Establish initially-unknown sparse push contracts before native implementation.
#include <apta/apta.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"line %d: %s\n",__LINE__,#e); exit(2); } } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2); unsigned holes = (unsigned)strtoul(argv[1], NULL, 10); CHECK(holes < 2);
    apta_context_config_t cc; apta_context_config_init(&cc); cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_context_t *c = NULL; CHECK(apta_context_create(&cc, &c) == 0);
    apta_session_config_t sc; apta_session_config_init(&sc); sc.requested_features = cc.requested_capabilities;
    sc.source_sample_rate = 8000; sc.channel_count = 1; sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED; sc.total_frames = APTA_TOTAL_FRAMES_UNKNOWN; sc.overview_frames_per_column = 64;
    apta_session_t *s = NULL;
    sc.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    CHECK(apta_session_create(c, &sc, &s) == APTA_ERROR_INVALID_ARGUMENT && !s);
    sc.flags = 0; CHECK(apta_session_create(c, &sc, &s) == 0);
    const apta_result_t *initial = apta_session_acquire_result(s); CHECK(initial);
    const unsigned offsets[] = {128, 0, 256, 64, 192};
    apta_work_budget_t budget; apta_work_budget_init(&budget);
    for (unsigned i = 0; i < (holes ? 3u : 5u); ++i) {
        int16_t pcm[64]; for (unsigned j = 0; j < 64; ++j) pcm[j] = (int16_t)((offsets[i]+j)*71-12000);
        apta_pcm_block_t b; apta_pcm_block_init(&b); b.first_frame = offsets[i]; b.frame_count = 64; b.data = pcm;
        uint32_t accepted = 0; CHECK(apta_session_push_pcm(s, &b, &accepted) == 0 && accepted == 64);
        CHECK(apta_session_process(s, &budget, NULL) >= 0);
    }
    CHECK(apta_session_signal_end_of_input(s, 319) == APTA_ERROR_CONFLICT);
    CHECK(apta_session_signal_end_of_input(s, 320) == 0);
    CHECK(apta_session_signal_end_of_input(s, 320) == 0);
    CHECK(apta_session_signal_end_of_input(s, 321) == APTA_ERROR_CONFLICT);
    CHECK(apta_session_process(s, &budget, NULL) == APTA_STATUS_END_OF_INPUT);
    const apta_result_t *r = apta_session_acquire_result(s); CHECK(r);
    uint64_t size = 0; CHECK(apta_result_query_serialized_size(r, NULL, &size) == 0);
    void *data = malloc((size_t)size); CHECK(data); size_t written = 0;
    CHECK(apta_result_serialize(r, NULL, data, (size_t)size, &written) == 0);
    CHECK(fwrite(data, 1, written, stdout) == written); free(data);
    apta_result_release(r); CHECK(apta_session_destroy(s) == 0);
    CHECK(apta_context_destroy(c) == APTA_ERROR_BUSY);
    apta_result_info_t info; apta_result_info_init(&info); CHECK(apta_result_get_info(initial, &info) == 0 && info.generation == 1);
    apta_result_release(initial); CHECK(apta_context_destroy(c) == 0); return 0;
}
