// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define MAX_SEGMENTS 65536u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
    } while (0)

static void set_range(apta_frame_range_t *range, uint64_t first, uint64_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4u) << 32u);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static void put_u64(uint8_t *p, uint64_t value)
{
    put_u32(p, (uint32_t)value);
    put_u32(p + 4u, (uint32_t)(value >> 32u));
}

static uint32_t crc32c(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;
    for (index = 0u; index < size; ++index) {
        uint32_t bit;
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^
                  ((crc & 1u) != 0u ? UINT32_C(0x82f63b78) : 0u);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static int create_builder(
    apta_context_t *context,
    apta_session_state_t session_state,
    uint64_t total_frames,
    apta_result_builder_t **builder_out)
{
    apta_result_builder_options_t options;
    apta_result_builder_info_t info;
    apta_result_provenance_t provenance;
    apta_source_info_t source;
    apta_waveform_column_t column = {0};
    apta_waveform_span_t span;
    apta_waveform_overview_view_t overview;

    apta_result_builder_options_init(&options);
    options.maximum_meter_segments = MAX_SEGMENTS;
    options.maximum_allocation_bytes = UINT64_C(32) * 1024u * 1024u;
    CHECK(apta_result_builder_create(context, &options, builder_out) ==
          APTA_STATUS_OK);
    apta_result_builder_info_init(&info);
    info.session_state = session_state;
    info.container_version = 1u;
    CHECK(apta_result_builder_set_info(*builder_out, &info) == APTA_STATUS_OK);
    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
    provenance.source_name.data = "state-scale";
    provenance.source_name.size = 11u;
    CHECK(apta_result_builder_set_provenance(*builder_out, &provenance) ==
          APTA_STATUS_OK);
    apta_source_info_init(&source);
    source.total_frames = total_frames;
    source.sample_rate = 48000u;
    source.channel_count = 1u;
    source.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    CHECK(apta_result_builder_set_source_info(*builder_out, &source) ==
          APTA_STATUS_OK);
    memset(&span, 0, sizeof(span));
    set_range(&span.source_range, 0u, total_frames);
    column.flags = APTA_WAVEFORM_COLUMN_VALID;
    span.column_count = 1u;
    span.columns = &column;
    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = total_frames;
    overview.state = session_state == APTA_SESSION_COMPLETED
                         ? APTA_FEATURE_FINAL
                         : APTA_FEATURE_PARTIAL;
    overview.confidence = APTA_CONFIDENCE_UNKNOWN;
    overview.span_count = 1u;
    overview.spans = &span;
    CHECK(apta_result_builder_set_waveform_overview(*builder_out, &overview) ==
          APTA_STATUS_OK);
    return 1;
}

static void init_segment(
    apta_meter_segment_t *segment,
    uint64_t first,
    uint64_t end,
    int64_t ordinal,
    uint32_t id,
    apta_feature_state_t state)
{
    memset(segment, 0, sizeof(*segment));
    segment->struct_size = (uint32_t)sizeof(*segment);
    segment->api_version = APTA_API_VERSION;
    set_range(&segment->applicability_range, first, end);
    segment->downbeat_frame = first;
    segment->downbeat_ordinal = ordinal;
    segment->numerator = 4u;
    segment->denominator = 4u;
    segment->state = state;
    segment->confidence = APTA_CONFIDENCE_UNKNOWN;
    segment->segment_id = id;
}

static void init_meter(
    apta_meter_view_t *meter,
    const apta_meter_segment_t *segments,
    uint32_t count,
    apta_feature_state_t state)
{
    apta_meter_view_init(meter);
    meter->downbeat_frame = segments[0].downbeat_frame;
    meter->downbeat_ordinal = segments[0].downbeat_ordinal;
    meter->numerator = segments[0].numerator;
    meter->denominator = segments[0].denominator;
    meter->state = state;
    meter->confidence = APTA_CONFIDENCE_UNKNOWN;
    meter->segment_count = count;
    meter->segments = segments;
}

static int state_case(
    apta_context_t *context,
    apta_session_state_t session_state,
    apta_feature_state_t meter_state,
    apta_feature_state_t first_state,
    apta_feature_state_t second_state,
    apta_status_t setter_expected,
    apta_status_t finalize_expected,
    int partial_expected)
{
    apta_result_builder_t *builder = NULL;
    apta_meter_segment_t segments[2];
    apta_meter_view_t meter;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_key_view_t key_view;
    apta_meter_view_t meter_view;
    apta_quality_view_t quality_view;
    uint8_t *bytes = NULL;
    uint64_t size = 0u;
    size_t written = 0u;
    apta_status_t status;
    int ok = 0;

    if (!create_builder(context, session_state, 200u, &builder)) goto cleanup;
    init_segment(&segments[0], 0u, 100u, 0, 1u, first_state);
    init_segment(&segments[1], 100u, 200u, 4, 2u, second_state);
    init_meter(&meter, segments, 2u, meter_state);
    status = apta_result_builder_set_meter(builder, &meter);
    if (status != setter_expected) goto cleanup;
    if (status < 0) {
        ok = 1;
        goto cleanup;
    }
    status = apta_result_builder_finalize(builder, &result);
    if (status != finalize_expected) goto cleanup;
    if (status < 0) {
        ok = result == NULL;
        goto cleanup;
    }
    if (apta_result_query_serialized_size(result, NULL, &size) < 0 ||
        size > SIZE_MAX) goto cleanup;
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL || apta_result_serialize(
            result, NULL, bytes, (size_t)size, &written) < 0 ||
        written != (size_t)size ||
        (((bytes[16] & 1u) != 0u) != partial_expected) ||
        apta_result_parse(context, NULL, bytes, written, &parsed) < 0) {
        goto cleanup;
    }
    apta_key_view_init(&key_view);
    apta_meter_view_init(&meter_view);
    apta_quality_view_init(&quality_view);
    if (apta_result_get_key(parsed, NULL, &key_view) !=
            APTA_STATUS_NOT_AVAILABLE ||
        apta_result_get_quality(
            parsed, APTA_FEATURE_METER_DOWNBEAT, &quality_view) !=
            APTA_STATUS_NOT_AVAILABLE ||
        apta_result_get_meter(parsed, NULL, &meter_view) != APTA_STATUS_OK ||
        meter_view.state != meter_state || meter_view.segment_count != 2u ||
        meter_view.segments[0].state != first_state ||
        meter_view.segments[1].state != second_state) goto cleanup;
    ok = 1;

cleanup:
    if (parsed != NULL) apta_result_release(parsed);
    free(bytes);
    if (result != NULL) apta_result_release(result);
    if (builder != NULL) apta_result_builder_destroy(builder);
    return ok;
}

static int id_order_case(apta_context_t *context)
{
    apta_result_builder_t *builder = NULL;
    apta_meter_segment_t segments[2];
    apta_meter_view_t meter;
    apta_status_t status;
    CHECK(create_builder(context, APTA_SESSION_COMPLETED, 200u, &builder));
    init_segment(&segments[0], 0u, 100u, 0, 2u, APTA_FEATURE_FINAL);
    init_segment(&segments[1], 100u, 200u, 4, 1u, APTA_FEATURE_FINAL);
    init_meter(&meter, segments, 2u, APTA_FEATURE_FINAL);
    status = apta_result_builder_set_meter(builder, &meter);
    apta_result_builder_destroy(builder);
    CHECK(status == APTA_ERROR_CONFLICT);
    return 1;
}

static int absent_case(
    apta_context_t *context, apta_session_state_t session_state)
{
    apta_result_builder_t *builder = NULL;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_key_view_t key;
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    uint8_t *bytes = NULL;
    uint64_t size = 0u;
    size_t written = 0u;
    int ok = 0;
    if (!create_builder(context, session_state, 200u, &builder) ||
        apta_result_builder_finalize(builder, &result) < 0 ||
        apta_result_query_serialized_size(result, NULL, &size) < 0 ||
        size > SIZE_MAX) goto cleanup;
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL || apta_result_serialize(
            result, NULL, bytes, (size_t)size, &written) < 0 ||
        apta_result_parse(context, NULL, bytes, written, &parsed) < 0) {
        goto cleanup;
    }
    apta_key_view_init(&key);
    apta_meter_view_init(&meter);
    apta_quality_view_init(&quality);
    ok = apta_result_get_key(parsed, NULL, &key) ==
             APTA_STATUS_NOT_AVAILABLE &&
         apta_result_get_meter(parsed, NULL, &meter) ==
             APTA_STATUS_NOT_AVAILABLE &&
         apta_result_get_quality(
             parsed, APTA_FEATURE_MUSICAL_KEY, &quality) ==
             APTA_STATUS_NOT_AVAILABLE;
cleanup:
    if (parsed != NULL) apta_result_release(parsed);
    free(bytes);
    if (result != NULL) apta_result_release(result);
    if (builder != NULL) apta_result_builder_destroy(builder);
    return ok;
}

