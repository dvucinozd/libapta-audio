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

static void set_range(apta_frame_range_t *range, uint64_t first, uint64_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

static int prepare_builder(
    apta_context_t *context,
    const apta_result_builder_options_t *options,
    apta_result_builder_t **builder_out)
{
    apta_result_provenance_t provenance;
    apta_source_info_t source;

    if (apta_result_builder_create(context, options, builder_out) !=
        APTA_STATUS_OK) {
        return 0;
    }
    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
    provenance.source_name.data = "fixture";
    provenance.source_name.size = 7u;
    if (apta_result_builder_set_provenance(*builder_out, &provenance) !=
        APTA_STATUS_OK) {
        return 0;
    }
    apta_source_info_init(&source);
    source.total_frames = 100000u;
    source.sample_rate = 48000u;
    source.channel_count = 2u;
    source.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    return apta_result_builder_set_source_info(*builder_out, &source) ==
           APTA_STATUS_OK;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_result_builder_t *limited_builder = NULL;
    const apta_result_t *result = (const apta_result_t *)(uintptr_t)1u;
    apta_source_info_t source;
    apta_result_provenance_t provenance;
    apta_metadata_t metadata;
    apta_waveform_overview_view_t overview;
    apta_waveform_span_t spans[2];
    apta_waveform_column_t columns[2] = {
        {0, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID},
        {0, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID}};
    apta_tempo_view_t tempo;
    apta_tempo_candidate_t candidate;
    apta_grid_view_t grid;
    apta_frame_range_t coverage;
    apta_grid_segment_t segments[2];
    apta_beat_t beats[2];
    apta_key_view_t key;
    apta_key_candidate_t key_candidate;
    apta_meter_view_t meter;
    apta_meter_segment_t meter_segments[2];
    apta_quality_view_t quality;

    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_result_builder_options_init(&options);
    options.struct_size = 0u;
    CHECK(apta_result_builder_create(context, &options, &builder) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(builder == NULL);
    apta_result_builder_options_init(&options);
    options.flags = 1u;
    CHECK(apta_result_builder_create(context, &options, &builder) ==
          APTA_ERROR_UNSUPPORTED);
    options.flags = 0u;
    CHECK(prepare_builder(context, &options, &builder));

    apta_source_info_init(&source);
    source.sample_rate = 48000u;
    source.channel_count = 2u;
    source.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    source.reserved32[0] = 1u;
    CHECK(apta_result_builder_set_source_info(builder, &source) ==
          APTA_ERROR_INVALID_ARGUMENT);
    source.reserved32[0] = 0u;
    source.channel_count = 1u;
    CHECK(apta_result_builder_set_source_info(builder, &source) ==
          APTA_ERROR_INVALID_ARGUMENT);

    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_NATIVE_ANALYSIS;
    CHECK(apta_result_builder_set_provenance(builder, &provenance) ==
          APTA_ERROR_UNSUPPORTED);

    apta_metadata_init(&metadata);
    metadata.struct_size = 0u;
    CHECK(apta_result_builder_set_metadata(builder, &metadata) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);

    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 100u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 90u;
    memset(spans, 0, sizeof(spans));
    set_range(&spans[0].source_range, 0u, 100u);
    spans[0].column_count = 1u;
    spans[0].columns = &columns[0];
    set_range(&spans[1].source_range, 50u, 150u);
    spans[1].first_column_index = 1u;
    spans[1].column_count = 1u;
    spans[1].columns = &columns[1];
    overview.span_count = 2u;
    overview.spans = spans;
    CHECK(apta_result_builder_set_waveform_overview(builder, &overview) ==
          APTA_ERROR_INVALID_ARGUMENT);
    set_range(&spans[1].source_range, 100u, 200u);
    spans[1].columns = NULL;
    CHECK(apta_result_builder_set_waveform_overview(builder, &overview) ==
          APTA_ERROR_INVALID_ARGUMENT);
    spans[1].columns = &columns[1];
    overview.flags = UINT32_MAX;
    CHECK(apta_result_builder_set_waveform_overview(builder, &overview) ==
          APTA_ERROR_UNSUPPORTED);
    overview.flags = 0u;

    apta_tempo_view_init(&tempo);
    set_range(&tempo.selected.evidence_range, 0u, 100000u);
    set_range(&tempo.selected.applicability_range, 0u, 100000u);
    tempo.selected.tempo_millibpm = 128000u;
    tempo.selected.state = APTA_FEATURE_FINAL;
    tempo.selected.confidence = 90u;
    memset(&candidate, 0, sizeof(candidate));
    candidate.tempo_millibpm = 128000u;
    candidate.score = 60000u;
    candidate.confidence = 90u;
    tempo.candidate_count = 1u;
    tempo.candidates = &candidate;
    candidate.relation_to_selected = 99u;
    CHECK(apta_result_builder_set_tempo(builder, &tempo) ==
          APTA_ERROR_INVALID_ARGUMENT);
    candidate.relation_to_selected = APTA_TEMPO_RELATION_INDEPENDENT;
    tempo.selected.confidence = 101u;
    CHECK(apta_result_builder_set_tempo(builder, &tempo) ==
          APTA_ERROR_INVALID_ARGUMENT);
    tempo.selected.confidence = 90u;
    CHECK(apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);

    set_range(&coverage, 0u, 100000u);
    memset(segments, 0, sizeof(segments));
    segments[0].struct_size = (uint32_t)sizeof(segments[0]);
    segments[0].api_version = APTA_API_VERSION;
    set_range(&segments[0].applicability_range, 0u, 50000u);
    segments[0].frames_per_beat.whole_frames = 22500u;
    segments[0].nominal_tempo_millibpm = 128000u;
    segments[0].state = APTA_FEATURE_FINAL;
    segments[0].confidence = 90u;
    segments[1] = segments[0];
    set_range(&segments[1].applicability_range, 40000u, 100000u);
    segments[1].segment_id = 2u;
    memset(beats, 0, sizeof(beats));
    beats[0].position.whole_frame = 100u;
    beats[0].ordinal = 1;
    beats[0].confidence = 90u;
    beats[1].position.whole_frame = 90u;
    beats[1].ordinal = 2;
    beats[1].confidence = 90u;
    apta_grid_view_init(&grid);
    set_range(&grid.requested_range, 0u, 100000u);
    set_range(&grid.evidence_range, 0u, 100000u);
    set_range(&grid.applicability_range, 0u, 100000u);
    grid.representation = APTA_GRID_REPRESENTATION_HYBRID;
    grid.state = APTA_FEATURE_FINAL;
    grid.confidence = 90u;
    grid.coverage_range_count = 1u;
    grid.coverage_ranges = &coverage;
    grid.segment_count = 2u;
    grid.segments = segments;
    grid.beat_count = 2u;
    grid.beats = beats;
    CHECK(apta_result_builder_set_beatgrid(
              builder,
              APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GLOBAL_BEATGRID,
              &grid) == APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_builder_set_beatgrid(
              builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) ==
          APTA_ERROR_INVALID_ARGUMENT);
    set_range(&segments[1].applicability_range, 50000u, 100000u);
    beats[1].position.whole_frame = 200u;
    beats[1].ordinal = 1;
    CHECK(apta_result_builder_set_beatgrid(
              builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) ==
          APTA_ERROR_INVALID_ARGUMENT);
    beats[1].ordinal = 2;
    grid.representation = 99u;
    CHECK(apta_result_builder_set_beatgrid(
              builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) ==
          APTA_ERROR_UNSUPPORTED);

    apta_key_view_init(&key);
    set_range(&key.applicability_range, 0u, 100000u);
    key.tonic = 12u;
    key.mode = APTA_KEY_MODE_MAJOR;
    key.state = APTA_FEATURE_FINAL;
    key.confidence = 90u;
    memset(&key_candidate, 0, sizeof(key_candidate));
    key_candidate.tonic = 0u;
    key_candidate.mode = APTA_KEY_MODE_MAJOR;
    key_candidate.confidence = 90u;
    key.candidate_count = 1u;
    key.candidates = &key_candidate;
    CHECK(apta_result_builder_set_key(builder, &key) ==
          APTA_ERROR_INVALID_ARGUMENT);
    key.tonic = 0u;
    key.tuning_offset_cents = 101;
    CHECK(apta_result_builder_set_key(builder, &key) ==
          APTA_ERROR_INVALID_ARGUMENT);

    memset(meter_segments, 0, sizeof(meter_segments));
    meter_segments[0].struct_size = (uint32_t)sizeof(meter_segments[0]);
    meter_segments[0].api_version = APTA_API_VERSION;
    set_range(&meter_segments[0].applicability_range, 0u, 50000u);
    meter_segments[0].numerator = 4u;
    meter_segments[0].denominator = 3u;
    meter_segments[0].state = APTA_FEATURE_FINAL;
    meter_segments[0].confidence = 90u;
    apta_meter_view_init(&meter);
    meter.numerator = 4u;
    meter.denominator = 4u;
    meter.state = APTA_FEATURE_FINAL;
    meter.confidence = 90u;
    meter.segment_count = 1u;
    meter.segments = meter_segments;
    CHECK(apta_result_builder_set_meter(builder, &meter) ==
          APTA_ERROR_INVALID_ARGUMENT);

    apta_quality_view_init(&quality);
    quality.feature = APTA_FEATURE_BPM | APTA_FEATURE_MUSICAL_KEY;
    quality.evidence_coverage_permille = 900u;
    quality.confidence = 90u;
    quality.state = APTA_FEATURE_FINAL;
    CHECK(apta_result_builder_set_quality(builder, &quality) ==
          APTA_ERROR_INVALID_ARGUMENT);
    quality.feature = APTA_FEATURE_MUSICAL_KEY;
    CHECK(apta_result_builder_set_quality(builder, &quality) ==
          APTA_STATUS_OK);
    CHECK(apta_result_builder_set_quality(builder, &quality) ==
          APTA_ERROR_CONFLICT);

    result = (const apta_result_t *)(uintptr_t)1u;
    CHECK(apta_result_builder_finalize(builder, &result) ==
          APTA_ERROR_CONFLICT);
    CHECK(result == NULL);

    apta_result_builder_options_init(&options);
    options.maximum_tempo_candidates = 1u;
    CHECK(prepare_builder(context, &options, &limited_builder));
    tempo.candidate_count = 2u;
    CHECK(apta_result_builder_set_tempo(limited_builder, &tempo) ==
          APTA_ERROR_LIMIT_EXCEEDED);

    apta_result_builder_destroy(limited_builder);
    limited_builder = NULL;
    apta_result_builder_options_init(&options);
    options.maximum_tempo_candidates = UINT32_MAX;
    CHECK(prepare_builder(context, &options, &limited_builder));
    tempo.candidate_count = UINT32_MAX;
    CHECK(apta_result_builder_set_tempo(limited_builder, &tempo) ==
          APTA_ERROR_LIMIT_EXCEEDED);

    apta_result_builder_destroy(limited_builder);
    apta_result_builder_destroy(builder);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
