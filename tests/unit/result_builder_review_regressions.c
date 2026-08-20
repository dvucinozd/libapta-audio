// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "../../src/core/apta_internal.h"
#include "../../src/beatgrid/apta_s6_internal.h"

static int failures;

#define EXPECT(name, condition)                                             \
    do {                                                                    \
        if (!(condition)) {                                                  \
            fprintf(stderr, "RED %s at %s:%d\n", name, __FILE__, __LINE__); \
            failures += 1;                                                   \
        }                                                                   \
    } while (0)

static void set_range(apta_frame_range_t *range, uint64_t first, uint64_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

static int create_builder(
    apta_context_t *context,
    const apta_result_builder_options_t *options,
    uint64_t total_frames,
    apta_result_builder_t **builder_out)
{
    apta_result_provenance_t provenance;
    apta_source_info_t source;
    if (apta_result_builder_create(context, options, builder_out) !=
        APTA_STATUS_OK) return 0;
    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
    provenance.source_name.data = "x";
    provenance.source_name.size = 1u;
    if (apta_result_builder_set_provenance(*builder_out, &provenance) !=
        APTA_STATUS_OK) return 0;
    apta_source_info_init(&source);
    source.total_frames = total_frames;
    source.sample_rate = 48000u;
    source.channel_count = 2u;
    source.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    return apta_result_builder_set_source_info(*builder_out, &source) ==
           APTA_STATUS_OK;
}

static void make_tempo(
    apta_tempo_view_t *tempo,
    apta_tempo_candidate_t *candidate,
    uint64_t frames)
{
    apta_tempo_view_init(tempo);
    set_range(&tempo->selected.evidence_range, 0u, frames);
    set_range(&tempo->selected.applicability_range, 0u, frames);
    tempo->selected.tempo_millibpm = 128000u;
    tempo->selected.confidence = 90u;
    tempo->selected.state = APTA_FEATURE_FINAL;
    memset(candidate, 0, sizeof(*candidate));
    candidate->tempo_millibpm = 128000u;
    candidate->score = 60000u;
    candidate->confidence = 90u;
    tempo->candidate_count = 1u;
    tempo->candidates = candidate;
}

static void make_grid(
    apta_grid_view_t *grid,
    apta_frame_range_t *coverage,
    apta_grid_segment_t *segment,
    uint64_t frames,
    int global)
{
    set_range(coverage, 0u, frames);
    memset(segment, 0, sizeof(*segment));
    segment->struct_size = (uint32_t)sizeof(*segment);
    segment->api_version = APTA_API_VERSION;
    set_range(&segment->applicability_range, 0u, frames);
    segment->frames_per_beat.whole_frames = 22500u;
    segment->beat_count = (uint32_t)(frames / 22500u);
    segment->nominal_tempo_millibpm = 128000u;
    segment->confidence = 90u;
    segment->state = APTA_FEATURE_FINAL;
    segment->segment_id = 1u;
    segment->revision = global ? 7u : 0u;
    apta_grid_view_init(grid);
    set_range(&grid->requested_range, 0u, frames);
    set_range(&grid->evidence_range, 0u, frames);
    set_range(&grid->applicability_range, 0u, frames);
    grid->representation = APTA_GRID_REPRESENTATION_SEGMENTS;
    grid->state = APTA_FEATURE_FINAL;
    grid->confidence = 90u;
    grid->coverage_range_count = 1u;
    grid->coverage_ranges = coverage;
    grid->segment_count = 1u;
    grid->segments = segment;
}

static void make_revision(
    apta_grid_revision_view_t *revision,
    const apta_grid_view_t *grid)
{
    apta_grid_revision_view_init(revision);
    revision->revision_id = 7u;
    revision->state = APTA_GRID_REVISION_APPLIED;
    revision->confidence = 90u;
    revision->affected_range = grid->applicability_range;
    revision->proposed_representation = grid->representation;
    revision->proposed_segment_count = grid->segment_count;
    revision->proposed_beat_count = grid->beat_count;
}

static void test_revision_and_cap(apta_context_t *context)
{
    apta_result_builder_t *builder = NULL;
    apta_result_builder_options_t options;
    apta_tempo_view_t tempo;
    apta_tempo_candidate_t candidate;
    apta_grid_view_t grid;
    apta_frame_range_t coverage;
    apta_grid_segment_t segment;
    const apta_result_t *result = NULL;
    apta_status_t status;
    apta_grid_revision_view_t revision;
    uint64_t cap;

    apta_result_builder_options_init(&options);
    EXPECT("revision/setup", create_builder(context, &options, 90000u, &builder));
    make_tempo(&tempo, &candidate, 90000u);
    make_grid(&grid, &coverage, &segment, 90000u, 1);
    EXPECT("revision/tempo", apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);
    EXPECT("revision/grid", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) == APTA_STATUS_OK);
    status = apta_result_builder_finalize(builder, &result);
    EXPECT("revision-required", status == APTA_ERROR_CONFLICT && result == NULL);
    if (result != NULL) apta_result_release(result);
    apta_result_builder_destroy(builder);

    cap = sizeof(apta_result_t) + 1u + sizeof(candidate) +
          sizeof(coverage) + sizeof(segment);
    apta_result_builder_options_init(&options);
    options.maximum_allocation_bytes = cap;
    builder = NULL;
    result = NULL;
    EXPECT("cap/setup", create_builder(context, &options, 90000u, &builder));
    EXPECT("cap/tempo", apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);
    EXPECT("cap/grid", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) == APTA_STATUS_OK);
    make_revision(&revision, &grid);
    EXPECT("cap/revision", apta_result_builder_set_grid_revision(builder, &revision) == APTA_STATUS_OK);
    status = apta_result_builder_finalize(builder, &result);
    EXPECT("cap-includes-s6", status == APTA_ERROR_LIMIT_EXCEEDED && result == NULL);
    if (result != NULL) apta_result_release(result);
    apta_result_builder_destroy(builder);

    apta_result_builder_options_init(&options);
    options.maximum_allocation_bytes =
        cap + sizeof(apta_internal_s6_result_state_t);
    builder = NULL;
    result = NULL;
    EXPECT("cap-boundary/setup", create_builder(context, &options, 90000u, &builder));
    EXPECT("cap-boundary/tempo", apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);
    EXPECT("cap-boundary/grid", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) == APTA_STATUS_OK);
    EXPECT("cap-boundary/revision", apta_result_builder_set_grid_revision(builder, &revision) == APTA_STATUS_OK);
    EXPECT("cap-boundary-pass", apta_result_builder_finalize(builder, &result) == APTA_STATUS_OK && result != NULL);
    if (result != NULL) apta_result_release(result);
    apta_result_builder_destroy(builder);
}

