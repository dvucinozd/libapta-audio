// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

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
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_metadata_t metadata;
    apta_metadata_view_t metadata_view;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_waveform_tile_view_t tile;
    int16_t pcm[1024] = {0};
    uint8_t *first = NULL;
    uint8_t *second = NULL;
    uint64_t required = 0u;
    uint64_t repeated_required = 0u;
    size_t written = 0u;
    size_t repeated_written = 0u;
    uint32_t accepted = 0u;
    uint64_t wovr_offset;
    uint64_t wdtl_offset;
    uint64_t meta_offset;

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

    apta_metadata_init(&metadata);
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    metadata.producer_name.data = "combined";
    metadata.producer_name.size = 8u;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
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
    CHECK((apta_result_get_available_features(result) &
           APTA_FEATURE_WAVEFORM_DETAIL) != 0u);

    CHECK(apta_result_query_serialized_size(result, NULL, &required) ==
          APTA_STATUS_OK);
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
    CHECK(get_u32(first + 20u) == 3u);
    CHECK(memcmp(first + 96u, "WOVR", 4u) == 0);
    CHECK(memcmp(first + 136u, "WDTL", 4u) == 0);
    CHECK(memcmp(first + 176u, "META", 4u) == 0);

    wovr_offset = get_u64(first + 96u + 8u);
    wdtl_offset = get_u64(first + 136u + 8u);
    meta_offset = get_u64(first + 176u + 8u);
    CHECK((wovr_offset & 7u) == 0u);
    CHECK((wdtl_offset & 7u) == 0u);
    CHECK((meta_offset & 7u) == 0u);
    CHECK(wovr_offset >= 216u);
    CHECK(wdtl_offset > wovr_offset);
    CHECK(meta_offset > wdtl_offset);

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
    CHECK(tile.column_count == 4u);
    CHECK(tile.state == APTA_FEATURE_FINAL);

    apta_metadata_view_init(&metadata_view);
    CHECK(apta_result_get_metadata(parsed, &metadata_view) ==
          APTA_STATUS_OK);
    CHECK(metadata_view.producer_name.size == 8u);
    CHECK(memcmp(metadata_view.producer_name.data, "combined", 8u) == 0);

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
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
