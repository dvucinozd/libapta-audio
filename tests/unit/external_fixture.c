// SPDX-License-Identifier: Apache-2.0
#include <ctype.h>
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
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define FIXTURE_SIZE 303u

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

static int load_fixture(uint8_t output[FIXTURE_SIZE])
{
    FILE *file = fopen(APTA_EXTERNAL_FIXTURE_HEX_PATH, "rb");
    size_t output_size = 0u;
    int high_nibble = -1;
    int character;

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
            return 0;
        }
        if (high_nibble < 0) {
            high_nibble = value;
        } else {
            if (output_size >= FIXTURE_SIZE) {
                (void)fclose(file);
                return 0;
            }
            output[output_size++] =
                (uint8_t)((high_nibble << 4u) | value);
            high_nibble = -1;
        }
    }

    if (fclose(file) != 0) {
        return 0;
    }
    return high_nibble < 0 && output_size == FIXTURE_SIZE;
}

int main(void)
{
    static const uint8_t source_id[] = {0x01u, 0x02u, 0x03u};
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_result_info_t info;
    apta_source_info_t source_info;
    apta_waveform_overview_view_t overview;
    apta_metadata_view_t metadata;
    uint8_t fixture[FIXTURE_SIZE];
    uint8_t serialized[FIXTURE_SIZE];
    uint64_t required_size = 0u;
    size_t written = 0u;

    CHECK(load_fixture(fixture));

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    CHECK(apta_result_parse(
              context,
              NULL,
              fixture,
              sizeof(fixture),
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

    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.level.level_id == 0u);
    CHECK(overview.level.frames_per_column == 1024u);
    CHECK(overview.state == APTA_FEATURE_FINAL);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].source_range.first_frame == 0u);
    CHECK(overview.spans[0].source_range.end_frame == 1024u);
    CHECK(overview.spans[0].column_count == 1u);
    CHECK(overview.spans[0].columns[0].minimum == 0);
    CHECK(overview.spans[0].columns[0].maximum == 0);
    CHECK(overview.spans[0].columns[0].rms == 0u);
    CHECK(overview.spans[0].columns[0].flags == APTA_WAVEFORM_COLUMN_VALID);

    apta_metadata_view_init(&metadata);
    CHECK(apta_result_get_metadata(result, &metadata) == APTA_STATUS_OK);
    CHECK(metadata.flags ==
          (APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
           APTA_METADATA_FLAG_CREATION_TIME_PRESENT |
           APTA_METADATA_FLAG_COMMENTS_PRESENT));
    CHECK(metadata.producer_name.size == 8u);
    CHECK(memcmp(metadata.producer_name.data, "external", 8u) == 0);
    CHECK(metadata.creation_unix_time == UINT64_C(1700000000));
    CHECK(metadata.application_source_id_kind == APTA_METADATA_SOURCE_ID_BYTES);
    CHECK(metadata.application_source_id.size == sizeof(source_id));
    CHECK(memcmp(
              metadata.application_source_id.data,
              source_id,
              sizeof(source_id)) == 0);
    CHECK(metadata.comments.size == 7u);
    CHECK(memcmp(metadata.comments.data, "fixture", 7u) == 0);

    CHECK(apta_result_query_serialized_size(result, NULL, &required_size) ==
          APTA_STATUS_OK);
    CHECK(required_size == FIXTURE_SIZE);
    CHECK(apta_result_serialize(
              result,
              NULL,
              serialized,
              sizeof(serialized),
              &written) == APTA_STATUS_OK);
    CHECK(written == FIXTURE_SIZE);
    if (memcmp(serialized, fixture, FIXTURE_SIZE) != 0) {
        size_t index;
        for (index = 0u; index < FIXTURE_SIZE; ++index) {
            if (serialized[index] != fixture[index]) {
                fprintf(
                    stderr,
                    "fixture mismatch at byte %zu: expected=%02x actual=%02x\n",
                    index,
                    fixture[index],
                    serialized[index]);
            }
        }
    }
    CHECK(memcmp(serialized, fixture, FIXTURE_SIZE) == 0);

    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