static void test_waveform_geometry(apta_context_t *context)
{
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_waveform_column_t column = {-1, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID};
    apta_waveform_span_t span;
    apta_waveform_overview_view_t overview;
    apta_waveform_tile_view_t tile;
    apta_waveform_detail_input_t detail;
    apta_result_builder_options_init(&options);
    EXPECT("wave/setup", create_builder(context, &options, 1024u, &builder));

    memset(&span, 0, sizeof(span));
    set_range(&span.source_range, 0u, 300u);
    span.column_count = 1u;
    span.columns = &column;
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 256u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 90u;
    overview.span_count = 1u;
    overview.spans = &span;
    EXPECT("overview-canonical-geometry",
           apta_result_builder_set_waveform_overview(builder, &overview) ==
               APTA_ERROR_INVALID_ARGUMENT);

    apta_waveform_tile_view_init(&tile);
    tile.level_id = 1u;
    tile.tile_index = 1u;
    set_range(&tile.source_range, 0u, 256u);
    tile.first_column_index = 0u;
    tile.column_count = 1u;
    tile.columns = &column;
    tile.state = APTA_FEATURE_FINAL;
    tile.confidence = 90u;
    apta_waveform_detail_input_init(&detail);
    detail.tile_count = 1u;
    detail.tiles = &tile;
    EXPECT("detail-canonical-geometry",
           apta_result_builder_set_waveform_detail(builder, &detail) ==
               APTA_ERROR_INVALID_ARGUMENT);
    column.flags = 0u;
    EXPECT("detail-valid-column",
           apta_result_builder_set_waveform_detail(builder, &detail) ==
               APTA_ERROR_INVALID_ARGUMENT);
    apta_result_builder_destroy(builder);

    builder = NULL;
    column.flags = APTA_WAVEFORM_COLUMN_VALID;
    column.low = 1u;
    EXPECT("wave2/setup", create_builder(
               context, &options, APTA_TOTAL_FRAMES_UNKNOWN, &builder));
    tile.tile_index = 0u;
    tile.first_column_index = 0u;
    set_range(&tile.source_range, 0u, 256u);
    EXPECT("detail-zero-bands",
           apta_result_builder_set_waveform_detail(builder, &detail) ==
               APTA_ERROR_INVALID_ARGUMENT);
    column.low = 0u;
    EXPECT("detail-final-known-length",
           apta_result_builder_set_waveform_detail(builder, &detail) ==
               APTA_ERROR_INVALID_ARGUMENT);
    apta_result_builder_destroy(builder);
}

