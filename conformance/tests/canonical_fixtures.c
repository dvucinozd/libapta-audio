// SPDX-License-Identifier: Apache-2.0
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#ifndef APTA_EXTERNAL_FIXTURE_HEX_PATH
#error "APTA_EXTERNAL_FIXTURE_HEX_PATH is required"
#endif

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    const char *name;
    apta_feature_mask_t expected_features;
    uint32_t has_wdtl;
    uint32_t has_meta;
    uint32_t has_temp;
    uint32_t has_lgrd;
    uint32_t has_ggrd;
} fixture_case_t;

static const fixture_case_t SUITE_FIXTURES[] = {
    {
        "v1-wovr-only.apta.hex",
        APTA_FEATURE_WAVEFORM_OVERVIEW,
        0u, 0u, 0u, 0u, 0u
    },
    {
        "v1-wovr-meta.apta.hex",
        APTA_FEATURE_WAVEFORM_OVERVIEW,
        0u, 1u, 0u, 0u, 0u
    },
    {
        "v1-wovr-wdtl.apta.hex",
        APTA_FEATURE_WAVEFORM_OVERVIEW |
            APTA_FEATURE_WAVEFORM_DETAIL,
        1u, 0u, 0u, 0u, 0u
    },
    {
        "v1-wovr-temp.apta.hex",
        APTA_FEATURE_WAVEFORM_OVERVIEW |
            APTA_FEATURE_BPM |
            APTA_FEATURE_CONFIDENCE,
        0u, 0u, 1u, 0u, 0u
    },
    {
        "v1-wovr-temp-lgrd.apta.hex",
        APTA_FEATURE_WAVEFORM_OVERVIEW |
            APTA_FEATURE_BPM |
            APTA_FEATURE_LOCAL_BEATGRID |
            APTA_FEATURE_CONFIDENCE,
        0u, 0u, 1u, 1u, 0u
    },
    {
        "v1-wovr-temp-ggrd-revn.apta.hex",
        APTA_FEATURE_WAVEFORM_OVERVIEW |
            APTA_FEATURE_BPM |
            APTA_FEATURE_GLOBAL_BEATGRID |
            APTA_FEATURE_CONFIDENCE,
        0u, 0u, 1u, 0u, 1u
    },
    {
        "v1-all-standard-sections.apta.hex",
        APTA_FEATURE_WAVEFORM_OVERVIEW |
            APTA_FEATURE_WAVEFORM_DETAIL |
            APTA_FEATURE_BPM |
            APTA_FEATURE_LOCAL_BEATGRID |
            APTA_FEATURE_GLOBAL_BEATGRID |
            APTA_FEATURE_CONFIDENCE,
        1u, 1u, 1u, 1u, 1u
    }
};

