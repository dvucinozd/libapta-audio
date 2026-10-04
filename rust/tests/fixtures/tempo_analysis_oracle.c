// SPDX-License-Identifier: Apache-2.0
// Public mutations; unchanged C library produces the reference container.
#include <apta/apta.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(2);} } while (0)
int main(int argc,char **argv) {
 CHECK(argc==4 || argc==5);
 unsigned rate=(unsigned)strtoul(argv[1],NULL,10);
 unsigned steps=(unsigned)strtoul(argv[2],NULL,10);
 FILE *file=fopen(argv[3],"rb");CHECK(file!=NULL);CHECK(fseek(file,0,SEEK_END)==0);long length=ftell(file);CHECK(length>=0 && length%4==0);rewind(file);
 unsigned count=(unsigned)(length/4);float *pcm=malloc((size_t)length);CHECK(pcm!=NULL);CHECK(fread(pcm,4,count,file)==count);fclose(file);
 apta_context_config_t cc;apta_context_config_init(&cc);cc.requested_capabilities=APTA_FEATURE_WAVEFORM_OVERVIEW|APTA_FEATURE_BPM|APTA_FEATURE_LOCAL_BEATGRID;
 if(argc==5 && (argv[4][0]=='g' || argv[4][0]=='r')) cc.requested_capabilities=APTA_FEATURE_WAVEFORM_OVERVIEW|APTA_FEATURE_BPM|APTA_FEATURE_LOCAL_BEATGRID|APTA_FEATURE_GLOBAL_BEATGRID|APTA_FEATURE_DYNAMIC_TEMPO;
 if(argc==5 && argv[4][0]=='k') cc.requested_capabilities=APTA_FEATURE_WAVEFORM_OVERVIEW|APTA_FEATURE_MUSICAL_KEY;
 if(argc==5 && argv[4][0]=='m') cc.requested_capabilities=APTA_FEATURE_WAVEFORM_OVERVIEW|APTA_FEATURE_BPM|APTA_FEATURE_LOCAL_BEATGRID|APTA_FEATURE_METER_DOWNBEAT|APTA_FEATURE_CALIBRATED_QUALITY;
 if(argc==5 && argv[4][0]=='a') cc.requested_capabilities=APTA_FEATURE_WAVEFORM_OVERVIEW|APTA_FEATURE_BPM|APTA_FEATURE_LOCAL_BEATGRID|APTA_FEATURE_GLOBAL_BEATGRID|APTA_FEATURE_DYNAMIC_TEMPO|APTA_FEATURE_MUSICAL_KEY|APTA_FEATURE_METER_DOWNBEAT|APTA_FEATURE_CALIBRATED_QUALITY;
 if(argc==5 && (argv[4][0]=='f' || argv[4][0]=='l' || argv[4][0]=='r')) cc.requested_capabilities|=APTA_FEATURE_GRID_LOCKING;
 apta_context_t *context=NULL;CHECK(apta_context_create(&cc,&context)==0);
 apta_session_config_t config;apta_session_config_init(&config);config.requested_features=cc.requested_capabilities;config.source_sample_rate=rate;config.channel_count=1;config.channel_layout=APTA_CHANNEL_LAYOUT_MONO;config.sample_format=APTA_SAMPLE_F32_NATIVE_INTERLEAVED;config.total_frames=count;config.overview_frames_per_column=32768;
 apta_session_t *session=NULL;CHECK(apta_session_create(context,&config,&session)==0);
 if(argc==5 && argv[4][0]=='f') {apta_focus_t focus;apta_focus_init(&focus);focus.feature_mask=APTA_FEATURE_BPM|APTA_FEATURE_LOCAL_BEATGRID|APTA_FEATURE_GRID_LOCKING;focus.playhead_frame=count/2;focus.lookbehind_frames=count/4;focus.lookahead_frames=count/8;CHECK(apta_session_set_focus(session,&focus)==0);}
 apta_work_budget_t budget;apta_work_budget_init(&budget);budget.maximum_steps=steps;
 unsigned locked=0;
 for(unsigned first=0;first<count;) {unsigned n=count-first;if(n>4096)n=4096;apta_pcm_block_t block;apta_pcm_block_init(&block);block.first_frame=first;block.frame_count=n;block.data=pcm+first;unsigned accepted=0;CHECK(apta_session_push_pcm(session,&block,&accepted)==0);CHECK(accepted==n);first+=n;CHECK(apta_session_process(session,&budget,NULL)>=0);
 if(argc==5 && argv[4][0]=='r' && !locked && first>=count/2) {apta_frame_range_t range;apta_frame_range_init(&range);range.first_frame=0;range.end_frame=count/2-8192;CHECK(apta_session_lock_grid_range(session,&range)==0);locked=1;}}

 CHECK(apta_session_signal_end_of_input(session,count)==0);
 int status=0;for(unsigned i=0;i<100000 && status!=APTA_STATUS_END_OF_INPUT;i++) {status=apta_session_process(session,&budget,NULL);CHECK(status>=0);}CHECK(status==APTA_STATUS_END_OF_INPUT);
 if(argc==5 && argv[4][0]=='l') {apta_frame_range_t range;apta_frame_range_init(&range);range.first_frame=count/4;range.end_frame=count*3/4;CHECK(apta_session_lock_grid_range(session,&range)==0);CHECK(apta_session_lock_grid_range(session,&range)==0);}
 if(argc==5 && argv[4][0]=='r') {const apta_result_t *pending=apta_session_acquire_result(session);CHECK(pending!=NULL);apta_grid_revision_view_t revision;apta_grid_revision_view_init(&revision);CHECK(apta_result_get_grid_revision(pending,&revision)==0);CHECK(revision.state==APTA_GRID_REVISION_PENDING);apta_result_release(pending);CHECK(apta_session_apply_grid_revision(session,revision.revision_id+1)==APTA_ERROR_CONFLICT);CHECK(apta_session_apply_grid_revision(session,revision.revision_id)==0);CHECK(apta_session_apply_grid_revision(session,revision.revision_id)==APTA_ERROR_INVALID_STATE);}
 const apta_result_t *result=apta_session_acquire_result(session);CHECK(result!=NULL);uint64_t size=0;CHECK(apta_result_query_serialized_size(result,NULL,&size)==0);void *out=malloc((size_t)size);CHECK(out!=NULL);size_t written=0;CHECK(apta_result_serialize(result,NULL,out,(size_t)size,&written)==0);CHECK(fwrite(out,1,written,stdout)==written);
 free(out);apta_result_release(result);CHECK(apta_session_destroy(session)==0);CHECK(apta_context_destroy(context)==0);free(pcm);return 0;
}