static int maximum_count_case(apta_context_t *context)
{
    apta_result_builder_t *builder = NULL;
    apta_meter_segment_t *segments = NULL;
    apta_meter_view_t meter;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    uint8_t *bytes = NULL;
    uint64_t size = 0u;
    size_t written = 0u;
    uint32_t index;
    int ok = 0;

    segments = (apta_meter_segment_t *)calloc(
        MAX_SEGMENTS, sizeof(*segments));
    if (segments == NULL || !create_builder(
            context, APTA_SESSION_COMPLETED, MAX_SEGMENTS, &builder)) {
        goto cleanup;
    }
    for (index = 0u; index < MAX_SEGMENTS; ++index) {
        init_segment(&segments[index], index, index + 1u, index,
                     index + 1u, APTA_FEATURE_FINAL);
    }
    init_meter(&meter, segments, MAX_SEGMENTS, APTA_FEATURE_FINAL);
    if (apta_result_builder_set_meter(builder, &meter) < 0 ||
        apta_result_builder_finalize(builder, &result) < 0 ||
        apta_result_query_serialized_size(result, NULL, &size) < 0 ||
        size > SIZE_MAX) goto cleanup;
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL || apta_result_serialize(
            result, NULL, bytes, (size_t)size, &written) < 0 ||
        written != (size_t)size ||
        apta_result_parse(context, NULL, bytes, written, &parsed) < 0) {
        goto cleanup;
    }
    ok = 1;

