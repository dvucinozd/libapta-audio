// SPDX-License-Identifier: Apache-2.0
// Public C detail scheduler commands; result tile storage is inspected only
// to dump the immutable published snapshot.
#include "apta_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) \
    do { \
        if (!(x)) { \
            fprintf(stderr, "failed line %d\n", __LINE__); \
            return 2; \
        } \
    } while (0)

int main(void) {
    apta_context_config_t cc;
    apta_context_config_init(&cc);
    cc.requested_capabilities  =  APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL;
    apta_context_t *context  =  NULL;
    CHECK(apta_context_create(&cc, &context) == 0);
    apta_session_config_t sc;
    apta_session_config_init(&sc);
    sc.source_sample_rate = 48000;
    sc.channel_count = 1;
    sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    sc.total_frames = 98305;
    sc.overview_frames_per_column = 256;
    sc.requested_features = cc.requested_capabilities;
    sc.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    void *workspace = aligned_alloc(64, 1048576);
    CHECK(workspace);
    sc.static_workspace = workspace; sc.static_workspace_size = 1048576;
    apta_session_t *session = NULL;
    CHECK(apta_session_create(context, &sc, &session) == 0);
    const apta_result_t *retained[2] = { NULL, NULL };
    char op;
    uint64_t a, b, c, d, e;
    while (scanf(" %c %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64, &op, &a, &b, &c, &d, &e) == 6) {
        int status = 0;
        uint64_t out[8] = {0};
        switch (op) {
            case 'M':
            case 'A': {
                apta_region_request_t r;
                apta_region_request_init(&r);
                r.range.first_frame = a;
                r.range.end_frame = b;
                r.priority = (uint8_t)c;
                r.soft_deadline_monotonic_ns = d;
                r.request_id = (uint32_t)e;
                r.feature_mask = op == 'M' ? APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL : APTA_FEATURE_WAVEFORM_DETAIL;
                uint32_t id = 0;
                status = apta_session_request_region(session, &r, &id);
                out[0] = id;
                break;
            }
            case 'C':
                status  =  apta_session_cancel_region_request(session, (uint32_t)a);
                break;
            case 'F': {
                apta_focus_t f;
                apta_focus_init(&f);
                f.playhead_frame = a;
                f.lookbehind_frames = b;
                f.lookahead_frames = c;
                f.priority = (uint8_t)d;
                f.feature_mask = e;
                status = apta_session_set_focus(session, &f);
                break;
            }
            case 'D': {
                apta_pcm_request_t r;
                apta_pcm_request_init(&r);
                status = apta_session_next_pcm_request(session, &r);
                out[0] = r.range.first_frame;
                out[1] = r.range.end_frame;
                out[2] = r.priority;
                out[3] = r.request_token;
                out[4] = r.feature_mask;
                break;
            }
            case 'R': {
                apta_request_progress_t r;
                apta_request_progress_init(&r);
                status = apta_session_get_request_progress(session, (uint32_t)a, &r);
                if (status == 0) {
                    out[0]  =  r.request_id;
                    out[1] = r.state;
                    out[2] = r.requested_range.first_frame;
                    out[3] = r.requested_range.end_frame;
                    out[4] = r.requested_features;
                    out[5] = r.satisfied_features;
                    out[6] = r.progress_permille;
                    out[7] = r.diagnostic_code;
                }
                break;
            }
            case 'P': {
                CHECK(b <= 4096);
                int16_t pcm[4096];
                for (unsigned i = 0;i<b;i++)pcm[i] = (int16_t)c;
                apta_pcm_block_t block;
                apta_pcm_block_init(&block);
                block.first_frame = a;
                block.frame_count = (uint32_t)b;
                block.data = pcm;
                uint32_t accepted = 0;
                status = apta_session_push_pcm(session, &block, &accepted);
                out[0] = accepted;
                break;
            }
            case 'W': {
                apta_work_budget_t budget; apta_work_budget_init(&budget);
                budget.maximum_input_frames=(uint32_t)a; budget.maximum_steps=(uint32_t)b;
                status=apta_session_process(session,&budget,NULL);
                break;
            }
            case 'E': status=apta_session_signal_end_of_input(session,sc.total_frames); break;
            case 'H': CHECK(a<2 && retained[a]==NULL); retained[a]=apta_session_acquire_result(session); CHECK(retained[a]); break;
            case 'L': CHECK(a<2 && retained[a]); apta_result_release(retained[a]); retained[a]=NULL; break;
            case 'Y':
            case 'Z': {
                const apta_result_t *r = op == 'Y' ? retained[a] : apta_session_acquire_result(session); CHECK(r);
                out[0]=r->detail_tile_count;
                apta_result_info_t info; apta_result_info_init(&info); CHECK(apta_result_get_info(r,&info)==0);
                out[1]=info.session_state; out[2]=info.available_features; out[3]=info.changed_features; out[4]=info.generation;
                for(unsigned i=0;i<r->detail_tile_count;i++) {
                    const apta_waveform_tile_view_t *t=&r->detail_tiles[i];
                    printf("T %u %u %" PRIu64 " %" PRIu64 " %u %u %u %u\n",t->level_id,t->tile_index,t->source_range.first_frame,t->source_range.end_frame,t->first_column_index,t->column_count,t->state,t->confidence);
                    for(unsigned j=0;j<t->column_count;j++) {
                        const apta_waveform_column_t *v=&t->columns[j];
                        printf("V %d %d %u %u\n",v->minimum,v->maximum,v->rms,v->flags);
                    }
                }
                if(op != 'Y') { apta_result_release(r); }
                break;
            }
            default: CHECK(0);
        }
        printf("%d", status);
        for (unsigned i = 0;i<8;i++)printf(" %" PRIu64, out[i]);
        puts("");
    }
    CHECK(feof(stdin));
    for(unsigned i=0;i<2;i++)if(retained[i])apta_result_release(retained[i]);
    CHECK(apta_session_destroy(session) == 0);
    CHECK(apta_context_destroy(context) == 0);
    free(workspace);
    return 0;
}