static void test_masks_and_state(apta_context_t *context)
{
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_waveform_column_t column = {-1, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID};
    apta_waveform_span_t span;
    apta_waveform_overview_view_t overview;
    const apta_result_t *result = NULL;
    apta_result_info_t info;
    apta_result_builder_info_t builder_info;

    apta_result_builder_options_init(&options);
    EXPECT("mask/setup", create_builder(context, &options, 256u, &builder));
    memset(&span, 0, sizeof(span));
    set_range(&span.source_range, 0u, 256u);
    span.column_count = 1u;
    span.columns = &column;
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 256u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 90u;
    overview.span_count = 1u;
    overview.spans = &span;
    EXPECT("mask/wave", apta_result_builder_set_waveform_overview(builder, &overview) == APTA_STATUS_OK);
    EXPECT("mask/finalize", apta_result_builder_finalize(builder, &result) == APTA_STATUS_OK);
    if (result != NULL) {
        apta_result_info_init(&info);
        EXPECT("waveform-confidence-mask", apta_result_get_info(result, &info) == APTA_STATUS_OK &&
               (info.available_features & APTA_FEATURE_CONFIDENCE) != 0u &&
               (info.changed_features & APTA_FEATURE_CONFIDENCE) != 0u);
        apta_result_release(result);
        result = NULL;
    }
    apta_result_builder_info_init(&builder_info);
    builder_info.session_state = APTA_SESSION_CREATED;
    EXPECT("state/info", apta_result_builder_set_info(builder, &builder_info) == APTA_STATUS_OK);
    EXPECT("session-feature-state",
           apta_result_builder_finalize(builder, &result) ==
               APTA_ERROR_CONFLICT && result == NULL);
    if (result != NULL) apta_result_release(result);
    result = NULL;
    apta_result_builder_info_init(&builder_info);
    builder_info.session_state = APTA_SESSION_COMPLETED;
    overview.state = APTA_FEATURE_PARTIAL;
    EXPECT("state/completed-info", apta_result_builder_set_info(builder, &builder_info) == APTA_STATUS_OK);
    EXPECT("state/partial-wave", apta_result_builder_set_waveform_overview(builder, &overview) == APTA_STATUS_OK);
    EXPECT("completed-requires-final",
           apta_result_builder_finalize(builder, &result) ==
               APTA_ERROR_CONFLICT && result == NULL);
    if (result != NULL) apta_result_release(result);
    apta_result_builder_destroy(builder);
}