cleanup:
    if (parsed != NULL) apta_result_release(parsed);
    free(bytes);
    if (result != NULL) apta_result_release(result);
    if (builder != NULL) apta_result_builder_destroy(builder);
    free(segments);
    return ok;
}

static int grid_conflict_case(apta_context_t *context)
{
    apta_result_builder_t *builder = NULL;
    apta_tempo_view_t tempo;
    apta_tempo_candidate_t candidate;
    apta_grid_view_t grid;
    apta_frame_range_t coverage;
    apta_grid_segment_t grid_segment;
    apta_meter_segment_t meter_segment;
    apta_meter_view_t meter;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    uint8_t *bytes = NULL;
    uint64_t size = 0u;
    size_t written = 0u;
    uint32_t count;
    uint32_t index;
    int ok = 0;

    if (!create_builder(context, APTA_SESSION_COMPLETED, 45000u, &builder)) {
        goto cleanup;
    }
    apta_tempo_view_init(&tempo);
    set_range(&tempo.selected.evidence_range, 0u, 45000u);
    set_range(&tempo.selected.applicability_range, 0u, 45000u);
    tempo.selected.tempo_millibpm = 128000u;
    tempo.selected.confidence = 90u;
    tempo.selected.state = APTA_FEATURE_FINAL;
    memset(&candidate, 0, sizeof(candidate));
    candidate.tempo_millibpm = 128000u;
    candidate.score = 60000u;
    candidate.confidence = 90u;
    tempo.candidate_count = 1u;
    tempo.candidates = &candidate;
    if (apta_result_builder_set_tempo(builder, &tempo) < 0) goto cleanup;

    set_range(&coverage, 0u, 45000u);
    memset(&grid_segment, 0, sizeof(grid_segment));
    grid_segment.struct_size = (uint32_t)sizeof(grid_segment);
    grid_segment.api_version = APTA_API_VERSION;
    set_range(&grid_segment.applicability_range, 0u, 45000u);
    grid_segment.frames_per_beat.whole_frames = 22500u;
    grid_segment.beat_count = 2u;
    grid_segment.nominal_tempo_millibpm = 128000u;
    grid_segment.confidence = 90u;
    grid_segment.state = APTA_FEATURE_FINAL;
    grid_segment.segment_id = 1u;
    apta_grid_view_init(&grid);
    set_range(&grid.requested_range, 0u, 45000u);
    set_range(&grid.evidence_range, 0u, 45000u);
    set_range(&grid.applicability_range, 0u, 45000u);
    grid.representation = APTA_GRID_REPRESENTATION_SEGMENTS;
    grid.state = APTA_FEATURE_FINAL;
    grid.confidence = 90u;
    grid.coverage_range_count = 1u;
    grid.coverage_ranges = &coverage;
    grid.segment_count = 1u;
    grid.segments = &grid_segment;
    if (apta_result_builder_set_beatgrid(
            builder, APTA_FEATURE_LOCAL_BEATGRID, &grid) < 0) goto cleanup;

    init_segment(&meter_segment, 0u, 45000u, 0, 1u, APTA_FEATURE_FINAL);
    init_meter(&meter, &meter_segment, 1u, APTA_FEATURE_FINAL);
    if (apta_result_builder_set_meter(builder, &meter) < 0 ||
        apta_result_builder_finalize(builder, &result) < 0 ||
        apta_result_query_serialized_size(result, NULL, &size) < 0 ||
        size > SIZE_MAX) goto cleanup;
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL || apta_result_serialize(
            result, NULL, bytes, (size_t)size, &written) < 0 ||
        apta_result_parse(context, NULL, bytes, written, &parsed) < 0) {
        goto cleanup;
    }
    apta_result_release(parsed);
    parsed = NULL;
    count = get_u32(bytes + 20u);
    for (index = 0u; index < count; ++index) {
        uint8_t *entry = bytes + (size_t)get_u64(bytes + 24u) +
                         (size_t)index * 40u;
        if (memcmp(entry, "MTRD", 4u) == 0) {
            uint8_t *payload = bytes + (size_t)get_u64(entry + 8u);
            const size_t payload_size = (size_t)get_u64(entry + 16u);
            put_u64(payload + 16u, 1u);
            put_u64(payload + 48u + 16u, 1u);
            put_u32(entry + 32u, crc32c(payload, payload_size));
            ok = apta_result_parse(
                     context, NULL, bytes, written, &parsed) ==
                 APTA_ERROR_CORRUPT_DATA && parsed == NULL;
            break;
        }
    }