static int hex_value(int character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static int load_hex_file(
    const char *path,
    uint8_t **bytes_out,
    size_t *size_out)
{
    FILE *file;
    uint8_t *bytes = NULL;
    size_t size = 0u;
    size_t capacity = 0u;
    int high_nibble = -1;
    int character;

    if (path == NULL || bytes_out == NULL || size_out == NULL) {
        return 0;
    }
    *bytes_out = NULL;
    *size_out = 0u;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    while ((character = fgetc(file)) != EOF) {
        int value;
        if (isspace((unsigned char)character)) {
            continue;
        }
        value = hex_value(character);
        if (value < 0) {
            (void)fclose(file);
            free(bytes);
            return 0;
        }
        if (high_nibble < 0) {
            high_nibble = value;
            continue;
        }
        if (size == capacity) {
            size_t new_capacity = capacity == 0u ? 256u : capacity * 2u;
            uint8_t *replacement;
            if (new_capacity < capacity) {
                (void)fclose(file);
                free(bytes);
                return 0;
            }
            replacement = (uint8_t *)realloc(bytes, new_capacity);
            if (replacement == NULL) {
                (void)fclose(file);
                free(bytes);
                return 0;
            }
            bytes = replacement;
            capacity = new_capacity;
        }
        bytes[size++] = (uint8_t)((high_nibble << 4u) | value);
        high_nibble = -1;
    }

    if (fclose(file) != 0 || high_nibble >= 0 || size == 0u) {
        free(bytes);
        return 0;
    }
    *bytes_out = bytes;
    *size_out = size;
    return 1;
}

static int build_suite_path(
    const char *fixture_name,
    char *path_out,
    size_t path_size)
{
    const char *reference_path = APTA_EXTERNAL_FIXTURE_HEX_PATH;
    size_t directory_length = strlen(reference_path);
    int written;

    while (directory_length != 0u &&
           reference_path[directory_length - 1u] != '/' &&
           reference_path[directory_length - 1u] != '\\') {
        directory_length -= 1u;
    }
    if (directory_length == 0u || directory_length > INT_MAX) {
        return 0;
    }
    written = snprintf(
        path_out,
        path_size,
        "%.*scontainer-v1-suite/%s",
        (int)directory_length,
        reference_path,
        fixture_name);
    return written > 0 && (size_t)written < path_size;
}

static int verify_exact_roundtrip(
    const apta_result_t *result,
    const uint8_t *fixture,
    size_t fixture_size)
{
    uint64_t required_size = 0u;
    uint8_t *serialized;
    size_t written = 0u;

    CHECK(apta_result_query_serialized_size(result, NULL, &required_size) ==
          APTA_STATUS_OK);
    CHECK(required_size == fixture_size);
    CHECK(required_size <= SIZE_MAX);
    serialized = (uint8_t *)malloc((size_t)required_size);
    CHECK(serialized != NULL);
    CHECK(apta_result_serialize(
              result,
              NULL,
              serialized,
              (size_t)required_size,
              &written) == APTA_STATUS_OK);
    CHECK(written == fixture_size);
    if (memcmp(serialized, fixture, fixture_size) != 0) {
        size_t index;
        for (index = 0u; index < fixture_size; ++index) {
            if (serialized[index] != fixture[index]) {
                fprintf(
                    stderr,
                    "fixture mismatch at byte %zu: expected=%02x actual=%02x\n",
                    index,
                    fixture[index],
                    serialized[index]);
                break;
            }
        }
    }
    CHECK(memcmp(serialized, fixture, fixture_size) == 0);
    free(serialized);
    return 0;
}

static int verify_overview(const apta_result_t *result)
{
    apta_waveform_overview_view_t overview;

    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.level.level_id == 0u);
    CHECK(overview.level.frames_per_column == 1024u);
    CHECK(overview.level.origin_frame == 0u);
    CHECK(overview.state == APTA_FEATURE_FINAL);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].source_range.first_frame == 0u);
    CHECK(overview.spans[0].source_range.end_frame == 1024u);
    CHECK(overview.spans[0].first_column_index == 0u);
    CHECK(overview.spans[0].column_count == 1u);
    CHECK(overview.spans[0].columns[0].minimum == 0);
    CHECK(overview.spans[0].columns[0].maximum == 0);
    CHECK(overview.spans[0].columns[0].rms == 0u);
    CHECK(overview.spans[0].columns[0].flags ==
          APTA_WAVEFORM_COLUMN_VALID);
    return 0;
}