static void test_cross_validation(apta_context_t *context)
{
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_grid_view_t grid;
    apta_frame_range_t coverage;
    apta_grid_segment_t segment;
    const apta_result_t *result = NULL;
    apta_tempo_view_t tempo;
    apta_tempo_candidate_t candidates[2];
    apta_key_view_t key;
    apta_key_candidate_t key_candidate;
    apta_meter_view_t meter;
    apta_meter_segment_t meter_segments[2];

    apta_result_builder_options_init(&options);
    EXPECT("cross/setup", create_builder(context, &options, 90000u, &builder));
    make_grid(&grid, &coverage, &segment, 90000u, 0);
    EXPECT("cross/grid", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_LOCAL_BEATGRID, &grid) == APTA_STATUS_OK);
    EXPECT("grid-requires-bpm", apta_result_builder_finalize(builder, &result) == APTA_ERROR_CONFLICT && result == NULL);
    if (result != NULL) {
        apta_result_release(result);
        result = NULL;
    }
    apta_result_builder_reset(builder);
    apta_result_builder_destroy(builder);

    builder = NULL;
    EXPECT("duplicates/setup", create_builder(context, &options, 90000u, &builder));
    make_tempo(&tempo, &candidates[0], 90000u);
    candidates[1] = candidates[0];
    candidates[1].score = candidates[0].score;
    tempo.candidate_count = 2u;
    tempo.candidates = candidates;
    EXPECT("duplicate-tempo", apta_result_builder_set_tempo(builder, &tempo) == APTA_ERROR_CONFLICT);

    apta_key_view_init(&key);
    set_range(&key.applicability_range, 0u, 90000u);
    key.tonic = 9u;
    key.mode = APTA_KEY_MODE_MINOR;
    key.state = APTA_FEATURE_FINAL;
    key.confidence = 90u;
    memset(&key_candidate, 0, sizeof(key_candidate));
    key_candidate.tonic = 0u;
    key_candidate.mode = APTA_KEY_MODE_MAJOR;
    key_candidate.score = 50000u;
    key_candidate.confidence = 80u;
    key.candidate_count = 1u;
    key.candidates = &key_candidate;
    EXPECT("selected-key-membership", apta_result_builder_set_key(builder, &key) == APTA_ERROR_CONFLICT);

    memset(meter_segments, 0, sizeof(meter_segments));
    meter_segments[0].struct_size = sizeof(meter_segments[0]);
    meter_segments[0].api_version = APTA_API_VERSION;
    set_range(&meter_segments[0].applicability_range, 0u, 45000u);
    meter_segments[0].numerator = 4u;
    meter_segments[0].denominator = 4u;
    meter_segments[0].state = APTA_FEATURE_FINAL;
    meter_segments[0].confidence = 90u;
    meter_segments[0].segment_id = 1u;
    meter_segments[0].downbeat_ordinal = 4;
    meter_segments[1] = meter_segments[0];
    set_range(&meter_segments[1].applicability_range, 45000u, 90000u);
    meter_segments[1].downbeat_frame = 45000u;
    meter_segments[1].downbeat_ordinal = 2;
    meter_segments[1].segment_id = 2u;
    apta_meter_view_init(&meter);
    meter.numerator = 4u;
    meter.denominator = 4u;
    meter.state = APTA_FEATURE_FINAL;
    meter.confidence = 90u;
    meter.segment_count = 2u;
    meter.segments = meter_segments;
    meter.downbeat_ordinal = 4;
    EXPECT("meter-ordinal-monotonic", apta_result_builder_set_meter(builder, &meter) == APTA_ERROR_CONFLICT);
    apta_result_builder_destroy(builder);
}