cleanup:
    if (parsed != NULL) apta_result_release(parsed);
    free(bytes);
    if (result != NULL) apta_result_release(result);
    if (builder != NULL) apta_result_builder_destroy(builder);
    return ok;
}

static int partial_all_case(apta_context_t *context)
{
    apta_result_builder_t *builder = NULL;
    apta_key_view_t key;
    apta_meter_segment_t segment;
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    uint8_t *bytes = NULL;
    uint64_t size = 0u;
    size_t written = 0u;
    int ok = 0;
    if (!create_builder(context, APTA_SESSION_ACTIVE, 200u, &builder)) {
        goto cleanup;
    }
    apta_key_view_init(&key);
    set_range(&key.applicability_range, 0u, 200u);
    key.tonic = 2u;
    key.mode = APTA_KEY_MODE_MAJOR;
    key.tuning_offset_cents = 5;
    key.confidence = 60u;
    key.state = APTA_FEATURE_PARTIAL;
    if (apta_result_builder_set_key(builder, &key) < 0) goto cleanup;
    init_segment(&segment, 0u, 200u, 0, 1u, APTA_FEATURE_STABLE);
    init_meter(&meter, &segment, 1u, APTA_FEATURE_PARTIAL);
    if (apta_result_builder_set_meter(builder, &meter) < 0) goto cleanup;
    apta_quality_view_init(&quality);
    quality.feature = APTA_FEATURE_MUSICAL_KEY;
    quality.evidence_coverage_permille = 0u;
    quality.confidence = 0u;
    quality.state = APTA_FEATURE_PARTIAL;
    if (apta_result_builder_set_quality(builder, &quality) < 0) goto cleanup;
    quality.feature = APTA_FEATURE_METER_DOWNBEAT;
    quality.evidence_coverage_permille = APTA_EVIDENCE_COVERAGE_UNKNOWN;
    quality.confidence = APTA_CONFIDENCE_UNKNOWN;
    quality.state = APTA_FEATURE_STABLE;
    if (apta_result_builder_set_quality(builder, &quality) < 0 ||
        apta_result_builder_finalize(builder, &result) < 0 ||
        apta_result_query_serialized_size(result, NULL, &size) < 0 ||
        size > SIZE_MAX) goto cleanup;
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL || apta_result_serialize(
            result, NULL, bytes, (size_t)size, &written) < 0 ||
        (bytes[16] & 1u) == 0u ||
        apta_result_parse(context, NULL, bytes, written, &parsed) < 0) {
        goto cleanup;
    }
    apta_key_view_init(&key);
    apta_meter_view_init(&meter);
    apta_quality_view_init(&quality);
    if (apta_result_get_key(parsed, NULL, &key) != APTA_STATUS_OK ||
        key.state != APTA_FEATURE_PARTIAL || key.tonic != 2u ||
        key.mode != APTA_KEY_MODE_MAJOR || key.tuning_offset_cents != 5 ||
        key.confidence != 60u || key.candidate_count != 0u ||
        apta_result_get_meter(parsed, NULL, &meter) != APTA_STATUS_OK ||
        meter.state != APTA_FEATURE_PARTIAL || meter.segment_count != 1u ||
        meter.segments[0].state != APTA_FEATURE_STABLE ||
        apta_result_get_quality(
            parsed, APTA_FEATURE_MUSICAL_KEY, &quality) != APTA_STATUS_OK ||
        quality.state != APTA_FEATURE_PARTIAL || quality.confidence != 0u ||
        quality.evidence_coverage_permille != 0u) goto cleanup;
    apta_quality_view_init(&quality);
    ok = apta_result_get_quality(
             parsed, APTA_FEATURE_METER_DOWNBEAT, &quality) == APTA_STATUS_OK &&
         quality.state == APTA_FEATURE_STABLE &&
         quality.confidence == APTA_CONFIDENCE_UNKNOWN &&
         quality.evidence_coverage_permille ==
             APTA_EVIDENCE_COVERAGE_UNKNOWN;
