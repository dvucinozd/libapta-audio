// SPDX-License-Identifier: Apache-2.0
// Public context/session/result lifetime contract, unchanged C.
#include <apta/apta.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 2; } } while (0)
int main(void) {
    apta_context_config_t cc; apta_context_config_init(&cc); cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_context_t *c = NULL; CHECK(apta_context_create(&cc, &c) == 0);
    apta_session_config_t sc; apta_session_config_init(&sc); sc.requested_features = cc.requested_capabilities;
    sc.source_sample_rate = 48000; sc.channel_count = 1; sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO; sc.total_frames = 64; sc.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED; sc.overview_frames_per_column = 64;
    apta_session_t *s = NULL; CHECK(apta_session_create(c, &sc, &s) == 0);
    CHECK(apta_context_destroy(c) == APTA_ERROR_BUSY); puts("busy");
    const apta_result_t *r = apta_session_acquire_result(s); CHECK(r);
    CHECK(apta_session_destroy(s) == 0); CHECK(apta_context_destroy(c) == APTA_ERROR_BUSY); puts("busy");
    apta_result_info_t info; apta_result_info_init(&info); CHECK(apta_result_get_info(r, &info) == 0 && info.generation == 1);
    apta_result_release(r); CHECK(apta_context_destroy(c) == 0); puts("closed"); return 0;
}