static void test_modifiers_and_deep_cross(apta_context_t *context)
{
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_grid_view_t grid;
    apta_frame_range_t coverage;
    apta_grid_segment_t segments[2];
    apta_beat_t beat;
    apta_tempo_view_t tempo;
    apta_tempo_candidate_t candidate;
    apta_meter_view_t meter;
    apta_meter_segment_t meter_segment;
    apta_grid_revision_view_t revision;
    const apta_result_t *result = NULL;

    apta_result_builder_options_init(&options);
    EXPECT("mod/setup", create_builder(context, &options, 90000u, &builder));
    make_grid(&grid, &coverage, &segments[0], 90000u, 0);
    grid.flags = APTA_GRID_FLAG_DYNAMIC_TEMPO;
    segments[0].flags = APTA_GRID_FLAG_DYNAMIC_TEMPO;
    EXPECT("local-dynamic-rejected",
           apta_result_builder_set_beatgrid(
               builder, APTA_FEATURE_LOCAL_BEATGRID, &grid) ==
               APTA_ERROR_UNSUPPORTED);

    make_grid(&grid, &coverage, &segments[0], 90000u, 1);
    grid.flags = APTA_GRID_FLAG_LOCKED;
    segments[0].flags = APTA_GRID_FLAG_LOCKED;
    EXPECT("global-locked-rejected",
           apta_result_builder_set_beatgrid(
               builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) ==
               APTA_ERROR_UNSUPPORTED);
    grid.flags = APTA_GRID_FLAG_DYNAMIC_TEMPO;
    segments[0].flags = 0u;
    EXPECT("modifier-aggregate",
           apta_result_builder_set_beatgrid(
               builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) ==
               APTA_ERROR_CONFLICT);

    grid.flags = 0u;
    segments[1] = segments[0];
    set_range(&segments[0].applicability_range, 0u, 45000u);
    set_range(&segments[1].applicability_range, 45000u, 90000u);
    segments[1].segment_id = segments[0].segment_id;
    grid.segment_count = 2u;
    grid.segments = segments;
    EXPECT("duplicate-grid-segment-id",
           apta_result_builder_set_beatgrid(
               builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) ==
               APTA_ERROR_CONFLICT);

    grid.segment_count = 1u;
    grid.segments = &segments[0];
    set_range(&segments[0].applicability_range, 0u, 90000u);
    memset(&beat, 0, sizeof(beat));
    beat.position.whole_frame = 100u;
    beat.ordinal = 0;
    beat.confidence = 90u;
    beat.revision = 7u;
    grid.representation = APTA_GRID_REPRESENTATION_HYBRID;
    grid.beat_count = 1u;
    grid.beats = &beat;
    EXPECT("hybrid-segment-beat-agreement",
           apta_result_builder_set_beatgrid(
               builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) ==
               APTA_ERROR_CONFLICT);
    apta_result_builder_destroy(builder);

    builder = NULL;
    EXPECT("cross2/setup", create_builder(context, &options, 90000u, &builder));
    make_tempo(&tempo, &candidate, 90000u);
    candidate.tempo_millibpm = 127000u;
    EXPECT("selected-tempo-membership",
           apta_result_builder_set_tempo(builder, &tempo) ==
               APTA_ERROR_CONFLICT);
    make_tempo(&tempo, &candidate, 90000u);
    EXPECT("cross2/tempo", apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);
    make_grid(&grid, &coverage, &segments[0], 90000u, 0);
    EXPECT("cross2/grid", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_LOCAL_BEATGRID, &grid) == APTA_STATUS_OK);
    memset(&meter_segment, 0, sizeof(meter_segment));
    meter_segment.struct_size = sizeof(meter_segment);
    meter_segment.api_version = APTA_API_VERSION;
    set_range(&meter_segment.applicability_range, 0u, 90000u);
    meter_segment.downbeat_frame = 100u;
    meter_segment.downbeat_ordinal = 0;
    meter_segment.numerator = 4u;
    meter_segment.denominator = 4u;
    meter_segment.state = APTA_FEATURE_FINAL;
    meter_segment.confidence = 90u;
    meter_segment.segment_id = 1u;
    apta_meter_view_init(&meter);
    meter.downbeat_frame = 100u;
    meter.downbeat_ordinal = 0;
    meter.numerator = 4u;
    meter.denominator = 4u;
    meter.state = APTA_FEATURE_FINAL;
    meter.confidence = 90u;
    meter.segment_count = 1u;
    meter.segments = &meter_segment;
    EXPECT("cross2/meter", apta_result_builder_set_meter(builder, &meter) == APTA_STATUS_OK);
    EXPECT("meter-grid-downbeat",
           apta_result_builder_finalize(builder, &result) ==
               APTA_ERROR_CONFLICT && result == NULL);
    if (result != NULL) apta_result_release(result);

    /* A revision must match every proposed count and element revision. */
    apta_result_builder_reset(builder);
    apta_result_builder_destroy(builder);
    builder = NULL;
    EXPECT("revision2/setup", create_builder(context, &options, 90000u, &builder));
    make_grid(&grid, &coverage, &segments[0], 90000u, 1);
    EXPECT("revision2/grid", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) == APTA_STATUS_OK);
    make_revision(&revision, &grid);
    revision.revision_id = 0u;
    EXPECT("revision-nonzero", apta_result_builder_set_grid_revision(builder, &revision) == APTA_ERROR_INVALID_ARGUMENT);
    make_revision(&revision, &grid);
    revision.proposed_segment_count += 1u;
    EXPECT("revision-count-match", apta_result_builder_set_grid_revision(builder, &revision) == APTA_ERROR_CONFLICT);
    make_revision(&revision, &grid);
    segments[0].revision = 8u;
    EXPECT("revision2/grid-replace", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) == APTA_STATUS_OK);
    EXPECT("revision-element-match", apta_result_builder_set_grid_revision(builder, &revision) == APTA_ERROR_CONFLICT);
    apta_result_builder_destroy(builder);
}

