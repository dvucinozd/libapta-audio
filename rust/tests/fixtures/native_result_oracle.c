// SPDX-License-Identifier: Apache-2.0
// Test-only native-import scenarios using the unchanged C public builder.
#include <apta/apta.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TRY(call) do { status=(call); if(status<0) goto report; } while(0)
static void range(apta_frame_range_t *r,uint64_t a,uint64_t b){apta_frame_range_init(r);r->first_frame=a;r->end_frame=b;}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    unsigned scenario=(unsigned)strtoul(argv[1],NULL,10);if(scenario>27)return 2;
    apta_context_config_t cc;apta_context_config_init(&cc);apta_context_t *context=NULL;
    if(apta_context_create(&cc,&context)!=APTA_STATUS_OK)return 2;
    apta_result_builder_t *builder=NULL;const apta_result_t *result=NULL;apta_status_t status=0;
    apta_result_info_t output_info;apta_result_info_init(&output_info);
    uint32_t candidate_count=0,coverage_count=0;
    TRY(apta_result_builder_create(context,NULL,&builder));
    apta_result_builder_info_t info;apta_result_builder_info_init(&info);info.generation=42;info.container_version=1;
    info.lineage_id_high=123;info.lineage_id_low=456;
    if(scenario==3)info.session_state=APTA_SESSION_CREATED;
    if(scenario==5||scenario==20)info.session_state=APTA_SESSION_ACTIVE;
    if(scenario==6)info.session_state=APTA_SESSION_CANCELLED;
    if(scenario==7)info.session_state=APTA_SESSION_FAILED;
    if(scenario==15)info.container_version=0;
    if(scenario==16)info.generation=0;
    TRY(apta_result_builder_set_info(builder,&info));
    apta_source_info_t source;apta_source_info_init(&source);source.sample_rate=scenario==12?1:48000;source.channel_count=2;source.channel_layout=APTA_CHANNEL_LAYOUT_STEREO;source.total_frames=scenario==27?APTA_TOTAL_FRAMES_UNKNOWN:100000;
    TRY(apta_result_builder_set_source_info(builder,&source));
    apta_result_provenance_t provenance;apta_result_provenance_init(&provenance);provenance.origin=scenario==2?APTA_RESULT_PROVENANCE_NATIVE_ANALYSIS:APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;provenance.source_name.data="oracle";provenance.source_name.size=scenario==17?0:6;provenance.source_version.data="1";provenance.source_version.size=1;
    TRY(apta_result_builder_set_provenance(builder,&provenance));
    apta_tempo_view_t tempo;apta_tempo_view_init(&tempo);tempo.selected.tempo_millibpm=120000;tempo.selected.confidence=255;tempo.selected.state=(scenario>=4&&scenario<=7)?APTA_FEATURE_PARTIAL:APTA_FEATURE_FINAL;range(&tempo.selected.evidence_range,0,100000);range(&tempo.selected.applicability_range,0,100000);
    apta_tempo_candidate_t candidates[2];memset(candidates,0,sizeof(candidates));candidates[0].tempo_millibpm=120000;candidates[0].score=50000;candidates[0].confidence=80;candidates[1]=candidates[0];candidates[1].score=40000;
    if(scenario==1||scenario==23||scenario==24){tempo.candidates=candidates;tempo.candidate_count=scenario==24?2:1;if(scenario==1)candidates[0].confidence=255;if(scenario==23)candidates[0].tempo_millibpm=60000;}
    if(scenario!=13&&scenario!=14&&scenario!=25&&scenario!=26)TRY(apta_result_builder_set_tempo(builder,&tempo));
    if(scenario==13||scenario==14){apta_key_view_t key;apta_key_view_init(&key);key.state=APTA_FEATURE_FINAL;key.confidence=scenario==13?80:255;key.tonic=0;key.mode=APTA_KEY_MODE_MAJOR;range(&key.applicability_range,0,100000);TRY(apta_result_builder_set_key(builder,&key));}
    if((scenario>=8&&scenario<=12)||(scenario>=18&&scenario<=22)){
        apta_grid_view_t grid;apta_grid_view_init(&grid);range(&grid.requested_range,0,100000);range(&grid.evidence_range,0,100000);range(&grid.applicability_range,0,100000);grid.state=scenario==20?APTA_FEATURE_STABLE:APTA_FEATURE_FINAL;grid.confidence=255;
        apta_frame_range_t coverage[2];range(&coverage[0],0,24000);range(&coverage[1],48000,72000);grid.coverage_ranges=coverage;grid.coverage_range_count=2;
        apta_grid_segment_t segment;memset(&segment,0,sizeof(segment));segment.struct_size=sizeof(segment);segment.api_version=APTA_API_VERSION;range(&segment.applicability_range,0,100000);segment.frames_per_beat.whole_frames=scenario==12?0:24000;segment.frames_per_beat.fraction_q32=scenario==12?UINT32_C(0x80000000):0;segment.nominal_tempo_millibpm=120000;segment.segment_id=1;segment.revision=7;segment.state=scenario==21?APTA_FEATURE_PARTIAL:grid.state;segment.confidence=255;
        apta_beat_t beats[2];memset(beats,0,sizeof(beats));beats[0].position.whole_frame=0;beats[0].ordinal=0;beats[0].confidence=255;beats[1]=beats[0];beats[1].position.whole_frame=scenario==10?23000:24000;beats[1].ordinal=1;
        if(scenario>=8&&scenario<=10){grid.representation=APTA_GRID_REPRESENTATION_EXPLICIT;grid.beats=beats;grid.beat_count=scenario==8?1:2;}else{grid.representation=APTA_GRID_REPRESENTATION_SEGMENTS;grid.segments=&segment;grid.segment_count=1;}
        if(scenario==22)grid.flags=APTA_GRID_FLAG_LOCKED;
        int global=scenario>=18&&scenario<=20;
        TRY(apta_result_builder_set_beatgrid(builder,global?APTA_FEATURE_GLOBAL_BEATGRID:APTA_FEATURE_LOCAL_BEATGRID,&grid));
        if(global){apta_grid_revision_view_t revision;apta_grid_revision_view_init(&revision);revision.state=scenario==18?APTA_GRID_REVISION_APPLIED:APTA_GRID_REVISION_PENDING;revision.confidence=255;revision.revision_id=7;revision.previous_revision_id=6;revision.proposed_representation=APTA_GRID_REPRESENTATION_SEGMENTS;revision.proposed_segment_count=1;range(&revision.affected_range,0,100000);TRY(apta_result_builder_set_grid_revision(builder,&revision));}
    }
    if(scenario==25){apta_quality_view_t quality;apta_quality_view_init(&quality);quality.feature=APTA_FEATURE_BPM;quality.state=APTA_FEATURE_FINAL;quality.confidence=255;quality.evidence_coverage_permille=65535;TRY(apta_result_builder_set_quality(builder,&quality));}
    TRY(apta_result_builder_finalize(builder,&result));
    TRY(apta_result_get_info(result,&output_info));
    {apta_tempo_view_t value;apta_tempo_view_init(&value);if(apta_result_get_tempo(result,NULL,&value)==APTA_STATUS_OK)candidate_count=value.candidate_count;apta_grid_view_t grid;apta_grid_view_init(&grid);if(apta_result_get_beatgrid(result,APTA_FEATURE_LOCAL_BEATGRID,NULL,&grid)==APTA_STATUS_OK)coverage_count=grid.coverage_range_count;}
report:
    printf("%d %" PRIu64 " %" PRIu64 " %u %u %u %u\n",status,output_info.available_features,output_info.generation,output_info.session_state,candidate_count,coverage_count,output_info.container_version);
    apta_result_release(result);apta_result_builder_destroy(builder);if(apta_context_destroy(context)!=APTA_STATUS_OK)return 2;return 0;
}
