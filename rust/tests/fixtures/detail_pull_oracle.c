// SPDX-License-Identifier: Apache-2.0
// Public C detail-enabled scheduled pull callbacks, release and failures.
// The immutable result detail tiles are dumped for exact comparison.
#include "apta_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed line %d\n", __LINE__); exit(2); } } while (0)
typedef struct {
    unsigned scenario, reads, releases;
    uint64_t first;
    uint32_t requested;
    int16_t pcm[4097];
    apta_session_t *session;
} source_state;
static uint64_t APTA_CALL total(void *u) { (void)u; return 4096; }
static apta_status_t APTA_CALL read_frames(void *u, uint64_t first, uint32_t maximum, apta_pcm_block_t *block) {
    source_state *s = u;
    s->reads++;
    s->first = first;
    s->requested = maximum;
    if (s->scenario == 13) { apta_session_request_cancel(s->session); return APTA_STATUS_WOULD_BLOCK; }
    if (s->scenario == 1 && s->reads == 1) return APTA_STATUS_WOULD_BLOCK;
    if (s->scenario == 4 || s->scenario == 14 || (s->scenario == 6 && s->reads >= 2)) return APTA_ERROR_CORRUPT_DATA;
    if (s->scenario == 5) return APTA_STATUS_END_OF_INPUT;
    uint32_t n = maximum;
    if (s->scenario == 1 && n > 300) n = 300;
    if (s->scenario == 8) n = 0;
    if (s->scenario == 9) n = maximum + 1;
    CHECK(n <= 4097);
    for (unsigned i = 0; i < n; i++) s->pcm[i] = (int16_t)(1000 + ((first + i) / 1024) * 1000);
    block->data = s->pcm;
    block->first_frame = first + (s->scenario == 3);
    block->frame_count = n;
    if (s->scenario == 7) apta_session_request_cancel(s->session);
    return APTA_STATUS_OK;
}
static void APTA_CALL release_frames(void *u, apta_pcm_block_t *block) {
    source_state *s = u;
    s->releases++;
    block->data = NULL;
}
static void snapshot(unsigned step, int status, source_state *s) {
    const apta_result_t *r = apta_session_acquire_result(s->session);
    CHECK(r);
    apta_result_info_t info; apta_result_info_init(&info);
    CHECK(apta_result_get_info(r, &info) == 0);
    apta_waveform_overview_view_t w; apta_waveform_overview_view_init(&w);
    int found = apta_result_get_waveform_overview(r, 0, &w);
    CHECK(found == 0 || found == APTA_STATUS_NOT_AVAILABLE);
    printf("H %u %d %u %u %" PRIu64 " %u %u %" PRIu64 " %u %" PRIu64 " %" PRIu64 " %u %u %u\n",
        step, status, s->reads, s->releases, s->first, s->requested,
        apta_session_get_state(s->session), info.generation, info.session_state,
        info.available_features, info.changed_features, found == 0 ? w.state : 0, found == 0 ? w.span_count : 0, found == 0 ? w.confidence : 0);
    if (found == 0) for (unsigned i = 0; i < w.span_count; i++) {
        const apta_waveform_span_t *span = &w.spans[i];
        printf("S %" PRIu64 " %" PRIu64 " %u\n", span->source_range.first_frame, span->source_range.end_frame, span->column_count);
        for (unsigned j = 0; j < span->column_count; j++) {
            const apta_waveform_column_t *c = &span->columns[j];
            printf("C %d %d %u %u %u %u %u\n", c->minimum, c->maximum, c->rms, c->low, c->mid, c->high, c->flags);
        }
    }
    for(unsigned i=0;i<r->detail_tile_count;i++) {
        const apta_waveform_tile_view_t *t=&r->detail_tiles[i];
        printf("T %u %u %" PRIu64 " %" PRIu64 " %u %u %u %u\n",t->level_id,t->tile_index,t->source_range.first_frame,t->source_range.end_frame,t->first_column_index,t->column_count,t->state,t->confidence);
        for(unsigned j=0;j<t->column_count;j++) {const apta_waveform_column_t *v=&t->columns[j]; printf("V %d %d %u %u\n",v->minimum,v->maximum,v->rms,v->flags);}
    }
    apta_result_release(r);
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    source_state state = {0}; state.scenario = (unsigned)strtoul(argv[1], NULL, 10); CHECK(state.scenario <= 17);
    apta_context_config_t cc; apta_context_config_init(&cc); cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL;
    if (state.scenario >= 16) cc.requested_capabilities |= APTA_FEATURE_WAVEFORM_3BAND;
    apta_context_t *context = NULL; CHECK(apta_context_create(&cc, &context) == 0);
    apta_session_config_t config; apta_session_config_init(&config);
    config.source_sample_rate = 48000; config.channel_count = 1; config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED; config.total_frames = 4096;
    config.requested_features = cc.requested_capabilities; config.input_mode = APTA_INPUT_MODE_PULL;
    config.overview_frames_per_column = 1024; config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    void *workspace = aligned_alloc(64, 262144); CHECK(workspace);
    config.static_workspace = workspace; config.static_workspace_size = 262144;
    CHECK(apta_session_create(context, &config, &state.session) == 0);
    apta_pcm_source_t source; apta_pcm_source_init(&source); source.user_data = &state;
    source.get_total_frames = total; source.read_frames = read_frames; source.release_frames = release_frames;
    CHECK(apta_session_set_source(state.session, &source) == 0);
    if (state.scenario >= 16) {
        apta_result_builder_options_t options; apta_result_builder_options_init(&options);
        apta_result_builder_t *builder = NULL;
        CHECK(apta_result_builder_create(context, &options, &builder) == 0);
        apta_result_builder_info_t ri; apta_result_builder_info_init(&ri); ri.session_state=APTA_SESSION_ACTIVE; ri.generation=77; ri.lineage_id_high=123; ri.lineage_id_low=456;
        CHECK(apta_result_builder_set_info(builder,&ri)==0);
        apta_result_provenance_t provenance; apta_result_provenance_init(&provenance);
        provenance.origin=APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
        provenance.source_name.data="seed"; provenance.source_name.size=4;
        CHECK(apta_result_builder_set_provenance(builder,&provenance)==0);
        apta_source_info_t info; apta_source_info_init(&info);
        info.sample_rate = 48000; info.channel_count = 1; info.channel_layout = APTA_CHANNEL_LAYOUT_MONO; info.total_frames = 4096;
        CHECK(apta_result_builder_set_source_info(builder, &info) == 0);
        apta_waveform_column_t columns[2] = {{-12000,19000,32000,12,23,34,9}, {-12000,19000,32000,12,23,34,9}};
        apta_waveform_span_t spans[2] = {0};
        for (unsigned i=0; i<2; i++) {
            apta_frame_range_init(&spans[i].source_range);
            spans[i].first_column_index = i == 0 ? 0 : 3;
            spans[i].source_range.first_frame = (uint64_t)spans[i].first_column_index * 1024;
            spans[i].source_range.end_frame = spans[i].source_range.first_frame + 1024;
            spans[i].column_count = 1; spans[i].columns = &columns[i];
        }
        apta_waveform_overview_view_t overview; apta_waveform_overview_view_init(&overview);
        overview.level.frames_per_column=1024; overview.state=APTA_FEATURE_PARTIAL; overview.confidence=42;
        overview.span_count=state.scenario == 17 ? 2 : 1; overview.spans=spans;
        CHECK(apta_result_builder_set_waveform_overview(builder,&overview)==0);
        const apta_result_t *checkpoint=NULL;
        CHECK(apta_result_builder_finalize(builder,&checkpoint)==0);
        CHECK(apta_session_seed_from_result(state.session,checkpoint)==0);
        apta_result_release(checkpoint); apta_result_builder_destroy(builder);
        apta_region_request_t request; apta_region_request_init(&request);
        request.range.first_frame=0; request.range.end_frame=1024;
        request.feature_mask=APTA_FEATURE_WAVEFORM_DETAIL; request.priority=240;
        uint32_t id=0; CHECK(apta_session_request_region(state.session,&request,&id)==0);
        apta_pcm_request_t demand; apta_pcm_request_init(&demand);
        CHECK(apta_session_next_pcm_request(state.session,&demand)==0);
        CHECK(demand.range.first_frame==0);
    }
    const apta_result_t *initial = state.scenario == 6 ? apta_session_acquire_result(state.session) : NULL;
    apta_focus_t focus; apta_focus_init(&focus); focus.feature_mask = APTA_FEATURE_WAVEFORM_DETAIL;
    focus.playhead_frame = 2048; focus.lookahead_frames = 1024; focus.priority = 240;
    if (state.scenario == 0 || state.scenario == 12) CHECK(apta_session_set_focus(state.session, &focus) == 0);
    if (state.scenario == 10) apta_session_request_cancel(state.session);
    if (state.scenario == 11) {
        apta_region_request_t request; apta_region_request_init(&request);
        request.range.first_frame = 2048; request.range.end_frame = 3072;
        request.feature_mask = APTA_FEATURE_WAVEFORM_DETAIL; request.priority = 32;
        uint32_t id = 0;
        CHECK(apta_session_request_region(state.session, &request, &id) == 0);
        request.range.first_frame = 0; request.range.end_frame = 1024; request.priority = 96;
        CHECK(apta_session_request_region(state.session, &request, &id) == 0);
    }
    snapshot(0, 0, &state);
    int terminal = 0;
    for (unsigned step = 1; step <= 20; step++) {
        apta_work_budget_t budget; apta_work_budget_init(&budget);
        budget.maximum_input_frames = state.scenario == 2 ? 4096 : state.scenario == 6 ? 256 : 1024;
        budget.maximum_steps = state.scenario == 2 ? 1 : 4;
        int status = apta_session_process(state.session, &budget, NULL);
        snapshot(step, status, &state);
        if (state.scenario == 0 && step <= 2) { focus.playhead_frame = 0; if (step == 2) focus.feature_mask = 0; CHECK(apta_session_set_focus(state.session, &focus) == 0); }
        if (state.scenario == 6 && step == 2) { CHECK(status == APTA_ERROR_RESULT_SLOTS_EXHAUSTED); apta_result_release(initial); initial = NULL; }
        if ((status < 0 && status != APTA_ERROR_RESULT_SLOTS_EXHAUSTED) || status == APTA_STATUS_END_OF_INPUT) {
            terminal = 1;
            if (state.scenario == 14 || state.scenario == 15) apta_session_request_cancel(state.session);
            snapshot(step + 100, apta_session_process(state.session, &budget, NULL), &state);
            break;
        }
    }
    CHECK(terminal);
    if (initial) apta_result_release(initial);
    CHECK(apta_session_destroy(state.session) == 0); free(workspace); CHECK(apta_context_destroy(context) == 0);
    return 0;
}
