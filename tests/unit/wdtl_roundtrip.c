// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "stream_equivalence.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
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
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_waveform_tile_view_t tile;
    int16_t pcm[1024] = {0};
    uint8_t first[512];
    uint8_t second[512];
    uint64_t serialized_size = 0u;
    uint64_t parsed_size = 0u;
    size_t written = 0u;
    size_t rewritten = 0u;
    uint32_t accepted = 0u;

    memset(first, 0xA5, sizeof(first));
    memset(second, 0x5A, sizeof(second));

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

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

    CHECK(apta_result_query_serialized_size(result, NULL, &serialized_size) ==
          APTA_STATUS_OK);
    CHECK(serialized_size == 376u);
    CHECK(apta_result_serialize(
              result,
              NULL,
              first,
              sizeof(first),
              &written) == APTA_STATUS_OK);
    CHECK(written == serialized_size);
    CHECK(apta_test_stream_matches_buffer(result, first, serialized_size));
    CHECK(apta_test_stream_parse_matches_buffer(context, first, serialized_size));

    CHECK(memcmp(first, "APTA", 4u) == 0);
    CHECK(get_u32(first + 16u) == 0u);
    CHECK(get_u32(first + 20u) == 2u);
    CHECK(get_u64(first + 24u) == 96u);
    CHECK(get_u64(first + 32u) == serialized_size);

    CHECK(memcmp(first + 96u, "WOVR", 4u) == 0);
    CHECK(get_u64(first + 104u) == 176u);
    CHECK(get_u64(first + 112u) == 90u);

    CHECK(memcmp(first + 136u, "WDTL", 4u) == 0);
    CHECK(get_u64(first + 144u) == 272u);
    CHECK(get_u64(first + 152u) == 104u);

    CHECK(get_u32(first + 272u) == 1u);
    CHECK(get_u32(first + 276u) == 0u);
    CHECK(get_u64(first + 280u) == 16u);

    CHECK(get_u32(first + 288u) == 1u);
    CHECK(get_u32(first + 292u) == 0u);
    CHECK(get_u64(first + 296u) == 0u);
    CHECK(get_u64(first + 304u) == 1024u);
    CHECK(get_u32(first + 312u) == 0u);
    CHECK(get_u32(first + 316u) == 4u);
    CHECK(get_u64(first + 320u) == 64u);
    CHECK(get_u32(first + 328u) == APTA_FEATURE_FINAL);
    CHECK(first[334u] == APTA_CONFIDENCE_UNKNOWN);
    CHECK(first[335u] == 0u);

    CHECK(apta_result_parse(
              context,
              NULL,
              first,
              written,
              &parsed) == APTA_STATUS_OK);
    CHECK(parsed != NULL);
    CHECK((apta_result_get_available_features(parsed) &
           APTA_FEATURE_WAVEFORM_DETAIL) != 0u);

    apta_waveform_tile_view_init(&tile);
    CHECK(apta_result_get_waveform_tile(parsed, 1u, 0u, &tile) ==
          APTA_STATUS_OK);
    CHECK(tile.source_range.first_frame == 0u);
    CHECK(tile.source_range.end_frame == 1024u);
    CHECK(tile.first_column_index == 0u);
    CHECK(tile.column_count == 4u);
    CHECK(tile.state == APTA_FEATURE_FINAL);
    CHECK(tile.confidence == APTA_CONFIDENCE_UNKNOWN);
    CHECK(tile.columns != NULL);

    CHECK(apta_result_query_serialized_size(parsed, NULL, &parsed_size) ==
          APTA_STATUS_OK);
    CHECK(parsed_size == serialized_size);
    CHECK(apta_result_serialize(
              parsed,
              NULL,
              second,
              sizeof(second),
              &rewritten) == APTA_STATUS_OK);
    CHECK(rewritten == written);
    CHECK(memcmp(first, second, written) == 0);

    apta_result_release(parsed);
    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
