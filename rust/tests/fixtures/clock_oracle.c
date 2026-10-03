// SPDX-License-Identifier: Apache-2.0
#include <apta/apta.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    unsigned scenario,calls,reads,releases;
    uint64_t events;
    apta_session_t *session;
    int16_t samples[1024];
} state_t;
static uint64_t tick(void *user) {
    state_t *s=user;
    s->calls++;
    s->events=s->events*10u+3u;
    if(s->scenario==1u)return 0;
    if(s->scenario==3u)return s->calls==1u?UINT64_MAX-500u:UINT64_MAX;
    return (uint64_t)s->calls*1000u;
}
static apta_status_t read_pcm(void *user,uint64_t first,uint32_t maximum,apta_pcm_block_t *out){
    state_t *s=user;
    s->reads++;
    s->events=s->events*10u+1u;
    if(s->scenario==9u)return APTA_ERROR_SOURCE;
    if(s->scenario==10u)return APTA_STATUS_WOULD_BLOCK;
    if(s->scenario==11u)apta_session_request_cancel(s->session);
    apta_pcm_block_init(out);
    out->first_frame=first;
    out->frame_count=maximum;
    out->data=s->samples;
    return APTA_STATUS_OK;
}
static void release_pcm(void *user,apta_pcm_block_t *block){
    state_t *s=user;
    (void)block;
    s->releases++;
    s->events=s->events*10u+2u;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    state_t state;
    memset(&state,0,sizeof(state));
    state.scenario=(unsigned)strtoul(argv[1],NULL,10);
    apta_context_config_t cc;
    apta_context_config_init(&cc);
    cc.requested_capabilities=APTA_FEATURE_WAVEFORM_OVERVIEW;
    cc.clock.user_data=&state;
    cc.clock.monotonic_time_ns=tick;
    apta_context_t *context=NULL;
    if(apta_context_create(&cc,&context)!=0)return 3;
    apta_session_config_t config;
    apta_session_config_init(&config);
    config.source_sample_rate=48000;
    config.channel_count=1;
    config.channel_layout=APTA_CHANNEL_LAYOUT_MONO;
    config.sample_format=APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.total_frames=1024;
    config.overview_frames_per_column=64;
    config.requested_features=APTA_FEATURE_WAVEFORM_OVERVIEW;
    int pull=state.scenario==6u||state.scenario>=9u;
    if(pull)config.input_mode=APTA_INPUT_MODE_PULL;
    if(apta_session_create(context,&config,&state.session)!=0)return 4;
    if(pull){
        apta_pcm_source_t source;
        apta_pcm_source_init(&source);
        source.user_data=&state;
        source.read_frames=read_pcm;
        source.release_frames=release_pcm;
        if(apta_session_set_source(state.session,&source)!=0)return 5;
    }
    else {
        apta_pcm_block_t block;
        apta_pcm_block_init(&block);
        block.data=state.samples;
        block.frame_count=1024;
        uint32_t accepted=0;
        if(apta_session_push_pcm(state.session,&block,&accepted)<0||accepted!=1024)return 6;
    }
    apta_work_budget_t budget;
    apta_work_budget_init(&budget);
    budget.soft_time_budget_us=state.scenario==2u?0u:1u;
    if(state.scenario==4u){
        budget.maximum_input_frames=17;
        budget.soft_time_budget_us=100;
    }
    if(state.scenario==5u){
        budget.maximum_steps=1;
        budget.soft_time_budget_us=100;
    }
    if(state.scenario==7u)apta_session_request_cancel(state.session);
    if(state.scenario==8u){
        apta_work_budget_t unlimited;
        apta_work_budget_init(&unlimited);
        if(apta_session_signal_end_of_input(state.session,1024)!=0)return 7;
        if(apta_session_process(state.session,&unlimited,NULL)<0)return 8;
    }
    apta_progress_t progress;
    apta_progress_init(&progress);
    apta_status_t status=apta_session_process(state.session,&budget,&progress);
    printf("%d %u %u %u %" PRIu64 " %u %u %u\n",status,progress.consumed_input_frames,progress.completed_steps,state.calls,state.events,(unsigned)apta_session_get_state(state.session),state.reads,state.releases);
    if(apta_session_destroy(state.session)!=0||apta_context_destroy(context)!=0)return 9;
    return 0;
}