cleanup:
    if (parsed != NULL) apta_result_release(parsed);
    free(bytes);
    if (result != NULL) apta_result_release(result);
    if (builder != NULL) apta_result_builder_destroy(builder);
    return ok;
}

int main(int argc, char **argv)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_context_config_init(&config);
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    if (argc == 2 && strcmp(argv[1], "id-scale") == 0) {
        CHECK(id_order_case(context));
        CHECK(maximum_count_case(context));
        CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
        return 0;
    }
    CHECK(state_case(context, APTA_SESSION_COMPLETED, APTA_FEATURE_FINAL,
                     APTA_FEATURE_PROVISIONAL, APTA_FEATURE_FINAL,
                     APTA_ERROR_CONFLICT, APTA_ERROR_CONFLICT, 0));
    CHECK(state_case(context, APTA_SESSION_ACTIVE, APTA_FEATURE_STABLE,
                     APTA_FEATURE_PARTIAL, APTA_FEATURE_FINAL,
                     APTA_ERROR_CONFLICT, APTA_ERROR_CONFLICT, 0));
    CHECK(state_case(context, APTA_SESSION_ACTIVE, APTA_FEATURE_PARTIAL,
                     APTA_FEATURE_PARTIAL, APTA_FEATURE_FINAL,
                     APTA_STATUS_OK, APTA_STATUS_OK, 1));
    CHECK(state_case(context, APTA_SESSION_ACTIVE, APTA_FEATURE_STABLE,
                     APTA_FEATURE_STABLE, APTA_FEATURE_FINAL,
                     APTA_STATUS_OK, APTA_STATUS_OK, 1));
    CHECK(state_case(context, APTA_SESSION_COMPLETED, APTA_FEATURE_FINAL,
                     APTA_FEATURE_FINAL, APTA_FEATURE_FINAL,
                     APTA_STATUS_OK, APTA_STATUS_OK, 0));
    CHECK(state_case(context, APTA_SESSION_COMPLETED, APTA_FEATURE_PARTIAL,
                     APTA_FEATURE_PARTIAL, APTA_FEATURE_FINAL,
                     APTA_STATUS_OK, APTA_ERROR_CONFLICT, 0));
    CHECK(absent_case(context, APTA_SESSION_ACTIVE));
    CHECK(absent_case(context, APTA_SESSION_COMPLETED));
    CHECK(grid_conflict_case(context));
    CHECK(partial_all_case(context));
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