static void test_replacement_and_selected_only(apta_context_t *context)
{
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_waveform_column_t columns[2] = {
        {-1, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID},
        {-2, 2, 2u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID}};
    apta_waveform_span_t span;
    apta_waveform_overview_view_t overview;
    apta_waveform_overview_view_t result_overview;
    apta_tempo_view_t tempo;
    apta_key_view_t key;
    const apta_result_t *result = NULL;

    apta_result_builder_options_init(&options);
    EXPECT("replace/setup", create_builder(context, &options, 512u, &builder));
    memset(&span, 0, sizeof(span));
    set_range(&span.source_range, 0u, 512u);
    span.column_count = 2u;
    span.columns = columns;
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 256u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 90u;
    overview.span_count = 1u;
    overview.spans = &span;
    EXPECT("replace/initial", apta_result_builder_set_waveform_overview(builder, &overview) == APTA_STATUS_OK);
    columns[0].maximum = 9;
    EXPECT("replace/success", apta_result_builder_set_waveform_overview(builder, &overview) == APTA_STATUS_OK);
    set_range(&span.source_range, 0u, 300u);
    EXPECT("replace/failure", apta_result_builder_set_waveform_overview(builder, &overview) == APTA_ERROR_INVALID_ARGUMENT);
    EXPECT("replace/finalize", apta_result_builder_finalize(builder, &result) == APTA_STATUS_OK);
    apta_waveform_overview_view_init(&result_overview);
    EXPECT("replace/preserved", apta_result_get_waveform_overview(result, 0u, &result_overview) == APTA_STATUS_OK &&
           result_overview.spans[0].source_range.end_frame == 512u &&
           result_overview.spans[0].columns[0].maximum == 9);
    if (result != NULL) apta_result_release(result);
    apta_result_builder_reset(builder);

    apta_tempo_view_init(&tempo);
    set_range(&tempo.selected.evidence_range, 0u, 512u);
    set_range(&tempo.selected.applicability_range, 0u, 512u);
    tempo.selected.tempo_millibpm = 128000u;
    tempo.selected.state = APTA_FEATURE_FINAL;
    tempo.selected.confidence = 90u;
    EXPECT("tempo-selected-only", apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);
    apta_key_view_init(&key);
    set_range(&key.applicability_range, 0u, 512u);
    key.tonic = 0u;
    key.mode = APTA_KEY_MODE_MAJOR;
    key.state = APTA_FEATURE_FINAL;
    key.confidence = 90u;
    EXPECT("key-selected-only", apta_result_builder_set_key(builder, &key) == APTA_STATUS_OK);
    apta_result_builder_destroy(builder);
}

