// SPDX-License-Identifier: Apache-2.0
// Late revision acceptance and retained-slot failures through public C APIs.
#include <apta/apta.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(2); } } while (0)
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
        has ? t.selected.tempo_millibpm : 0, has ? t.selected.state : 0, 0,
        has_meter ? m.downbeat_frame : 0, has_meter ? m.downbeat_ordinal : 0,
        has_meter ? m.segments[0].applicability_range.first_frame : 0, has_meter ? m.segments[0].applicability_range.end_frame : 0, has_global ? g.flags : 0, has_global ? g.segment_count : 0, has_global ? g.beat_count : 0, 0, 0, (uint64_t)0, 0, has_local ? lg.flags : 0, has_local ? lg.applicability_range.first_frame : 0, has_local ? lg.applicability_range.end_frame : 0, has_revision ? rv.state : 0, has_revision ? rv.revision_id : 0);
    apta_result_release(r);
}
int main(int argc, char **argv) {
    (void)argv; const int all = argc > 1; const int detail = argc > 2;
    const uint64_t features = (detail ? APTA_FEATURE_WAVEFORM_DETAIL : 0) | (all ? APTA_FEATURE_MUSICAL_KEY | APTA_FEATURE_METER_DOWNBEAT | APTA_FEATURE_CALIBRATED_QUALITY : 0) | APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_DYNAMIC_TEMPO | APTA_FEATURE_GRID_LOCKING;
    apta_context_config_t cc; apta_context_config_init(&cc); cc.requested_capabilities = features;
    apta_context_t *c = NULL; CHECK(apta_context_create(&cc, &c) == 0);
    apta_session_config_t sc; apta_session_config_init(&sc); sc.requested_features = features;
    sc.source_sample_rate = 8000; sc.channel_count = 1; sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED; sc.total_frames = 640000; sc.overview_frames_per_column = 32768;
    sc.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    void *workspace = aligned_alloc(64, 4*1024*1024); CHECK(workspace); sc.static_workspace = workspace; sc.static_workspace_size = 4*1024*1024;
    apta_session_t *s = NULL; CHECK(apta_session_create(c, &sc, &s) == 0); trace(s);
    apta_work_budget_t budget; apta_work_budget_init(&budget);
    unsigned first = 0, locked = 0; float pcm[4096]; const apta_result_t *old = NULL;
    uint64_t generation = 0; unsigned id = 0;
    while (first < 640000) {
        unsigned n = 640000-first; if (n > 4096) n = 4096;
        for (unsigned j = 0; j < n; j++) { unsigned i = first+j; unsigned phase = i % (i < 320000 ? 3840 : 6000); pcm[j] = phase < 64 ? (float)(64-phase)/64.0f*0.75f : 0.0f; }
        apta_pcm_block_t b; apta_pcm_block_init(&b); b.first_frame = first; b.frame_count = n; b.data = pcm;
        unsigned accepted = 0; CHECK(apta_session_push_pcm(s, &b, &accepted) == 0 && accepted == n); first += n; trace(s);
        int status = apta_session_process(s, &budget, NULL); CHECK(status >= 0 || (old && status == APTA_ERROR_RESULT_SLOTS_EXHAUSTED)); trace(s);
        if (!locked && first >= 320000) { apta_frame_range_t range; apta_frame_range_init(&range); range.end_frame = 311808; CHECK(apta_session_lock_grid_range(s, &range) == 0); locked = 1; trace(s); }
        const apta_result_t *r = apta_session_acquire_result(s); CHECK(r); apta_result_info_t info; apta_result_info_init(&info); CHECK(apta_result_get_info(r, &info) == 0);
        apta_grid_revision_view_t rv; apta_grid_revision_view_init(&rv); int pending = apta_result_get_grid_revision(r, &rv) == 0 && rv.state == APTA_GRID_REVISION_PENDING;
        if (old && info.generation != generation) { id = rv.revision_id; apta_result_release(r); break; }
        if (!old && pending) { old = r; generation = info.generation; } else apta_result_release(r);
    }
    CHECK(old && first < 640000 && id != 0);
    const apta_result_t *newer = apta_session_acquire_result(s); CHECK(newer);
    CHECK(apta_session_apply_grid_revision(s, id) == APTA_ERROR_RESULT_SLOTS_EXHAUSTED); trace(s);
    CHECK(apta_session_apply_grid_revision(s, id) == APTA_ERROR_INVALID_STATE); trace(s);
    apta_result_release(old); budget.maximum_steps = 1; CHECK(apta_session_process(s, &budget, NULL) == APTA_ERROR_RESULT_SLOTS_EXHAUSTED); trace(s);
    apta_result_release(newer); newer = NULL; CHECK(apta_session_process(s, &budget, NULL) >= 0); trace(s);
    const apta_result_t *applied = apta_session_acquire_result(s); apta_grid_revision_view_t rv; apta_grid_revision_view_init(&rv); CHECK(apta_result_get_grid_revision(applied, &rv) == 0 && rv.state == APTA_GRID_REVISION_APPLIED);
    apta_result_release(applied); CHECK(apta_session_destroy(s) == 0); CHECK(apta_context_destroy(c) == 0); free(workspace); return 0;
}
