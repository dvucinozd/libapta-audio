// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void set_range(
    apta_frame_range_t *range,
    apta_source_frame_t first,
    apta_source_frame_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_result_builder_info_t builder_info;
    apta_result_provenance_t provenance;
    apta_source_info_t source;
    apta_metadata_t metadata;
    apta_waveform_column_t overview_columns[2] = {
        {-100, 200, 80u, 20u, 30u, 40u,
         APTA_WAVEFORM_COLUMN_VALID | APTA_WAVEFORM_COLUMN_HAS_3BAND},
        {-90, 180, 70u, 18u, 28u, 38u,
         APTA_WAVEFORM_COLUMN_VALID | APTA_WAVEFORM_COLUMN_HAS_3BAND}};
    apta_waveform_span_t overview_span;
    apta_waveform_overview_view_t overview;
    apta_waveform_column_t detail_columns[2] = {
        {-30, 40, 20u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID},
        {-25, 35, 18u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID}};
    apta_waveform_tile_view_t detail_tile;
    apta_waveform_detail_input_t detail;
    apta_tempo_candidate_t tempo_candidate;
    apta_tempo_view_t tempo;
    apta_frame_range_t local_coverage;
    apta_grid_segment_t local_segment;
    apta_grid_view_t local_grid;
    apta_frame_range_t global_coverage;
    apta_grid_segment_t global_segments[2];
    apta_beat_t global_beats[3];
    apta_grid_view_t global_grid;
    apta_key_candidate_t key_candidate;
    apta_key_view_t key;
    apta_meter_segment_t meter_segments[2];
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    const apta_result_t *result = NULL;
    apta_result_info_t result_info;
    apta_result_provenance_t result_provenance;
    apta_metadata_view_t result_metadata;
    apta_waveform_overview_view_t result_overview;
    apta_waveform_tile_view_t result_tile;
    apta_tempo_view_t result_tempo;
    apta_grid_view_t result_grid;
    apta_key_view_t result_key;
    apta_meter_view_t result_meter;
    apta_quality_view_t result_quality;
    char producer[] = "Rekordbox";
    char comments[] = "USB export import";
    char source_name[] = "rekordbox.xml";
    char source_version[] = "6.8.5";

    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_result_builder_options_init(&options);
    CHECK(options.maximum_allocation_bytes != 0u);
    CHECK(apta_result_builder_create(context, &options, &builder) ==
          APTA_STATUS_OK);

    apta_result_builder_info_init(&builder_info);
    builder_info.generation = 77u;
    builder_info.container_version = 1u;
    builder_info.lineage_id_high = UINT64_C(0x0102030405060708);
    builder_info.lineage_id_low = UINT64_C(0x1112131415161718);
    CHECK(apta_result_builder_set_info(builder, &builder_info) ==
          APTA_STATUS_OK);

    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
    provenance.source_name.data = source_name;
    provenance.source_name.size = (uint32_t)strlen(source_name);
    provenance.source_version.data = source_version;
    provenance.source_version.size = (uint32_t)strlen(source_version);
    CHECK(apta_result_builder_set_provenance(builder, &provenance) ==
          APTA_STATUS_OK);

    apta_source_info_init(&source);
    source.total_frames = 2880000u;
    source.sample_rate = 48000u;
    source.channel_count = 2u;
    source.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    source.fingerprint_kind = APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256;
    source.fingerprint[0] = 0xA5u;
    CHECK(apta_result_builder_set_source_info(builder, &source) ==
          APTA_STATUS_OK);

    apta_metadata_init(&metadata);
    metadata.producer_name.data = producer;
    metadata.producer_name.size = (uint32_t)strlen(producer);
    metadata.comments.data = comments;
    metadata.comments.size = (uint32_t)strlen(comments);
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
                     APTA_METADATA_FLAG_COMMENTS_PRESENT;
    CHECK(apta_result_builder_set_metadata(builder, &metadata) ==
          APTA_STATUS_OK);

    apta_waveform_overview_view_init(&overview);
    overview.level.level_id = 0u;
    overview.level.frames_per_column = 1440000u;
    overview.level.origin_frame = 0u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 96u;
    memset(&overview_span, 0, sizeof(overview_span));
    set_range(&overview_span.source_range, 0u, 2880000u);
    overview_span.column_count = 2u;
    overview_span.columns = overview_columns;
    overview.span_count = 1u;
    overview.spans = &overview_span;
    CHECK(apta_result_builder_set_waveform_overview(builder, &overview) ==
          APTA_STATUS_OK);

    apta_waveform_tile_view_init(&detail_tile);
    detail_tile.level_id = 1u;
    detail_tile.tile_index = 4u;
    set_range(&detail_tile.source_range, 65536u, 131072u);
    detail_tile.first_column_index = 256u;
    detail_tile.column_count = 2u;
    detail_tile.columns = detail_columns;
    detail_tile.state = APTA_FEATURE_FINAL;
    detail_tile.confidence = 91u;
    apta_waveform_detail_input_init(&detail);
    detail.tile_count = 1u;
    detail.tiles = &detail_tile;
    CHECK(apta_result_builder_set_waveform_detail(builder, &detail) ==
          APTA_STATUS_OK);

    apta_tempo_view_init(&tempo);
    set_range(&tempo.selected.evidence_range, 0u, 2880000u);
    set_range(&tempo.selected.applicability_range, 0u, 2880000u);
    tempo.selected.tempo_millibpm = 128000u;
    tempo.selected.confidence = 94u;
    tempo.selected.state = APTA_FEATURE_FINAL;
    memset(&tempo_candidate, 0, sizeof(tempo_candidate));
    tempo_candidate.tempo_millibpm = 128000u;
    tempo_candidate.score = 65000u;
    tempo_candidate.confidence = 94u;
    tempo.candidate_count = 1u;
    tempo.candidates = &tempo_candidate;
    CHECK(apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);

    set_range(&local_coverage, 0u, 2880000u);
    memset(&local_segment, 0, sizeof(local_segment));
    local_segment.struct_size = (uint32_t)sizeof(local_segment);
    local_segment.api_version = APTA_API_VERSION;
    set_range(&local_segment.applicability_range, 0u, 2880000u);
    local_segment.frames_per_beat.whole_frames = 22500u;
    local_segment.beat_count = 128u;
    local_segment.nominal_tempo_millibpm = 128000u;
    local_segment.confidence = 93u;
    local_segment.state = APTA_FEATURE_FINAL;
    local_segment.segment_id = 1u;
    apta_grid_view_init(&local_grid);
    set_range(&local_grid.requested_range, 0u, 2880000u);
    set_range(&local_grid.evidence_range, 0u, 2880000u);
    set_range(&local_grid.applicability_range, 0u, 2880000u);
    local_grid.representation = APTA_GRID_REPRESENTATION_SEGMENTS;
    local_grid.state = APTA_FEATURE_FINAL;
    local_grid.confidence = 93u;
    local_grid.coverage_range_count = 1u;
    local_grid.coverage_ranges = &local_coverage;
    local_grid.segment_count = 1u;
    local_grid.segments = &local_segment;
    local_grid.flags = APTA_GRID_FLAG_LOCKED;
    CHECK(apta_result_builder_set_beatgrid(
              builder, APTA_FEATURE_LOCAL_BEATGRID, &local_grid) ==
          APTA_STATUS_OK);

    set_range(&global_coverage, 0u, 2880000u);
    memset(global_segments, 0, sizeof(global_segments));
    global_segments[0] = local_segment;
    set_range(&global_segments[0].applicability_range, 0u, 1440000u);
    global_segments[0].beat_count = 64u;
    global_segments[1] = local_segment;
    set_range(&global_segments[1].applicability_range, 1440000u, 2880000u);
    global_segments[1].beat_count = 64u;
    global_segments[1].segment_id = 2u;
    global_segments[1].flags = APTA_GRID_FLAG_DYNAMIC_TEMPO;
    memset(global_beats, 0, sizeof(global_beats));
    global_beats[0].position.whole_frame = 0u;
    global_beats[0].ordinal = 0;
    global_beats[0].confidence = 90u;
    global_beats[1].position.whole_frame = 22500u;
    global_beats[1].ordinal = 1;
    global_beats[1].confidence = 90u;
    global_beats[2].position.whole_frame = 45000u;
    global_beats[2].ordinal = 2;
    global_beats[2].confidence = 90u;
    apta_grid_view_init(&global_grid);
    set_range(&global_grid.requested_range, 0u, 2880000u);
    set_range(&global_grid.evidence_range, 0u, 2880000u);
    set_range(&global_grid.applicability_range, 0u, 2880000u);
    global_grid.representation = APTA_GRID_REPRESENTATION_HYBRID;
    global_grid.state = APTA_FEATURE_FINAL;
    global_grid.confidence = 90u;
    global_grid.coverage_range_count = 1u;
    global_grid.coverage_ranges = &global_coverage;
    global_grid.segment_count = 2u;
    global_grid.segments = global_segments;
    global_grid.beat_count = 3u;
    global_grid.beats = global_beats;
    global_grid.flags = APTA_GRID_FLAG_DYNAMIC_TEMPO;
    CHECK(apta_result_builder_set_beatgrid(
              builder, APTA_FEATURE_GLOBAL_BEATGRID, &global_grid) ==
          APTA_STATUS_OK);

    apta_key_view_init(&key);
    set_range(&key.applicability_range, 0u, 2880000u);
    key.tonic = 9u;
    key.mode = APTA_KEY_MODE_MINOR;
    key.tuning_offset_cents = -7;
    key.confidence = 88u;
    key.state = APTA_FEATURE_FINAL;
    memset(&key_candidate, 0, sizeof(key_candidate));
    key_candidate.tonic = 9u;
    key_candidate.mode = APTA_KEY_MODE_MINOR;
    key_candidate.tuning_offset_cents = -7;
    key_candidate.score = 62000u;
    key_candidate.confidence = 88u;
    key.candidate_count = 1u;
    key.candidates = &key_candidate;
    CHECK(apta_result_builder_set_key(builder, &key) == APTA_STATUS_OK);

    memset(meter_segments, 0, sizeof(meter_segments));
    meter_segments[0].struct_size = (uint32_t)sizeof(meter_segments[0]);
    meter_segments[0].api_version = APTA_API_VERSION;
    set_range(&meter_segments[0].applicability_range, 0u, 1440000u);
    meter_segments[0].numerator = 4u;
    meter_segments[0].denominator = 4u;
    meter_segments[0].state = APTA_FEATURE_FINAL;
    meter_segments[0].confidence = 86u;
    meter_segments[0].segment_id = 1u;
    meter_segments[1] = meter_segments[0];
    set_range(&meter_segments[1].applicability_range, 1440000u, 2880000u);
    meter_segments[1].downbeat_frame = 1440000u;
    meter_segments[1].downbeat_ordinal = 64;
    meter_segments[1].numerator = 3u;
    meter_segments[1].segment_id = 2u;
    apta_meter_view_init(&meter);
    meter.numerator = 4u;
    meter.denominator = 4u;
    meter.state = APTA_FEATURE_FINAL;
    meter.confidence = 86u;
    meter.segment_count = 2u;
    meter.segments = meter_segments;
    CHECK(apta_result_builder_set_meter(builder, &meter) == APTA_STATUS_OK);

    apta_quality_view_init(&quality);
    quality.feature = APTA_FEATURE_MUSICAL_KEY;
    quality.calibration_model_id = 42u;
    quality.evidence_coverage_permille = 930u;
    quality.confidence = 84u;
    quality.state = APTA_FEATURE_FINAL;
    quality.flags = APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT;
    CHECK(apta_result_builder_set_quality(builder, &quality) == APTA_STATUS_OK);

    /* Setters own inputs immediately. */
    memset(producer, 'X', sizeof(producer) - 1u);
    memset(comments, 'Y', sizeof(comments) - 1u);
    memset(source_name, 'Z', sizeof(source_name) - 1u);
    overview_columns[0].maximum = 1;
    detail_columns[0].maximum = 1;
    tempo_candidate.tempo_millibpm = 64000u;
    global_beats[1].position.whole_frame = 999u;
    key_candidate.tonic = 0u;
    meter_segments[1].numerator = 9u;

    CHECK(apta_result_builder_finalize(builder, &result) == APTA_STATUS_OK);
    CHECK(result != NULL);

    /* Reuse is supported and a finalized result does not alias the builder. */
    apta_result_builder_reset(builder);
    apta_result_builder_destroy(builder);
    builder = NULL;

    apta_result_info_init(&result_info);
    CHECK(apta_result_get_info(result, &result_info) == APTA_STATUS_OK);
    CHECK(result_info.generation == 77u);
    CHECK(result_info.container_version == 1u);
    CHECK(result_info.available_features ==
          (APTA_FEATURE_WAVEFORM_OVERVIEW |
           APTA_FEATURE_WAVEFORM_DETAIL |
           APTA_FEATURE_WAVEFORM_3BAND |
           APTA_FEATURE_BPM |
           APTA_FEATURE_LOCAL_BEATGRID |
           APTA_FEATURE_GLOBAL_BEATGRID |
           APTA_FEATURE_DYNAMIC_TEMPO |
           APTA_FEATURE_CONFIDENCE |
           APTA_FEATURE_GRID_LOCKING |
           APTA_FEATURE_MUSICAL_KEY |
           APTA_FEATURE_METER_DOWNBEAT |
           APTA_FEATURE_CALIBRATED_QUALITY));
    CHECK(result_info.changed_features == result_info.available_features);

    apta_result_provenance_init(&result_provenance);
    CHECK(apta_result_get_provenance(result, &result_provenance) ==
          APTA_STATUS_OK);
    CHECK(result_provenance.origin == APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT);
    CHECK(result_provenance.source_name.size == 13u);
    CHECK(memcmp(result_provenance.source_name.data, "rekordbox.xml", 13u) == 0);

    apta_metadata_view_init(&result_metadata);
    CHECK(apta_result_get_metadata(result, &result_metadata) == APTA_STATUS_OK);
    CHECK(result_metadata.producer_name.size == 9u);
    CHECK(memcmp(result_metadata.producer_name.data, "Rekordbox", 9u) == 0);

    apta_waveform_overview_view_init(&result_overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &result_overview) ==
          APTA_STATUS_OK);
    CHECK(result_overview.spans[0].columns[0].maximum == 200);
    apta_waveform_tile_view_init(&result_tile);
    CHECK(apta_result_get_waveform_tile(result, 1u, 4u, &result_tile) ==
          APTA_STATUS_OK);
    CHECK(result_tile.columns[0].maximum == 40);
    apta_tempo_view_init(&result_tempo);
    CHECK(apta_result_get_tempo(result, NULL, &result_tempo) == APTA_STATUS_OK);
    CHECK(result_tempo.candidates[0].tempo_millibpm == 128000u);
    apta_grid_view_init(&result_grid);
    CHECK(apta_result_get_beatgrid(
              result, APTA_FEATURE_GLOBAL_BEATGRID, NULL, &result_grid) ==
          APTA_STATUS_OK);
    CHECK(result_grid.beats[1].position.whole_frame == 22500u);
    apta_key_view_init(&result_key);
    CHECK(apta_result_get_key(result, NULL, &result_key) == APTA_STATUS_OK);
    CHECK(result_key.candidates[0].tonic == 9u);
    apta_meter_view_init(&result_meter);
    CHECK(apta_result_get_meter(result, NULL, &result_meter) == APTA_STATUS_OK);
    CHECK(result_meter.segments[1].numerator == 3u);
    apta_quality_view_init(&result_quality);
    CHECK(apta_result_get_quality(
              result, APTA_FEATURE_MUSICAL_KEY, &result_quality) ==
          APTA_STATUS_OK);
    CHECK(result_quality.calibration_model_id == 42u);

    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
