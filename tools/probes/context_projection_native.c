/* SPDX-License-Identifier: Apache-2.0 */
/* P2 host-only generated contract probe. No detector or product dispatch. */
#include <apta/apta.h>
#include "apta_grid_position_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void range(apta_frame_range_t *r, uint64_t first, uint64_t end)
{
    apta_frame_range_init(r); r->first_frame = first; r->end_frame = end;
}

static int emit(const apta_result_t *result)
{
    apta_grid_view_t grid;
    apta_meter_view_t meter;
    uint32_t count = 0;
    apta_grid_view_init(&grid);
    if (apta_result_get_beatgrid(result, APTA_FEATURE_GLOBAL_BEATGRID, NULL, &grid) != APTA_STATUS_OK)
        return 1;
    printf("{\"range\":[%" PRIu64 ",%" PRIu64 "],\"points\":[",
           grid.applicability_range.first_frame, grid.applicability_range.end_frame);
    if (grid.representation == APTA_GRID_REPRESENTATION_EXPLICIT) {
        for (uint32_t i = 0; i < grid.beat_count; ++i) {
            const apta_beat_t *b = &grid.beats[i];
            printf("%s[%" PRIu64 ",%u,%" PRId64 "]", count++ ? "," : "",
                   b->position.whole_frame, b->position.fraction_q32, b->ordinal);
        }
    } else {
        for (uint32_t i = 0; i < grid.segment_count; ++i) {
            const apta_grid_segment_t *s = &grid.segments[i];
            uint32_t owned = 0;
            /* Bounded independent enumeration, using the actual C coordinate helper. */
            for (int64_t j = 0; j <= 3072; ++j) {
                apta_fractional_frame_t p;
                if (!apta_internal_grid_segment_position_at_ordinal(s, s->anchor_ordinal + j, &p)) return 1;
                if (p.whole_frame >= s->applicability_range.end_frame) break;
                if (p.whole_frame < s->applicability_range.first_frame) continue;
                printf("%s[%" PRIu64 ",%u,%" PRId64 "]", count++ ? "," : "",
                       p.whole_frame, p.fraction_q32, s->anchor_ordinal + j);
                ++owned;
            }
            if (owned != s->beat_count) return 1;
        }
    }
    printf("],\"meter\":");
    apta_meter_view_init(&meter);
    apta_status_t status = apta_result_get_meter(result, NULL, &meter);
    if (status == APTA_STATUS_OK) {
        printf("[%u,%u,%" PRIu64 ",%" PRId64 "]", meter.numerator, meter.denominator,
               meter.downbeat_frame, meter.downbeat_ordinal);
    } else if (status == APTA_STATUS_NOT_AVAILABLE) printf("null");
    else return 1;
    puts("}");
    return 0;
}

