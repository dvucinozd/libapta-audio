// SPDX-License-Identifier: Apache-2.0
// Public C scheduler command trace; no scheduler internals are inspected.
#include <apta/apta.h>
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

int main(int argc, char **argv) {
    uint64_t mask = argc > 1 ? strtoull(argv[1], NULL, 10) : APTA_FEATURE_WAVEFORM_OVERVIEW;
    apta_context_config_t cc;
    apta_context_config_init(&cc);
    cc.requested_capabilities = mask;
    apta_context_t *context  =  NULL;
    CHECK(apta_context_create(&cc, &context) == 0);
    apta_session_config_t sc;
    apta_session_config_init(&sc);
    sc.source_sample_rate = 48000;
    sc.channel_count = 1;
    sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    sc.total_frames = 8192;
    sc.requested_features = mask;
    apta_session_t *session = NULL;
    CHECK(apta_session_create(context, &sc, &session) == 0);
    char op;
    uint64_t a, b, c, d, e;
    while (scanf(" %c %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64, &op, &a, &b, &c, &d, &e) == 6) {
        int status = 0;
        uint64_t out[8] = {0};
        switch (op) {
            case 'A': {
                apta_region_request_t r;
                apta_region_request_init(&r);
                r.range.first_frame = a;
                r.range.end_frame = b;
                r.priority = (uint8_t)c;
                r.soft_deadline_monotonic_ns = d;
                r.request_id = (uint32_t)e;
                r.feature_mask = argc > 2 ? strtoull(argv[2], NULL, 10) : APTA_FEATURE_WAVEFORM_OVERVIEW;
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
                apta_work_budget_t budget;
                apta_work_budget_init(&budget);
                budget.maximum_input_frames = (uint32_t)a;
                budget.maximum_steps = (uint32_t)b;
                status = apta_session_process(session, &budget, NULL);
                const apta_result_t *result = apta_session_acquire_result(session);
                CHECK(result);
                apta_waveform_overview_view_t v;
                apta_waveform_overview_view_init(&v);
                int vs = apta_result_get_waveform_overview(result, 0, &v);
                CHECK(vs == 0 || vs == APTA_STATUS_NOT_AVAILABLE);
                if (vs == 0) {
                    out[0]  =  v.span_count;
                    for (unsigned i = 0;i<v.span_count;i++) {
                        CHECK(v.spans[i].first_column_index+v.spans[i].column_count <= 8);
                        for (unsigned j = 0;j<v.spans[i].column_count;j++)out[1] |= UINT64_C(1) << (v.spans[i].first_column_index+j);
                    }
                }
                apta_result_release(result);
                break;
            }
            default: CHECK(0);
        }
        printf("%d", status);
        for (unsigned i = 0;i<8;i++)printf(" %" PRIu64, out[i]);
        puts("");
    }
    CHECK(feof(stdin));
    CHECK(apta_session_destroy(session) == 0);
    CHECK(apta_context_destroy(context) == 0);
    return 0;
}
