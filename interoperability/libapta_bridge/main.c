// SPDX-License-Identifier: Apache-2.0
#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define EXPECTED_SIZE 1032u
#define EXPECTED_FEATURES                                              \
    (APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL |  \
     APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID |                 \
     APTA_FEATURE_GLOBAL_BEATGRID | APTA_FEATURE_CONFIDENCE)

#define CHECK(condition, field)                                            \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "libapta bridge: %s failed at %s:%d\n",       \
                    field, __FILE__, __LINE__);                             \
            goto cleanup;                                                  \
        }                                                                  \
    } while (0)

static int hex_value(int character)
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static int load_hex(const char *path, uint8_t **bytes_out, size_t *size_out)
{
    FILE *file = NULL;
    uint8_t *bytes = NULL;
    size_t size = 0u, capacity = 0u;
    int high = -1, character;
    *bytes_out = NULL;
    *size_out = 0u;
    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "libapta bridge: cannot open fixture: %s\n", path);
        return 0;
    }
    while ((character = fgetc(file)) != EOF) {
        int value;
        uint8_t *replacement;
        size_t next_capacity;
        if (isspace((unsigned char)character)) continue;
        value = hex_value(character);
        if (value < 0) goto failure;
        if (high < 0) { high = value; continue; }
        if (size == capacity) {
            next_capacity = capacity == 0u ? 1024u : capacity * 2u;
            if (next_capacity < capacity) goto failure;
            replacement = (uint8_t *)realloc(bytes, next_capacity);
            if (replacement == NULL) goto failure;
            bytes = replacement;
            capacity = next_capacity;
        }
        bytes[size++] = (uint8_t)((high << 4u) | value);
        high = -1;
    }
    if (fclose(file) != 0 || high >= 0 || size == 0u) {
        file = NULL;
        goto failure;
    }
    *bytes_out = bytes;
    *size_out = size;
    return 1;
failure:
    if (file != NULL) (void)fclose(file);
    free(bytes);
    return 0;
}