int main(int argc, char **argv)
{
    apta_context_t *context = NULL;
    apta_result_builder_t *builder = NULL;
    const apta_result_t *result = NULL;
    apta_context_config_t config;
    apta_status_t status;
    int rc = 1;
    uint8_t *bytes = NULL;
    FILE *file = NULL;
    if (argc < 3) return 2;
    apta_context_config_init(&config);
    if (apta_context_create(&config, &context) != APTA_STATUS_OK) return 2;
    if (strcmp(argv[1], "read") == 0) {
        file = fopen(argv[2], "rb");
        if (!file || fseek(file, 0, SEEK_END)) goto cleanup;
        long size = ftell(file);
        if (size <= 0 || size > 1048576 || fseek(file, 0, SEEK_SET)) goto cleanup;
        bytes = malloc((size_t)size);
        if (!bytes || fread(bytes, 1, (size_t)size, file) != (size_t)size) goto cleanup;
        if (apta_result_parse(context, NULL, bytes, (size_t)size, &result) != APTA_STATUS_OK) goto cleanup;
        rc = emit(result); goto cleanup;
    }
    if (argc != 4 || strcmp(argv[1], "build") != 0) goto cleanup;
    uint64_t total, first, end, anchor, whole, dbframe;
    int64_t ordinal, dbordinal;
    uint32_t tempo, fraction, n, hasmeter, numerator, representation, defect;
    apta_beat_t beats[3072];
    memset(beats, 0, sizeof(beats));
    file = fopen(argv[2], "r");
    if (!file || fscanf(file, "%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNd64
        " %u %" SCNu64 " %u %u %u %u %" SCNu64 " %" SCNd64 " %u %u",
        &total, &first, &end, &anchor, &ordinal, &tempo, &whole, &fraction, &n, &hasmeter,
        &numerator, &dbframe, &dbordinal, &representation, &defect) != 15
        || n == 0 || n > 3072 || total > 1440000 || !(first < end && end <= total)
        || anchor >= total || ordinal < 0 || ordinal > 6144) goto cleanup;
    for (uint32_t i = 0; i < n; ++i) {
        if (fscanf(file, "%" SCNu64 " %u %" SCNd64, &beats[i].position.whole_frame,
                   &beats[i].position.fraction_q32, &beats[i].ordinal) != 3) goto cleanup;
        beats[i].revision = 1;
    }
    fclose(file); file = NULL;
    apta_result_builder_options_t options;
    apta_result_builder_options_init(&options);
    if (apta_result_builder_create(context, &options, &builder) != APTA_STATUS_OK) goto cleanup;
#define SET(call) do { status = (call); if (status != APTA_STATUS_OK) { \
    printf("{\"rejected\":true,\"status\":%d,\"line\":%d}\n", (int)status, __LINE__); rc = 0; goto cleanup; } } while (0)
    apta_result_provenance_t provenance;
    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
    provenance.source_name.data = "P2 public generated probe";
    provenance.source_name.size = (uint32_t)strlen(provenance.source_name.data);
    SET(apta_result_builder_set_provenance(builder, &provenance));
    apta_source_info_t source;
    apta_source_info_init(&source);
    source.total_frames = total; source.sample_rate = 48000;
    source.channel_count = 1; source.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    SET(apta_result_builder_set_source_info(builder, &source));
    apta_waveform_span_t span;
    memset(&span, 0, sizeof(span)); range(&span.source_range, 0, total); span.column_count = 1;
    apta_waveform_column_t column = {0}; column.flags = APTA_WAVEFORM_COLUMN_VALID;
    span.columns = &column;
    apta_waveform_overview_view_t overview;
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = (uint32_t)total;
    overview.span_count = 1;
    overview.spans = &span; overview.state = APTA_FEATURE_FINAL; overview.confidence = 0;
    SET(apta_result_builder_set_waveform_overview(builder, &overview));
    apta_tempo_view_t t;
    apta_tempo_view_init(&t);
    t.selected.tempo_millibpm = tempo; t.selected.state = APTA_FEATURE_FINAL; t.selected.confidence = 0;
    range(&t.selected.evidence_range, first, end); range(&t.selected.applicability_range, first, end);
    apta_tempo_candidate_t candidate = {0}; candidate.tempo_millibpm = tempo; candidate.score = 1;
    t.candidate_count = 1; t.candidates = &candidate;
    SET(apta_result_builder_set_tempo(builder, &t));
    apta_grid_segment_t segment;
    memset(&segment, 0, sizeof(segment));
    segment.struct_size = sizeof(segment); segment.api_version = APTA_API_VERSION;
    range(&segment.applicability_range, first, end);
    segment.anchor_position.whole_frame = anchor; segment.anchor_ordinal = ordinal;
    segment.frames_per_beat.whole_frames = whole; segment.frames_per_beat.fraction_q32 = fraction;
    segment.beat_count = n; segment.nominal_tempo_millibpm = tempo;
    segment.state = APTA_FEATURE_FINAL; segment.segment_id = 1; segment.revision = 1;
    apta_grid_view_t grid;
    apta_frame_range_t coverage; range(&coverage, first, end);
    apta_grid_view_init(&grid);
    range(&grid.requested_range, first, end); range(&grid.evidence_range, first, end);
    range(&grid.applicability_range, first, end);
    grid.coverage_range_count = 1; grid.coverage_ranges = &coverage;
    grid.representation = representation; grid.state = APTA_FEATURE_FINAL; grid.confidence = 0;
    if (representation != APTA_GRID_REPRESENTATION_EXPLICIT) { grid.segment_count = 1; grid.segments = &segment; }
    if (representation != APTA_GRID_REPRESENTATION_SEGMENTS) { grid.beat_count = n; grid.beats = beats; }
    if (defect == 4) beats[0].position.fraction_q32 ^= 1;
    SET(apta_result_builder_set_beatgrid(builder, APTA_FEATURE_GLOBAL_BEATGRID, &grid));
    apta_grid_revision_view_t revision;
    apta_grid_revision_view_init(&revision);
    revision.revision_id = 1; revision.state = APTA_GRID_REVISION_APPLIED; revision.confidence = 0;
    revision.proposed_representation = representation;
    revision.proposed_segment_count = grid.segment_count; revision.proposed_beat_count = grid.beat_count;
    range(&revision.affected_range, first, end);
    SET(apta_result_builder_set_grid_revision(builder, &revision));
    apta_meter_segment_t ms;
    apta_meter_view_t meter;
    if (hasmeter) {
        if (defect == 1) ++dbframe;
        if (defect == 2) ++dbordinal;
        if (defect == 3) { dbframe = anchor; dbordinal = ordinal; }
        memset(&ms, 0, sizeof(ms)); ms.struct_size = sizeof(ms); ms.api_version = APTA_API_VERSION;
        range(&ms.applicability_range, first, end);
        ms.numerator = (uint16_t)numerator; ms.denominator = 4; ms.state = APTA_FEATURE_FINAL;
        ms.downbeat_frame = dbframe; ms.downbeat_ordinal = dbordinal; ms.segment_id = 1;
        apta_meter_view_init(&meter); meter.state = APTA_FEATURE_FINAL; meter.confidence = 0;
        meter.numerator = (uint16_t)numerator; meter.denominator = 4;
        meter.downbeat_frame = dbframe; meter.downbeat_ordinal = dbordinal;
        meter.segment_count = 1; meter.segments = &ms;
        SET(apta_result_builder_set_meter(builder, &meter));
    }
    SET(apta_result_builder_finalize(builder, &result));
    uint64_t size = 0;
    size_t written = 0;
    SET(apta_result_query_serialized_size(result, NULL, &size));
    if (size > 1048576 || !(bytes = malloc((size_t)size))) goto cleanup;
    SET(apta_result_serialize(result, NULL, bytes, (size_t)size, &written));
    file = fopen(argv[3], "wbx");
    if (!file || fwrite(bytes, 1, written, file) != written) goto cleanup;
    rc = emit(result);
cleanup:
    if (file) fclose(file);
    if (result) apta_result_release(result);
    if (builder) apta_result_builder_destroy(builder);
    free(bytes);
    if (context && apta_context_destroy(context) != APTA_STATUS_OK) rc = 1;
    return rc;
}
