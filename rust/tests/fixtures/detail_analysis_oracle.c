// SPDX-License-Identifier: Apache-2.0
// Compare the unchanged C detail kernel directly. Scheduler/publication lifecycle
// is intentionally outside this fixture; public integration has separate traces.
#include "apta_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed line %d\n", __LINE__); exit(2); } } while (0)
static void snapshot(apta_session_t *s) {
    CHECK(apta_internal_publish_result(s, APTA_FEATURE_WAVEFORM_DETAIL) == 0);
    const apta_result_t *r = apta_session_acquire_result(s);
    CHECK(r);
    printf("R %" PRIu64 " %u\n", s->detail_mutation_serial, r->detail_tile_count);
    for (unsigned i = 0; i < r->detail_tile_count; i++) {
        const apta_waveform_tile_view_t *t = &r->detail_tiles[i];
        printf("T %u %u %" PRIu64 " %" PRIu64 " %u %u %u %u\n", t->level_id, t->tile_index,
            t->source_range.first_frame, t->source_range.end_frame, t->first_column_index,
            t->column_count, t->state, t->confidence);
        for (unsigned j = 0; j < t->column_count; j++) {
            const apta_waveform_column_t *c = &t->columns[j];
            printf("C %d %d %u %u %u %u %u\n", c->minimum, c->maximum, c->rms, c->low, c->mid, c->high, c->flags);
        }
    }
    apta_result_release(r);
}
int main(void) {
    apta_context_config_t cc; apta_context_config_init(&cc);
    cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL;
    apta_context_t *context = NULL; CHECK(apta_context_create(&cc, &context) == 0);
    apta_session_config_t sc; apta_session_config_init(&sc);
    sc.source_sample_rate = 48000; sc.channel_count = 1; sc.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    sc.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    sc.requested_features = cc.requested_capabilities;
    apta_session_t *s = NULL; CHECK(apta_session_create(context, &sc, &s) == 0);
    char op; uint64_t a, b;
    while (scanf(" %c %" SCNu64 " %" SCNu64, &op, &a, &b) == 3) {
        switch (op) {
        case 'P': {
            CHECK(a <= UINT64_MAX - b);
            int status = 0; uint64_t count = 0;
            for (; count < b; count++) {
                uint64_t frame = a + count;
                int16_t raw = (int16_t)((int32_t)((frame * 73) % 65536) - 32768);
                float value = raw < 0 ? (float)raw / 32768.0f : raw == 0 ? 0.0f : (float)raw / 32767.0f;
                status = apta_internal_detail_process_sample(s, frame, value);
                if (status != 0) break;
            }
            printf("P %d %" PRIu64 "\n", status, count);
            break;
        }
        case 'R':
            s->end_of_input_signalled = a != UINT64_MAX;
            s->final_end_frame = a;
            apta_internal_detail_refresh_completed(s);
            snapshot(s);
            break;
        case 'T': {
            memset(s->requests, 0, sizeof(s->requests));
            unsigned slot = 0;
            for (unsigned i = 0; i < 64; i++) if ((a & (UINT64_C(1) << i)) != 0) {
                CHECK(slot < APTA_INTERNAL_MAX_REGION_REQUESTS);
                apta_internal_request_t *r = &s->requests[slot++];
                r->request_id = slot; r->state = APTA_REQUEST_QUEUED;
                r->request.feature_mask = APTA_FEATURE_WAVEFORM_DETAIL;
                r->request.range.first_frame = (uint64_t)i * APTA_INTERNAL_DETAIL_TILE_FRAMES;
                r->request.range.end_frame = r->request.range.first_frame + APTA_INTERNAL_DETAIL_TILE_FRAMES;
            }
            break;
        }
        case 'Q':
            printf("Q %d %d\n", apta_internal_detail_range_complete(s, a, b), apta_internal_detail_range_has_output(s, a, b));
            break;
        default: CHECK(0);
        }
    }
    CHECK(feof(stdin)); CHECK(apta_session_destroy(s) == 0); CHECK(apta_context_destroy(context) == 0);
    return 0;
}