static int write_binary(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (file == NULL) return 0;
    ok = fwrite(bytes, 1u, size, file) == size;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int write_report(const char *path, size_t size)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (file == NULL) return 0;
    ok = fprintf(file,
        "{\n"
        "  \"bridge\": {\n"
        "    \"implementation\": \"libapta\",\n"
        "    \"links_installed_package\": true,\n"
        "    \"public_api_only\": true\n"
        "  },\n"
        "  \"canonical_byte_identity\": true,\n"
        "  \"semantic_assertion_set\": \"full public fixture semantics\",\n"
        "  \"size_bytes\": %zu,\n"
        "  \"status\": \"pass\"\n"
        "}\n", size) > 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int verify_public_semantics(const apta_result_t *result)
{
    apta_result_info_t info;
    apta_source_info_t source;
    apta_waveform_overview_view_t overview;
    apta_waveform_tile_view_t tile;
    apta_metadata_view_t metadata;
    apta_tempo_view_t tempo;
    apta_grid_view_t local_grid, global_grid;
    apta_grid_revision_view_t revision;

    apta_result_info_init(&info);
    if (apta_result_get_info(result, &info) != APTA_STATUS_OK ||
        info.specification_major != 1u || info.specification_minor != 0u ||
        info.container_version != 1u || info.session_state != APTA_SESSION_COMPLETED ||
        info.available_features != EXPECTED_FEATURES) return 0;

    apta_source_info_init(&source);
    if (apta_result_get_source_info(result, &source) != APTA_STATUS_OK ||
        source.total_frames != 1024u || source.sample_rate != 48000u ||
        source.channel_count != 1u || source.channel_layout != APTA_CHANNEL_LAYOUT_MONO ||
        source.fingerprint_kind != APTA_SOURCE_FINGERPRINT_NONE) return 0;

    apta_waveform_overview_view_init(&overview);
    if (apta_result_get_waveform_overview(result, 0u, &overview) != APTA_STATUS_OK ||
        overview.level.level_id != 0u || overview.level.frames_per_column != 1024u ||
        overview.level.origin_frame != 0u || overview.state != APTA_FEATURE_FINAL ||
        overview.span_count != 1u || overview.spans[0].source_range.first_frame != 0u ||
        overview.spans[0].source_range.end_frame != 1024u ||
        overview.spans[0].first_column_index != 0u || overview.spans[0].column_count != 1u ||
        overview.spans[0].columns[0].minimum != 0 || overview.spans[0].columns[0].maximum != 0 ||
        overview.spans[0].columns[0].rms != 0u || overview.spans[0].columns[0].low != 0u ||
        overview.spans[0].columns[0].mid != 0u || overview.spans[0].columns[0].high != 0u ||
        overview.spans[0].columns[0].flags != APTA_WAVEFORM_COLUMN_VALID) return 0;

    apta_waveform_tile_view_init(&tile);
    if (apta_result_get_waveform_tile(result, 1u, 0u, &tile) != APTA_STATUS_OK ||
        tile.level_id != 1u || tile.tile_index != 0u ||
        tile.source_range.first_frame != 0u || tile.source_range.end_frame != 256u ||
        tile.first_column_index != 0u || tile.column_count != 1u ||
        tile.state != APTA_FEATURE_FINAL || tile.confidence != 90u ||
        tile.columns[0].minimum != 0 || tile.columns[0].maximum != 0 ||
        tile.columns[0].rms != 0u || tile.columns[0].flags != APTA_WAVEFORM_COLUMN_VALID) return 0;

    apta_metadata_view_init(&metadata);
    if (apta_result_get_metadata(result, &metadata) != APTA_STATUS_OK ||
        metadata.flags != APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT ||
        metadata.producer_name.size != 5u ||
        memcmp(metadata.producer_name.data, "suite", 5u) != 0) return 0;

    apta_tempo_view_init(&tempo);
    if (apta_result_get_tempo(result, NULL, &tempo) != APTA_STATUS_OK ||
        tempo.selected.state != APTA_FEATURE_FINAL || tempo.selected.confidence != 90u ||
        tempo.selected.tempo_millibpm != 120000u || tempo.selected.candidate_set_id != 1u ||
        tempo.selected.evidence_range.first_frame != 0u ||
        tempo.selected.evidence_range.end_frame != 1024u ||
        tempo.selected.applicability_range.first_frame != 0u ||
        tempo.selected.applicability_range.end_frame != 1024u ||
        tempo.candidate_count != 1u || tempo.candidates[0].tempo_millibpm != 120000u ||
        tempo.candidates[0].score != 1000u || tempo.candidates[0].confidence != 90u ||
        tempo.candidates[0].relation_to_selected != APTA_TEMPO_RELATION_INDEPENDENT) return 0;

    apta_grid_view_init(&local_grid);
    if (apta_result_get_beatgrid(result, APTA_FEATURE_LOCAL_BEATGRID, NULL, &local_grid) != APTA_STATUS_OK ||
        local_grid.representation != APTA_GRID_REPRESENTATION_SEGMENTS ||
        local_grid.state != APTA_FEATURE_FINAL || local_grid.confidence != 90u ||
        local_grid.requested_range.first_frame != 0u || local_grid.requested_range.end_frame != 1024u ||
        local_grid.evidence_range.first_frame != 0u || local_grid.evidence_range.end_frame != 1024u ||
        local_grid.applicability_range.first_frame != 0u || local_grid.applicability_range.end_frame != 1024u ||
        local_grid.coverage_range_count != 1u || local_grid.coverage_ranges[0].first_frame != 0u ||
        local_grid.coverage_ranges[0].end_frame != 1024u || local_grid.segment_count != 1u ||
        local_grid.beat_count != 0u || local_grid.segments[0].applicability_range.first_frame != 0u ||
        local_grid.segments[0].applicability_range.end_frame != 1024u ||
        local_grid.segments[0].anchor_position.whole_frame != 0u ||
        local_grid.segments[0].anchor_position.fraction_q32 != 0u ||
        local_grid.segments[0].anchor_ordinal != 0 ||
        local_grid.segments[0].frames_per_beat.whole_frames != 24000u ||
        local_grid.segments[0].frames_per_beat.fraction_q32 != 0u ||
        local_grid.segments[0].beat_count != 1u ||
        local_grid.segments[0].nominal_tempo_millibpm != 120000u ||
        local_grid.segments[0].segment_id != 1u || local_grid.segments[0].revision != 0u ||
        local_grid.segments[0].state != APTA_FEATURE_FINAL || local_grid.segments[0].confidence != 90u) return 0;

    apta_grid_view_init(&global_grid);
    if (apta_result_get_beatgrid(result, APTA_FEATURE_GLOBAL_BEATGRID, NULL, &global_grid) != APTA_STATUS_OK ||
        global_grid.representation != APTA_GRID_REPRESENTATION_SEGMENTS ||
        global_grid.state != APTA_FEATURE_FINAL || global_grid.confidence != 90u ||
        global_grid.requested_range.first_frame != 0u || global_grid.requested_range.end_frame != 1024u ||
        global_grid.evidence_range.first_frame != 0u || global_grid.evidence_range.end_frame != 1024u ||
        global_grid.applicability_range.first_frame != 0u || global_grid.applicability_range.end_frame != 1024u ||
        global_grid.coverage_range_count != 1u || global_grid.coverage_ranges[0].first_frame != 0u ||
        global_grid.coverage_ranges[0].end_frame != 1024u || global_grid.segment_count != 1u ||
        global_grid.beat_count != 0u || global_grid.segments[0].applicability_range.first_frame != 0u ||
        global_grid.segments[0].applicability_range.end_frame != 1024u ||
        global_grid.segments[0].anchor_position.whole_frame != 0u ||
        global_grid.segments[0].anchor_position.fraction_q32 != 0u ||
        global_grid.segments[0].anchor_ordinal != 0 ||
        global_grid.segments[0].frames_per_beat.whole_frames != 24000u ||
        global_grid.segments[0].frames_per_beat.fraction_q32 != 0u ||
        global_grid.segments[0].beat_count != 1u ||
        global_grid.segments[0].nominal_tempo_millibpm != 120000u ||
        global_grid.segments[0].segment_id != 2u || global_grid.segments[0].revision != 1u ||
        global_grid.segments[0].state != APTA_FEATURE_FINAL || global_grid.segments[0].confidence != 90u) return 0;

    apta_grid_revision_view_init(&revision);
    if (apta_result_get_grid_revision(result, &revision) != APTA_STATUS_OK ||
        revision.state != APTA_GRID_REVISION_APPLIED || revision.confidence != 90u ||
        revision.revision_id != 1u || revision.previous_revision_id != 0u ||
        revision.affected_range.first_frame != 0u || revision.affected_range.end_frame != 1024u ||
        revision.proposed_representation != APTA_GRID_REPRESENTATION_SEGMENTS ||
        revision.proposed_segment_count != 1u || revision.proposed_beat_count != 0u) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_parse_options_t parse_options;
    apta_serialize_options_t serialize_options;
    const apta_result_t *result = NULL;
    uint8_t *fixture = NULL, *canonical = NULL;
    size_t fixture_size = 0u, written = 0u;
    uint64_t required = 0u;
    int success = 0;
    if (argc != 4) {
        fprintf(stderr, "usage: %s fixture.apta.hex canonical.apta bridge-report.json\n", argv[0]);
        return 2;
    }
    CHECK(load_hex(argv[1], &fixture, &fixture_size), "load fixture");
    CHECK(fixture_size == EXPECTED_SIZE, "fixture size");
    apta_context_config_init(&config);
    config.requested_capabilities = EXPECTED_FEATURES;
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK, "context create");
    apta_parse_options_init(&parse_options);
    parse_options.flags = APTA_PARSE_STRICT;
    CHECK(apta_result_parse(context, &parse_options, fixture, fixture_size, &result) == APTA_STATUS_OK,
          "strict parse");
    CHECK(result != NULL, "parsed result");
    CHECK(verify_public_semantics(result), "public semantic assertions");
    apta_serialize_options_init(&serialize_options);
    serialize_options.flags = APTA_SERIALIZE_CANONICAL;
    CHECK(apta_result_query_serialized_size(result, &serialize_options, &required) == APTA_STATUS_OK,
          "canonical size");
    CHECK(required == fixture_size, "canonical exact size");
    canonical = (uint8_t *)malloc((size_t)required);
    CHECK(canonical != NULL, "canonical allocation");
    CHECK(apta_result_serialize(result, &serialize_options, canonical, (size_t)required, &written) == APTA_STATUS_OK,
          "canonical serialize");
    CHECK(written == fixture_size, "canonical written size");
    CHECK(memcmp(canonical, fixture, fixture_size) == 0, "canonical byte identity");
    CHECK(write_binary(argv[2], canonical, written), "write canonical fixture");
    CHECK(write_report(argv[3], written), "write bridge report");
    success = 1;
cleanup:
    free(canonical);
    free(fixture);
    if (result != NULL) apta_result_release(result);
    if (context != NULL && apta_context_destroy(context) != APTA_STATUS_OK) success = 0;
    return success ? 0 : 1;
}