static int verify_reference_fixture(apta_context_t *context)
{
    static const uint8_t source_id[] = {0x01u, 0x02u, 0x03u};
    uint8_t *fixture = NULL;
    size_t fixture_size = 0u;
    const apta_result_t *result = NULL;
    apta_result_info_t info;
    apta_source_info_t source_info;
    apta_metadata_view_t metadata;

    CHECK(load_hex_file(
              APTA_EXTERNAL_FIXTURE_HEX_PATH,
              &fixture,
              &fixture_size));
    CHECK(fixture_size == 303u);
    CHECK(apta_result_parse(
              context,
              NULL,
              fixture,
              fixture_size,
              &result) == APTA_STATUS_OK);
    CHECK(result != NULL);

    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.container_version == APTA_CONTAINER_VERSION);
    CHECK(info.specification_major == APTA_SPEC_VERSION_MAJOR);
    CHECK(info.specification_minor == APTA_SPEC_VERSION_MINOR);
    CHECK(info.producer_api_version == APTA_API_VERSION);
    CHECK(info.session_state == APTA_SESSION_COMPLETED);
    CHECK((info.available_features & APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u);

    apta_source_info_init(&source_info);
    CHECK(apta_result_get_source_info(result, &source_info) == APTA_STATUS_OK);
    CHECK(source_info.total_frames == 1024u);
    CHECK(source_info.sample_rate == 48000u);
    CHECK(source_info.channel_count == 1u);
    CHECK(source_info.channel_layout == APTA_CHANNEL_LAYOUT_MONO);
    CHECK(source_info.fingerprint_kind == APTA_SOURCE_FINGERPRINT_NONE);

    CHECK(verify_overview(result) == 0);

    apta_metadata_view_init(&metadata);
    CHECK(apta_result_get_metadata(result, &metadata) == APTA_STATUS_OK);
    CHECK(metadata.flags ==
          (APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
           APTA_METADATA_FLAG_CREATION_TIME_PRESENT |
           APTA_METADATA_FLAG_COMMENTS_PRESENT));
    CHECK(metadata.producer_name.size == 8u);
    CHECK(memcmp(metadata.producer_name.data, "external", 8u) == 0);
    CHECK(metadata.creation_unix_time == UINT64_C(1700000000));
    CHECK(metadata.application_source_id_kind ==
          APTA_METADATA_SOURCE_ID_BYTES);
    CHECK(metadata.application_source_id.size == sizeof(source_id));
    CHECK(memcmp(
              metadata.application_source_id.data,
              source_id,
              sizeof(source_id)) == 0);
    CHECK(metadata.comments.size == 7u);
    CHECK(memcmp(metadata.comments.data, "fixture", 7u) == 0);

    CHECK(verify_exact_roundtrip(result, fixture, fixture_size) == 0);
    apta_result_release(result);
    free(fixture);
    return 0;
}

