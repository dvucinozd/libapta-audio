// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "stream_equivalence.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint32_t get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8u) |
           ((uint32_t)source[2] << 16u) |
           ((uint32_t)source[3] << 24u);
}

static uint64_t get_u64(const uint8_t *source)
{
    return (uint64_t)get_u32(source) |
           ((uint64_t)get_u32(source + 4u) << 32u);
}

int main(void)
{
    static const uint8_t expected_meta[] = {
        0xA7u,
        0x01u, 0x67u, 'l', 'i', 'b', 'a', 'p', 't', 'a',
        0x02u, 0x65u, '0', '.', '1', '.', '0',
        0x03u, 0x69u, 'r', 'e', 'f', 'e', 'r', 'e', 'n', 'c', 'e',
        0x04u, 0x60u,
        0x05u, 0x1Au, 0x65u, 0x53u, 0xF1u, 0x00u,
        0x06u, 0x68u, 't', 'r', 'a', 'c', 'k', ':', '4', '2',
        0x07u, 0x69u, 'r', 'o', 'u', 'n', 'd', 't', 'r', 'i', 'p'
    };
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_metadata_t metadata;
    apta_metadata_view_t view;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    int16_t pcm[1024] = {0};
    uint8_t *first = NULL;
    uint8_t *second = NULL;
    uint64_t required = 0u;
    uint64_t repeated_required = 0u;
    size_t written = 0u;
    size_t repeated_written = 0u;
    uint32_t accepted = 0u;
    uint32_t section_count;
    const uint8_t *meta_directory;
    uint64_t meta_offset;
    uint64_t meta_size;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_metadata_init(&metadata);
    metadata.flags =
        APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
        APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT |
        APTA_METADATA_FLAG_BACKEND_NAME_PRESENT |
        APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT |
        APTA_METADATA_FLAG_CREATION_TIME_PRESENT |
        APTA_METADATA_FLAG_COMMENTS_PRESENT;
    metadata.producer_name.data = "libapta";
    metadata.producer_name.size = 7u;
    metadata.producer_version_string.data = "0.1.0";
    metadata.producer_version_string.size = 5u;
    metadata.backend_name.data = "reference";
    metadata.backend_name.size = 9u;
    metadata.backend_version.data = NULL;
    metadata.backend_version.size = 0u;
    metadata.creation_unix_time = UINT64_C(1700000000);
    metadata.application_source_id_kind = APTA_METADATA_SOURCE_ID_TEXT;
    metadata.application_source_id.data = (const uint8_t *)"track:42";
    metadata.application_source_id.size = 8u;
    metadata.comments.data = "roundtrip";
    metadata.comments.size = 9u;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 1024u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 1024u);
    CHECK(apta_session_signal_end_of_input(session, 1024u) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    CHECK(apta_session_process(session, &budget, NULL) ==
          APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    CHECK(apta_result_query_serialized_size(result, NULL, &required) ==
          APTA_STATUS_OK);
    CHECK(required > sizeof(expected_meta));
    first = (uint8_t *)malloc((size_t)required);
    second = (uint8_t *)malloc((size_t)required);
    CHECK(first != NULL);
    CHECK(second != NULL);

    CHECK(apta_result_serialize(
              result,
              NULL,
              first,
              (size_t)required,
              &written) == APTA_STATUS_OK);
    CHECK(written == (size_t)required);
    CHECK(apta_test_stream_matches_buffer(result, first, required));
    CHECK(apta_test_stream_parse_matches_buffer(context, first, required));
    CHECK(get_u64(first + 32u) == required);

    section_count = get_u32(first + 20u);
    CHECK(section_count == 2u);
    CHECK(memcmp(first + 96u, "WOVR", 4u) == 0);
    meta_directory = first + 136u;
    CHECK(memcmp(meta_directory, "META", 4u) == 0);
    meta_offset = get_u64(meta_directory + 8u);
    meta_size = get_u64(meta_directory + 16u);
    CHECK((meta_offset & 7u) == 0u);
    CHECK(meta_size == sizeof(expected_meta));
    CHECK(meta_offset + meta_size == required);
    CHECK(memcmp(first + meta_offset, expected_meta, sizeof(expected_meta)) == 0);

    CHECK(apta_result_parse(
              context,
              NULL,
              first,
              written,
              &parsed) == APTA_STATUS_OK);
    CHECK(parsed != NULL);
    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(parsed, &view) == APTA_STATUS_OK);
    CHECK(view.flags == metadata.flags);
    CHECK(view.producer_name.size == 7u);
    CHECK(memcmp(view.producer_name.data, "libapta", 7u) == 0);
    CHECK(view.producer_version_string.size == 5u);
    CHECK(memcmp(view.producer_version_string.data, "0.1.0", 5u) == 0);
    CHECK(view.backend_name.size == 9u);
    CHECK(memcmp(view.backend_name.data, "reference", 9u) == 0);
    CHECK(view.backend_version.size == 0u);
    CHECK(view.creation_unix_time == UINT64_C(1700000000));
    CHECK(view.application_source_id_kind == APTA_METADATA_SOURCE_ID_TEXT);
    CHECK(view.application_source_id.size == 8u);
    CHECK(memcmp(view.application_source_id.data, "track:42", 8u) == 0);
    CHECK(view.comments.size == 9u);
    CHECK(memcmp(view.comments.data, "roundtrip", 9u) == 0);

    CHECK(apta_result_query_serialized_size(
              parsed,
              NULL,
              &repeated_required) == APTA_STATUS_OK);
    CHECK(repeated_required == required);
    CHECK(apta_result_serialize(
              parsed,
              NULL,
              second,
              (size_t)repeated_required,
              &repeated_written) == APTA_STATUS_OK);
    CHECK(repeated_written == written);
    CHECK(memcmp(first, second, written) == 0);

    apta_result_release(parsed);
    apta_result_release(result);
    free(second);
    free(first);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
