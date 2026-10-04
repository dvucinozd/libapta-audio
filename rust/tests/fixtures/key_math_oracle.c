// SPDX-License-Identifier: Apache-2.0
// Public mutations of unchanged C; test-only inspection isolates numeric drift.
#include "../../../src/key/apta_key_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d\n", __LINE__); exit(2); } } while (0)
int main(int argc, char **argv) {
    (void)argv; const int trace = argc > 1;
    apta_context_config_t cc; apta_context_config_init(&cc);
    cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_MUSICAL_KEY;
    apta_context_t *c = NULL; CHECK(apta_context_create(&cc, &c) == 0);
    apta_session_config_t sc; apta_session_config_init(&sc);
    sc.requested_features = cc.requested_capabilities; sc.source_sample_rate = 8000;
    sc.channel_count = 1; sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED; sc.total_frames = 320000;
    apta_session_t *s = NULL; CHECK(apta_session_create(c, &sc, &s) == 0);
    float pcm[4096]; apta_work_budget_t budget; apta_work_budget_init(&budget);
    for (unsigned first = 0; first < 320000;) {
        unsigned n = 320000 - first; if (n > (trace ? 256u : 4096u)) n = trace ? 256u : 4096u;
        for (unsigned i = 0; i < n; ++i) { unsigned phase = (first+i)%4000; pcm[i] = phase < 64 ? (float)(64-phase)/64.0f * 0.75f : 0.0f; }
        apta_pcm_block_t b; apta_pcm_block_init(&b); b.first_frame = first; b.frame_count = n; b.data = pcm;
        unsigned accepted = 0; CHECK(apta_session_push_pcm(s, &b, &accepted) == 0 && accepted == n);
        CHECK(apta_session_process(s, &budget, NULL) >= 0); first += n;
        if (trace) {
            if (first == 256) CHECK(fwrite(s->key_analysis.coefficients[0], sizeof(float), 36, stdout) == 36);
            CHECK(fwrite(s->key_analysis.q1[0], sizeof(float), 36, stdout) == 36);
            CHECK(fwrite(s->key_analysis.q2[0], sizeof(float), 36, stdout) == 36);
            CHECK(fwrite(s->key_analysis.chroma[0], sizeof(float), 12, stdout) == 12);
        }
    }
    CHECK(apta_session_signal_end_of_input(s, 320000) == 0);
    int status = 0; for (unsigned i = 0; i < 1000 && status != APTA_STATUS_END_OF_INPUT; ++i) { status = apta_session_process(s, &budget, NULL); CHECK(status >= 0); }
    CHECK(status == APTA_STATUS_END_OF_INPUT);
    if (!trace) {
        CHECK(fwrite(s->key_analysis.coefficients[0], sizeof(float), 36, stdout) == 36);
        CHECK(fwrite(s->key_analysis.chroma[0], sizeof(float), 12, stdout) == 12);
    }
    const apta_result_t *r = apta_session_acquire_result(s); CHECK(r);
    apta_key_view_t key; apta_key_view_init(&key); CHECK(apta_result_get_key(r, NULL, &key) == 0 && key.candidate_count == 3);
    for (unsigned i = 0; i < 3; ++i) CHECK(fwrite(&key.candidates[i].score, sizeof(uint16_t), 1, stdout) == 1);
    apta_result_release(r);
    CHECK(apta_session_destroy(s) == 0); CHECK(apta_context_destroy(c) == 0); return 0;
}