static void test_explicit_grids(apta_context_t *context)
{
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_tempo_view_t tempo;
    apta_tempo_candidate_t candidate;
    apta_grid_view_t grid;
    apta_frame_range_t coverage;
    apta_beat_t beats[3];
    apta_grid_revision_view_t revision;
    apta_waveform_column_t overview_column = {
        -1, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID};
    apta_waveform_span_t overview_span;
    apta_waveform_overview_view_t overview;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_grid_view_t result_grid;
    uint64_t size = 0u;
    size_t written = 0u;
    uint8_t *bytes = NULL;
    uint8_t *roundtrip = NULL;

    apta_result_builder_options_init(&options);
    EXPECT("explicit-local/setup", create_builder(context, &options, 90000u, &builder));
    make_tempo(&tempo, &candidate, 90000u);
    EXPECT("explicit-local/tempo", apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);
    set_range(&coverage, 0u, 90000u);
    memset(beats, 0, sizeof(beats));
    beats[0].position.whole_frame = 0u;
    beats[0].ordinal = 0;
    beats[0].confidence = 90u;
    apta_grid_view_init(&grid);
    set_range(&grid.requested_range, 0u, 90000u);
    set_range(&grid.evidence_range, 0u, 90000u);
    set_range(&grid.applicability_range, 0u, 90000u);
    grid.representation = APTA_GRID_REPRESENTATION_EXPLICIT;
    grid.state = APTA_FEATURE_FINAL;
    grid.confidence = 90u;
    grid.coverage_range_count = 1u;
    grid.coverage_ranges = &coverage;
    grid.beat_count = 1u;
    grid.beats = beats;
    EXPECT("explicit-local/set", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_LOCAL_BEATGRID, &grid) == APTA_STATUS_OK);
    EXPECT("explicit-local/finalize", apta_result_builder_finalize(builder, &result) == APTA_STATUS_OK && result != NULL);
    if (result != NULL) {
        apta_grid_view_init(&result_grid);
        EXPECT("explicit-local/get", apta_result_get_beatgrid(result, APTA_FEATURE_LOCAL_BEATGRID, NULL, &result_grid) == APTA_STATUS_OK &&
               result_grid.representation == APTA_GRID_REPRESENTATION_EXPLICIT &&
               result_grid.segment_count == 0u && result_grid.beat_count == 1u);
        apta_result_release(result);
        result = NULL;
    }
    apta_result_builder_destroy(builder);

    builder = NULL;
    EXPECT("explicit-global/setup", create_builder(context, &options, 90000u, &builder));
    EXPECT("explicit-global/tempo", apta_result_builder_set_tempo(builder, &tempo) == APTA_STATUS_OK);
    beats[0].revision = 7u;
    beats[1] = beats[0];
    beats[1].position.whole_frame = 22500u;
    beats[1].ordinal = 1;
    beats[2] = beats[1];
    beats[2].position.whole_frame = 45000u;
    beats[2].ordinal = 2;
    grid.beat_count = 3u;
    grid.beats = beats;
    EXPECT("explicit-global/set", apta_result_builder_set_beatgrid(builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid) == APTA_STATUS_OK);
    make_revision(&revision, &grid);
    EXPECT("explicit-global/revision", apta_result_builder_set_grid_revision(builder, &revision) == APTA_STATUS_OK);
    memset(&overview_span, 0, sizeof(overview_span));
    set_range(&overview_span.source_range, 0u, 90000u);
    overview_span.column_count = 1u;
    overview_span.columns = &overview_column;
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 90000u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 90u;
    overview.span_count = 1u;
    overview.spans = &overview_span;
    EXPECT("explicit-global/overview", apta_result_builder_set_waveform_overview(builder, &overview) == APTA_STATUS_OK);
    EXPECT("explicit-global/finalize", apta_result_builder_finalize(builder, &result) == APTA_STATUS_OK && result != NULL);
    if (result != NULL) {
        apta_grid_revision_view_init(&revision);
        EXPECT("explicit-global/revision-get", apta_result_get_grid_revision(result, &revision) == APTA_STATUS_OK && revision.revision_id == 7u);
        EXPECT("explicit-global/size", apta_result_query_serialized_size(result, NULL, &size) == APTA_STATUS_OK);
        if (size <= SIZE_MAX) bytes = (uint8_t *)malloc((size_t)size);
        EXPECT("explicit-global/buffer", bytes != NULL);
        if (bytes != NULL) {
            EXPECT("explicit-global/serialize", apta_result_serialize(result, NULL, bytes, (size_t)size, &written) == APTA_STATUS_OK);
            EXPECT("explicit-global/parse", apta_result_parse(context, NULL, bytes, written, &parsed) == APTA_STATUS_OK && parsed != NULL);
            if (parsed != NULL) {
                size_t roundtrip_written = 0u;
                apta_grid_view_init(&result_grid);
                EXPECT("explicit-global/get", apta_result_get_beatgrid(parsed, APTA_FEATURE_GLOBAL_BEATGRID, NULL, &result_grid) == APTA_STATUS_OK &&
                       result_grid.representation == APTA_GRID_REPRESENTATION_EXPLICIT &&
                       result_grid.segment_count == 0u && result_grid.beat_count == 3u);
                roundtrip = (uint8_t *)malloc(written);
                EXPECT("explicit-global/roundtrip-buffer", roundtrip != NULL);
                if (roundtrip != NULL) {
                    EXPECT("explicit-global/reserialize",
                           apta_result_serialize(parsed, NULL, roundtrip, written,
                                                 &roundtrip_written) ==
                                   APTA_STATUS_OK &&
                               roundtrip_written == written &&
                               memcmp(bytes, roundtrip, written) == 0);
                }
                apta_result_release(parsed);
            }
        }
        apta_result_release(result);
    }
    free(roundtrip);
    free(bytes);
    apta_result_builder_destroy(builder);
}

