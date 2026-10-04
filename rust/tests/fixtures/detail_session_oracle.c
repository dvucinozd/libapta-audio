// SPDX-License-Identifier: Apache-2.0
// Public C eager acceptance, bounded process and EOF detail publication.
#include "apta_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed line %d\n", __LINE__); exit(2); } } while (0)
static void snapshot(apta_session_t *s) {
    /* Observe snapshots published by public session processing only. */
    const apta_result_t *r = apta_session_acquire_result(s);
    CHECK(r);
    printf("R %u\n", r->detail_tile_count);
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
int main(int argc, char **argv) {
    int sequential = argc == 2;
    int unknown = sequential && strcmp(argv[1], "unknown") == 0;
    apta_context_config_t cc; apta_context_config_init(&cc);
    cc.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL;
    apta_context_t *context = NULL; CHECK(apta_context_create(&cc, &context) == 0);
    apta_session_config_t sc; apta_session_config_init(&sc);
    sc.source_sample_rate=48000; sc.channel_count=1; sc.channel_layout=APTA_CHANNEL_LAYOUT_MONO;
    sc.sample_format=APTA_SAMPLE_S16_NATIVE_INTERLEAVED; sc.total_frames=unknown ? APTA_TOTAL_FRAMES_UNKNOWN : 513;
    sc.overview_frames_per_column=64; sc.requested_features=cc.requested_capabilities;
    apta_session_t *s=NULL; CHECK(apta_session_create(context,&sc,&s)==0);
    int16_t pcm[513]; for(unsigned i=0;i<513;i++) pcm[i]=123;
    apta_pcm_block_t block; apta_pcm_block_init(&block); block.data=pcm;
    block.first_frame=sequential ? 0 : 256; block.frame_count=sequential ? 513 : 257;
    uint32_t accepted=0; CHECK(apta_session_push_pcm(s,&block,&accepted)>=0); CHECK(accepted==block.frame_count);
    apta_work_budget_t budget; apta_work_budget_init(&budget); budget.maximum_input_frames=1; budget.maximum_steps=1;
    apta_progress_t progress; apta_progress_init(&progress);
    CHECK(apta_session_process(s,&budget,&progress)>=0); snapshot(s);
    CHECK(apta_session_signal_end_of_input(s,513)>=0);
    apta_work_budget_init(&budget); CHECK(apta_session_process(s,&budget,&progress)>=0); snapshot(s);
    CHECK(apta_session_destroy(s)==0); CHECK(apta_context_destroy(context)==0); return 0;
}
