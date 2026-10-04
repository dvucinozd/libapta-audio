// SPDX-License-Identifier: Apache-2.0
// Unchanged compiled C; all mutations and observations use public APIs.
#include <apta/apta.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(2); } } while (0)
static unsigned clock_calls, reads, releases, requested;
static uint64_t read_first;
static unsigned input_profile, input_count = 320000, input_rate = 8000;
static float sample(unsigned frame) {
    if (input_profile & 1024) return 0.0f;
    if (input_profile & 4096) return 0.25f;
    unsigned tempo = (input_profile & 32768) ? (unsigned[]){80,160,120,200}[(frame/65536)%4] : 120;
    unsigned period = input_rate * 60 / tempo, phase = frame % period;
    float value = phase < 64 ? (float)(64-phase)/64.0f * (frame/period%4 == 0 ? 0.75f : 0.2f) : 0.0f;
    return (input_profile & 2048) ? value * 1e-8f : value;
}
static float source_pcm[4096];
static uint64_t total(void *u) { (void)u; return input_count; }
static apta_status_t read_pcm(void *u, uint64_t first, uint32_t maximum, apta_pcm_block_t *b) {
    (void)u; reads++; read_first = first; requested = maximum; CHECK(maximum <= 4096);
    for (unsigned j = 0; j < maximum; j++) source_pcm[j] = sample((unsigned)first+j);
    b->first_frame = first; b->frame_count = maximum; b->data = source_pcm; return APTA_STATUS_OK;
}
static void release_pcm(void *u, apta_pcm_block_t *b) { (void)u; b->data = NULL; releases++; }
static uint64_t tick(void *user) { (void)user; return (uint64_t)(++clock_calls) * 1000; }
static void trace(apta_session_t *s) {
    const apta_result_t *r = apta_session_acquire_result(s); CHECK(r);
    apta_result_info_t i; apta_result_info_init(&i); CHECK(apta_result_get_info(r, &i) == 0);
    apta_tempo_view_t t; apta_tempo_view_init(&t);
    int has = apta_result_get_tempo(r, NULL, &t) == 0;
    apta_grid_view_t g; apta_grid_view_init(&g); int has_global = apta_result_get_beatgrid(r, APTA_FEATURE_GLOBAL_BEATGRID, NULL, &g) == 0;
    apta_meter_view_t m; apta_meter_view_init(&m); int has_meter = apta_result_get_meter(r, NULL, &m) == 0;
    apta_grid_view_t lg; apta_grid_view_init(&lg); int has_local = apta_result_get_beatgrid(r, APTA_FEATURE_LOCAL_BEATGRID, NULL, &lg) == 0;
    apta_grid_revision_view_t rv; apta_grid_revision_view_init(&rv); int has_revision = apta_result_get_grid_revision(r, &rv) == 0;
    printf("%" PRIu64 " %u %" PRIu64 " %" PRIu64 " %u %u %u %" PRIu64 " %" PRId64 " %" PRIu64 " %" PRIu64 " %u %u %u %u %u %" PRIu64 " %u %u %" PRIu64 " %" PRIu64 " %u %u\n", i.generation,
        i.session_state, i.available_features, i.changed_features,
        has ? t.selected.tempo_millibpm : 0, has ? t.selected.state : 0, clock_calls,
        has_meter ? m.downbeat_frame : 0, has_meter ? m.downbeat_ordinal : 0,
        has_meter ? m.segments[0].applicability_range.first_frame : 0, has_meter ? m.segments[0].applicability_range.end_frame : 0, has_global ? g.flags : 0, has_global ? g.segment_count : 0, has_global ? g.beat_count : 0, reads, releases, read_first, requested, has_local ? lg.flags : 0, has_local ? lg.applicability_range.first_frame : 0, has_local ? lg.applicability_range.end_frame : 0, has_revision ? rv.state : 0, has_revision ? rv.revision_id : 0);
    apta_result_release(r);
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    unsigned profile = (unsigned)strtoul(argv[1], NULL, 10);
    input_profile = profile;
    if (profile & (8192|16384)) input_count = 256*16400+17;
    if (profile & 16384) input_rate = 2000;
    if (profile & 32768) input_count = 256*4800+17;
    uint64_t features = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID;
    if (profile & 1) features |= APTA_FEATURE_CONFIDENCE | APTA_FEATURE_GRID_LOCKING;
    if (profile & 2) features |= APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO | APTA_FEATURE_MUSICAL_KEY | APTA_FEATURE_METER_DOWNBEAT | APTA_FEATURE_CALIBRATED_QUALITY;
    /* Capability projections use independent public C sessions. */
    if (profile & 65536) features = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM;
    if (profile & 131072) features = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_MUSICAL_KEY;
    if (profile & 262144) features = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM | APTA_FEATURE_GLOBAL_BEATGRID;
    if (profile & 524288) features = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM | APTA_FEATURE_CONFIDENCE | APTA_FEATURE_CALIBRATED_QUALITY;
    apta_context_config_t cc; apta_context_config_init(&cc); cc.requested_capabilities = features; cc.clock.monotonic_time_ns = tick;
    apta_context_t *c = NULL; CHECK(apta_context_create(&cc, &c) == 0);
    apta_session_config_t sc; apta_session_config_init(&sc);
    sc.requested_features = features; sc.source_sample_rate = input_rate; sc.channel_count = 1;
    sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO; sc.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
    sc.total_frames = input_count; sc.overview_frames_per_column = 32768;
    sc.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    if (profile & 512) { sc.total_frames = APTA_TOTAL_FRAMES_UNKNOWN; sc.flags = 0; }
    if (profile & 32) sc.input_mode = APTA_INPUT_MODE_PULL;
    void *workspace = aligned_alloc(64, 4 * 1024 * 1024); CHECK(workspace);
    sc.static_workspace = workspace; sc.static_workspace_size = 4 * 1024 * 1024;
    apta_session_t *s = NULL; CHECK(apta_session_create(c, &sc, &s) == 0); trace(s);
    apta_frame_range_t lock; apta_frame_range_init(&lock);
    CHECK(apta_session_lock_grid_range(s, &lock) == APTA_ERROR_INVALID_ARGUMENT);
    lock.end_frame = 1;
    CHECK(apta_session_lock_grid_range(s, &lock) == ((profile & 1) ? APTA_ERROR_INVALID_STATE : APTA_ERROR_UNSUPPORTED));
    apta_work_budget_t budget; apta_work_budget_init(&budget); if (profile & (4|256)) budget.soft_time_budget_us = (profile & 256) ? 20 : 1000000;
    const apta_result_t *retained = NULL;
    if (profile & 32) {
        apta_pcm_source_t src; apta_pcm_source_init(&src); src.read_frames = read_pcm; src.release_frames = release_pcm; src.get_total_frames = total;
        CHECK(apta_session_set_source(s, &src) == 0);
        if (profile & 64) {
            apta_focus_t focus; apta_focus_init(&focus); focus.feature_mask = APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID;
            focus.playhead_frame = 163840; focus.lookahead_frames = 32768; focus.priority = 240;
            CHECK(apta_session_set_focus(s, &focus) == 0);
            apta_region_request_t request; apta_region_request_init(&request); request.feature_mask = focus.feature_mask;
            request.range.first_frame = 163840; request.range.end_frame = 196608; request.priority = 96;
            unsigned id = 0; CHECK(apta_session_request_region(s, &request, &id) == 0);
            apta_pcm_request_t demand; apta_pcm_request_init(&demand); CHECK(apta_session_next_pcm_request(s, &demand) == 0 && demand.range.first_frame == 163840 && demand.request_token == id);
        }
        unsigned complete = 0;
        for (unsigned i = 0; i < 200; i++) { int status = apta_session_process(s, &budget, NULL); CHECK(status >= 0); trace(s); if (status == APTA_STATUS_END_OF_INPUT) { complete = 1; break; } }
        CHECK(complete); goto done;
    }
    retained = (profile & 16) ? apta_session_acquire_result(s) : NULL;
    float pcm[4096];
    for (unsigned first = 0; first < input_count;) {
        unsigned n = input_count-first; if (n > 4096) n = 4096;
        for (unsigned j = 0; j < n; j++) pcm[j] = sample(first+j);
        apta_pcm_block_t b; apta_pcm_block_init(&b); b.first_frame = first; b.frame_count = n; b.data = pcm;
        unsigned accepted = 0; CHECK(apta_session_push_pcm(s, &b, &accepted) == 0 && accepted == n); trace(s);
        if ((profile & 8) && (retained || first >= 163840)) {
            apta_session_request_cancel(s);
            int status = apta_session_process(s, &budget, NULL); trace(s);
            if (retained) {
                CHECK(status == APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
                apta_result_info_t i; apta_result_info_init(&i); CHECK(apta_result_get_info(retained, &i) == 0 && i.generation == 1 && i.available_features == 0);
                apta_result_release(retained); retained = NULL;
                CHECK(apta_session_process(s, &budget, NULL) == APTA_ERROR_CANCELLED); trace(s);
            } else CHECK(status == APTA_ERROR_CANCELLED);
            goto done;
        }
        int status = apta_session_process(s, &budget, NULL); trace(s);
        if (status == APTA_ERROR_RESULT_SLOTS_EXHAUSTED && retained) {
            apta_result_info_t i; apta_result_info_init(&i); CHECK(apta_result_get_info(retained, &i) == 0 && i.generation == 1 && i.available_features == 0);
            apta_result_release(retained); retained = NULL;
            CHECK(apta_session_process(s, &budget, NULL) >= 0); trace(s);
        } else CHECK(status >= 0);
        first += n;
    }
    if (profile & 1048576) retained = apta_session_acquire_result(s);
    CHECK(apta_session_signal_end_of_input(s, input_count) == 0); trace(s);
    if (profile & 1048576) {
        lock.first_frame = input_count/4; lock.end_frame = input_count*3/4;
        CHECK(apta_session_lock_grid_range(s, &lock) == APTA_ERROR_RESULT_SLOTS_EXHAUSTED); trace(s);
        apta_result_release(retained); retained = NULL;
        CHECK(apta_session_lock_grid_range(s, &lock) == 0); trace(s);
        CHECK(apta_session_lock_grid_range(s, &lock) == 0); trace(s);
    }
    { unsigned complete = 0;
      for (unsigned i = 0; i < 1000; i++) { int status = apta_session_process(s, &budget, NULL); CHECK(status >= 0); trace(s); if (status == APTA_STATUS_END_OF_INPUT) { complete = 1; break; } }
      CHECK(complete);
    }
done:
    if (retained) apta_result_release(retained);
    CHECK(apta_session_destroy(s) == 0); CHECK(apta_context_destroy(c) == 0); free(workspace);
    return 0;
}