static int verify_suite_fixture(
    apta_context_t *context,
    const fixture_case_t *fixture_case)
{
    char path[1024];
    uint8_t *fixture = NULL;
    size_t fixture_size = 0u;
    const apta_result_t *result = NULL;
    apta_result_info_t info;
    apta_source_info_t source;

    CHECK(build_suite_path(fixture_case->name, path, sizeof(path)));
    CHECK(load_hex_file(path, &fixture, &fixture_size));
    CHECK(apta_result_parse(
              context,
              NULL,
              fixture,
              fixture_size,
              &result) == APTA_STATUS_OK);
    CHECK(result != NULL);

    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.container_version == APTA_CONTAINER_VERSION);
    CHECK(info.specification_major == APTA_SPEC_VERSION_MAJOR);
    CHECK(info.specification_minor == APTA_SPEC_VERSION_MINOR);
    CHECK(info.producer_api_version == APTA_API_VERSION);
    CHECK(info.session_state == APTA_SESSION_COMPLETED);
    CHECK(info.available_features == fixture_case->expected_features);

    apta_source_info_init(&source);
    CHECK(apta_result_get_source_info(result, &source) == APTA_STATUS_OK);
    CHECK(source.total_frames == 1024u);
    CHECK(source.sample_rate == 48000u);
    CHECK(source.channel_count == 1u);
    CHECK(source.channel_layout == APTA_CHANNEL_LAYOUT_MONO);
    CHECK(source.fingerprint_kind == APTA_SOURCE_FINGERPRINT_NONE);

    CHECK(verify_overview(result) == 0);

    if (fixture_case->has_wdtl) {
        apta_waveform_tile_view_t tile;
        apta_waveform_tile_view_init(&tile);
        CHECK(apta_result_get_waveform_tile(result, 1u, 0u, &tile) ==
              APTA_STATUS_OK);
        CHECK(tile.source_range.first_frame == 0u);
        CHECK(tile.source_range.end_frame == 256u);
        CHECK(tile.first_column_index == 0u);
        CHECK(tile.column_count == 1u);
        CHECK(tile.state == APTA_FEATURE_FINAL);
        CHECK(tile.confidence == 90u);
        CHECK(tile.columns[0].flags == APTA_WAVEFORM_COLUMN_VALID);
    }

    if (fixture_case->has_meta) {
        apta_metadata_view_t metadata;
        apta_metadata_view_init(&metadata);
        CHECK(apta_result_get_metadata(result, &metadata) == APTA_STATUS_OK);
        CHECK(metadata.flags == APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT);
        CHECK(metadata.producer_name.size == 5u);
        CHECK(memcmp(metadata.producer_name.data, "suite", 5u) == 0);
    }

    if (fixture_case->has_temp) {
        apta_tempo_view_t tempo;
        apta_tempo_view_init(&tempo);
        CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
        CHECK(tempo.selected.state == APTA_FEATURE_FINAL);
        CHECK(tempo.selected.confidence == 90u);
        CHECK(tempo.selected.tempo_millibpm == 120000u);
        CHECK(tempo.candidate_count == 1u);
        CHECK(tempo.candidates[0].tempo_millibpm == 120000u);
        CHECK(tempo.candidates[0].relation_to_selected ==
              APTA_TEMPO_RELATION_INDEPENDENT);
    }

    if (fixture_case->has_lgrd) {
        apta_grid_view_t grid;
        apta_grid_view_init(&grid);
        CHECK(apta_result_get_beatgrid(
                  result,
                  APTA_FEATURE_LOCAL_BEATGRID,
                  NULL,
                  &grid) == APTA_STATUS_OK);
        CHECK(grid.representation == APTA_GRID_REPRESENTATION_SEGMENTS);
        CHECK(grid.state == APTA_FEATURE_FINAL);
        CHECK(grid.segment_count == 1u);
        CHECK(grid.beat_count == 0u);
        CHECK(grid.segments[0].frames_per_beat.whole_frames == 24000u);
        CHECK(grid.segments[0].nominal_tempo_millibpm == 120000u);
    }

    if (fixture_case->has_ggrd) {
        apta_grid_view_t grid;
        apta_grid_revision_view_t revision;
        apta_grid_view_init(&grid);
        CHECK(apta_result_get_beatgrid(
                  result,
                  APTA_FEATURE_GLOBAL_BEATGRID,
                  NULL,
                  &grid) == APTA_STATUS_OK);
        CHECK(grid.representation == APTA_GRID_REPRESENTATION_SEGMENTS);
        CHECK(grid.state == APTA_FEATURE_FINAL);
        CHECK(grid.segment_count == 1u);
        CHECK(grid.beat_count == 0u);
        CHECK(grid.segments[0].revision == 1u);
        CHECK(grid.segments[0].nominal_tempo_millibpm == 120000u);

        apta_grid_revision_view_init(&revision);
        CHECK(apta_result_get_grid_revision(result, &revision) ==
              APTA_STATUS_OK);
        CHECK(revision.state == APTA_GRID_REVISION_APPLIED);
        CHECK(revision.revision_id == 1u);
        CHECK(revision.proposed_representation ==
              APTA_GRID_REPRESENTATION_SEGMENTS);
        CHECK(revision.proposed_segment_count == 1u);
        CHECK(revision.proposed_beat_count == 0u);
    }

    CHECK(verify_exact_roundtrip(result, fixture, fixture_size) == 0);
    apta_result_release(result);
    free(fixture);
    return 0;
}

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    size_t index;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = capabilities;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    CHECK(verify_reference_fixture(context) == 0);
    for (index = 0u;
         index < sizeof(SUITE_FIXTURES) / sizeof(SUITE_FIXTURES[0]);
         ++index) {
        CHECK(verify_suite_fixture(context, &SUITE_FIXTURES[index]) == 0);
    }

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