static void test_overview_state_geometry(apta_context_t *context)
{
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_result_builder_info_t info;
    apta_waveform_column_t columns[3] = {
        {-1, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID},
        {-1, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID},
        {-1, 1, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID}};
    apta_waveform_span_t spans[2];
    apta_waveform_overview_view_t overview;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_waveform_overview_view_t parsed_overview;
    uint64_t size = 0u;
    size_t written = 0u;
    uint8_t *bytes = NULL;

    apta_result_builder_options_init(&options);
    EXPECT("overview-state/setup", create_builder(context, &options, 1024u, &builder));
    memset(spans, 0, sizeof(spans));
    set_range(&spans[0].source_range, 0u, 256u);
    spans[0].first_column_index = 0u;
    spans[0].column_count = 1u;
    spans[0].columns = &columns[0];
    set_range(&spans[1].source_range, 512u, 1024u);
    spans[1].first_column_index = 2u;
    spans[1].column_count = 2u;
    spans[1].columns = &columns[1];
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 256u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 90u;
    overview.span_count = 2u;
    overview.spans = spans;
    EXPECT("overview-final-gap",
           apta_result_builder_set_waveform_overview(builder, &overview) ==
               APTA_ERROR_INVALID_ARGUMENT);

    overview.state = APTA_FEATURE_PARTIAL;
    overview.span_count = 1u;
    overview.spans = &spans[0];
    apta_result_builder_info_init(&info);
    info.session_state = APTA_SESSION_ACTIVE;
    EXPECT("overview-partial/info", apta_result_builder_set_info(builder, &info) == APTA_STATUS_OK);
    EXPECT("overview-partial-prefix/set", apta_result_builder_set_waveform_overview(builder, &overview) == APTA_STATUS_OK);
    EXPECT("overview-partial-prefix/finalize", apta_result_builder_finalize(builder, &result) == APTA_STATUS_OK && result != NULL);
    if (result != NULL) {
        EXPECT("overview-partial-prefix/size", apta_result_query_serialized_size(result, NULL, &size) == APTA_STATUS_OK);
        if (size <= SIZE_MAX) bytes = (uint8_t *)malloc((size_t)size);
        EXPECT("overview-partial-prefix/buffer", bytes != NULL);
        if (bytes != NULL) {
            EXPECT("overview-partial-prefix/serialize", apta_result_serialize(result, NULL, bytes, (size_t)size, &written) == APTA_STATUS_OK);
            EXPECT("overview-partial-prefix/parse", apta_result_parse(context, NULL, bytes, written, &parsed) == APTA_STATUS_OK && parsed != NULL);
            if (parsed != NULL) {
                apta_waveform_overview_view_init(&parsed_overview);
                EXPECT("overview-partial-prefix/get", apta_result_get_waveform_overview(parsed, 0u, &parsed_overview) == APTA_STATUS_OK &&
                       parsed_overview.state == APTA_FEATURE_PARTIAL && parsed_overview.span_count == 1u);
                apta_result_release(parsed);
            }
        }
        apta_result_release(result);
    }
    free(bytes);
    apta_result_builder_destroy(builder);
}

int main(void)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_context_config_init(&config);
    if (apta_context_create(&config, &context) != APTA_STATUS_OK) return 2;
    test_revision_and_cap(context);
    test_waveform_geometry(context);
    test_masks_and_state(context);
    test_cross_validation(context);
    test_modifiers_and_deep_cross(context);
    test_replacement_and_selected_only(context);
    test_explicit_grids(context);
    test_overview_state_geometry(context);
    if (apta_context_destroy(context) != APTA_STATUS_OK) return 3;
    return failures == 0 ? 0 : 1;
}
