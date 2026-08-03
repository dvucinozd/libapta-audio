// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_internal.h"

#define SAMPLE_RATE 48000u
#define BEAT_FRAMES 23040u
#define TOTAL_FRAMES 288000u
#define BLOCK_FRAMES 4096u
#define DIRECTORY_OFFSET 96u
#define DIRECTORY_ENTRY_SIZE 40u
#define CONTAINER_FLAG_PARTIAL_RESULT (1u << 0)
#define CONTAINER_FLAG_HAS_REQUIRED_EXTENSIONS (1u << 2)
#define SECTION_FLAG_REQUIRED (1u << 0)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static uint64_t get_u64(const uint8_t *p)
{
    return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4u) << 32u);
}

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static uint32_t crc32c(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t index;
    for (index = 0u; index < size; ++index) {
        uint32_t value = crc ^ data[index];
        uint32_t bit;
        for (bit = 0u; bit < 8u; ++bit) {
            value = (value >> 1u) ^
                    ((value & 1u) != 0u ? UINT32_C(0x82F63B78) : 0u);
        }
        crc = value;
    }
    return ~crc;
}

static uint8_t *find_entry(uint8_t *bytes, const char id[4])
{
    uint32_t count = get_u32(bytes + 20u);
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        uint8_t *entry = bytes + DIRECTORY_OFFSET +
                         (size_t)index * DIRECTORY_ENTRY_SIZE;
        if (memcmp(entry, id, 4u) == 0) {
            return entry;
        }
    }
    return NULL;
}

static void refresh_header_crc(uint8_t *bytes)
{
    put_u32(bytes + 92u, crc32c(bytes, 92u));
}

static void refresh_entry_crc(uint8_t *entry, uint8_t *bytes)
{
    const size_t offset = (size_t)get_u64(entry + 8u);
    const size_t size = (size_t)get_u64(entry + 16u);
    put_u32(entry + 32u, crc32c(bytes + offset, size));
}

static int16_t click_sample(uint64_t frame)
{
    return (frame % BEAT_FRAMES) < 128u ? (int16_t)30000 : 0;
}

static int build_result(
    apta_context_t *context,
    apta_session_t **session_out,
    const apta_result_t **result_out)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_GRID_LOCKING;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    apta_status_t status;

    apta_session_config_init(&config);
    config.source_sample_rate = SAMPLE_RATE;
    config.channel_count = 1u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.total_frames = TOTAL_FRAMES;
    config.requested_features = features;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;
    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t accepted = 0u;
        uint32_t index;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            samples[index] = click_sample(first + index);
        }
        apta_pcm_block_init(&block);
        block.data = samples;
        block.first_frame = first;
        block.frame_count = count;
        CHECK(apta_session_push_pcm(session, &block, &accepted) ==
              APTA_STATUS_OK);
        CHECK(accepted == count);
        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        first += count;
    }
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);

    *result_out = apta_session_acquire_result(session);
    CHECK(*result_out != NULL);
    *session_out = session;
    return 0;
}

static int verify_result(const apta_result_t *result)
{
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;
    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.tempo_millibpm >= 124500u);
    CHECK(tempo.selected.tempo_millibpm <= 125500u);
    CHECK(tempo.selected.state == APTA_FEATURE_FINAL);
    CHECK(tempo.candidate_count >= 1u);
    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.state == APTA_FEATURE_FINAL);
    CHECK(grid.segment_count == 1u);
    CHECK(grid.segments[0].nominal_tempo_millibpm ==
          tempo.selected.tempo_millibpm);
    return 0;
}

static int expect_rejected(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *bytes,
    size_t size)
{
    const apta_result_t *parsed = NULL;
    apta_status_t status = apta_result_parse(
        context,
        options,
        bytes,
        size,
        &parsed);
    CHECK(status == APTA_ERROR_CORRUPT_DATA ||
          status == APTA_ERROR_UNSUPPORTED);
    CHECK(parsed == NULL);
    return 0;
}

static int expect_accepted(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *bytes,
    size_t size,
    apta_session_state_t expected_state)
{
    const apta_result_t *parsed = NULL;
    apta_result_info_t info;
    CHECK(apta_result_parse(
              context,
              options,
              bytes,
              size,
              &parsed) == APTA_STATUS_OK);
    CHECK(parsed != NULL);
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(parsed, &info) == APTA_STATUS_OK);
    CHECK(info.session_state == expected_state);
    apta_result_release(parsed);
    return 0;
}

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_GRID_LOCKING;
    apta_context_config_t config;
    apta_context_t *context = NULL;
    apta_context_t *parse_context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    uint8_t *bytes = NULL;
    uint8_t *copy = NULL;
    uint8_t *roundtrip = NULL;
    uint64_t size64 = 0u;
    uint64_t roundtrip_size = 0u;
    size_t written = 0u;
    size_t roundtrip_written = 0u;
    size_t prefix;
    uint32_t relation;
    uint8_t *temp_entry;
    uint8_t *grid_entry;
    uint8_t *wovr_entry;
    apta_parse_options_t strict;
    apta_parse_options_t permissive;

    apta_context_config_init(&config);
    config.requested_capabilities = capabilities;
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    CHECK(apta_context_create(&config, &parse_context) == APTA_STATUS_OK);
    CHECK(build_result(context, &session, &result) == 0);
    CHECK(verify_result(result) == 0);

    apta_parse_options_init(&strict);
    strict.flags = APTA_PARSE_STRICT;
    apta_parse_options_init(&permissive);
    permissive.flags = 0u;

    CHECK(apta_result_query_serialized_size(result, NULL, &size64) ==
          APTA_STATUS_OK);
    CHECK(size64 > 0u && size64 <= SIZE_MAX);
    bytes = (uint8_t *)malloc((size_t)size64);
    copy = (uint8_t *)malloc((size_t)size64);
    CHECK(bytes != NULL && copy != NULL);
    CHECK(apta_result_serialize(
              result,
              NULL,
              bytes,
              (size_t)size64,
              &written) == APTA_STATUS_OK);
    CHECK(written == (size_t)size64);
    CHECK(find_entry(bytes, "TEMP") != NULL);
    CHECK(find_entry(bytes, "LGRD") != NULL);

    CHECK(apta_result_parse(
              parse_context,
              NULL,
              bytes,
              written,
              &parsed) == APTA_STATUS_OK);
    CHECK(parsed != NULL);
    CHECK(verify_result(parsed) == 0);
    CHECK(apta_result_query_serialized_size(parsed, NULL, &roundtrip_size) ==
          APTA_STATUS_OK);
    CHECK(roundtrip_size == size64);
    roundtrip = (uint8_t *)malloc((size_t)roundtrip_size);
    CHECK(roundtrip != NULL);
    CHECK(apta_result_serialize(
              parsed,
              NULL,
              roundtrip,
              (size_t)roundtrip_size,
              &roundtrip_written) == APTA_STATUS_OK);
    CHECK(roundtrip_written == written);
    CHECK(memcmp(bytes, roundtrip, written) == 0);
    apta_result_release(parsed);
    parsed = NULL;

    for (relation = APTA_TEMPO_RELATION_INDEPENDENT;
         relation <= APTA_TEMPO_RELATION_QUADRUPLE;
         ++relation) {
        apta_result_t *mutable_result = (apta_result_t *)(void *)result;
        apta_tempo_view_t relation_tempo;
        size_t relation_written = 0u;

        CHECK(mutable_result->tempo_candidates != NULL);
        mutable_result->tempo_candidates[0].relation_to_selected = relation;
        CHECK(apta_result_serialize(
                  result,
                  NULL,
                  copy,
                  (size_t)size64,
                  &relation_written) == APTA_STATUS_OK);
        CHECK(relation_written == written);
        CHECK(apta_result_parse(
                  parse_context,
                  NULL,
                  copy,
                  relation_written,
                  &parsed) == APTA_STATUS_OK);
        CHECK(parsed != NULL);
        apta_tempo_view_init(&relation_tempo);
        CHECK(apta_result_get_tempo(parsed, NULL, &relation_tempo) ==
              APTA_STATUS_OK);
        CHECK(relation_tempo.candidate_count >= 1u);
        CHECK(relation_tempo.candidates[0].relation_to_selected == relation);
        apta_result_release(parsed);
        parsed = NULL;
    }
    ((apta_result_t *)(void *)result)->tempo_candidates[0].relation_to_selected =
        APTA_TEMPO_RELATION_INDEPENDENT;

    for (prefix = 0u; prefix < written; ++prefix) {
        const apta_result_t *truncated = NULL;
        CHECK(apta_result_parse(
                  parse_context,
                  NULL,
                  bytes,
                  prefix,
                  &truncated) < 0);
        CHECK(truncated == NULL);
    }

    memcpy(copy, bytes, written);
    put_u32(
        copy + 16u,
        get_u32(copy + 16u) | CONTAINER_FLAG_HAS_REQUIRED_EXTENSIONS);
    refresh_header_crc(copy);
    CHECK(expect_accepted(
              parse_context,
              &strict,
              copy,
              written,
              APTA_SESSION_COMPLETED) == 0);

    memcpy(copy, bytes, written);
    put_u32(copy + 16u, get_u32(copy + 16u) | CONTAINER_FLAG_PARTIAL_RESULT);
    refresh_header_crc(copy);
    CHECK(expect_accepted(
              parse_context,
              &strict,
              copy,
              written,
              APTA_SESSION_ACTIVE) == 0);

    memcpy(copy, bytes, written);
    wovr_entry = find_entry(copy, "WOVR");
    CHECK(wovr_entry != NULL);
    put_u16(wovr_entry + 6u, 0u);
    CHECK(expect_rejected(parse_context, &strict, copy, written) == 0);

    memcpy(copy, bytes, written);
    temp_entry = find_entry(copy, "TEMP");
    CHECK(temp_entry != NULL);
    put_u16(temp_entry + 6u, SECTION_FLAG_REQUIRED);
    CHECK(expect_rejected(parse_context, &strict, copy, written) == 0);

    memcpy(copy, bytes, written);
    temp_entry = find_entry(copy, "TEMP");
    CHECK(temp_entry != NULL);
    {
        uint8_t *payload = copy + (size_t)get_u64(temp_entry + 8u);
        put_u32(payload + 52u, 1u);
        refresh_entry_crc(temp_entry, copy);
    }
    CHECK(expect_rejected(parse_context, &strict, copy, written) == 0);
    CHECK(expect_accepted(
              parse_context,
              &permissive,
              copy,
              written,
              APTA_SESSION_COMPLETED) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "LGRD");
    CHECK(grid_entry != NULL);
    {
        uint8_t *payload = copy + (size_t)get_u64(grid_entry + 8u);
        put_u32(payload + 92u, 1u);
        refresh_entry_crc(grid_entry, copy);
    }
    CHECK(expect_rejected(parse_context, &strict, copy, written) == 0);
    CHECK(expect_accepted(
              parse_context,
              &permissive,
              copy,
              written,
              APTA_SESSION_COMPLETED) == 0);

    memcpy(copy, bytes, written);
    temp_entry = find_entry(copy, "TEMP");
    CHECK(temp_entry != NULL);
    {
        uint8_t *payload = copy + (size_t)get_u64(temp_entry + 8u);
        payload[2] = APTA_FEATURE_PROVISIONAL;
        refresh_entry_crc(temp_entry, copy);
    }
    CHECK(expect_rejected(parse_context, &strict, copy, written) == 0);
    put_u32(copy + 16u, get_u32(copy + 16u) | CONTAINER_FLAG_PARTIAL_RESULT);
    refresh_header_crc(copy);
    CHECK(expect_accepted(
              parse_context,
              &strict,
              copy,
              written,
              APTA_SESSION_ACTIVE) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "LGRD");
    CHECK(grid_entry != NULL);
    {
        uint8_t *payload = copy + (size_t)get_u64(grid_entry + 8u);
        payload[2] = APTA_FEATURE_STABLE;
        payload[136] = APTA_FEATURE_STABLE;
        refresh_entry_crc(grid_entry, copy);
    }
    CHECK(expect_rejected(parse_context, &strict, copy, written) == 0);
    put_u32(copy + 16u, get_u32(copy + 16u) | CONTAINER_FLAG_PARTIAL_RESULT);
    refresh_header_crc(copy);
    CHECK(expect_accepted(
              parse_context,
              &strict,
              copy,
              written,
              APTA_SESSION_ACTIVE) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "LGRD");
    CHECK(grid_entry != NULL);
    {
        uint8_t *payload = copy + (size_t)get_u64(grid_entry + 8u);
        put_u32(payload + 120u, 126000u);
        refresh_entry_crc(grid_entry, copy);
    }
    CHECK(expect_rejected(parse_context, &strict, copy, written) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "LGRD");
    CHECK(grid_entry != NULL);
    memcpy(grid_entry, "TEMP", 4u);
    CHECK(expect_rejected(parse_context, &strict, copy, written) == 0);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(verify_result(result) == 0);
    apta_result_release(result);
    result = NULL;
    free(roundtrip);
    free(copy);
    free(bytes);
    CHECK(apta_context_destroy(parse_context) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
