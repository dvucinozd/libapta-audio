// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_internal.h"

#define SAMPLE_RATE 48000u
#define TOTAL_FRAMES 524288u
#define SPLIT_FRAME (TOTAL_FRAMES / 2u)
#define BLOCK_FRAMES 4096u
#define TEMPO_12_BIN 117188u
#define TEMPO_9_BIN 156250u
#define DIRECTORY_OFFSET 96u
#define DIRECTORY_ENTRY_SIZE 40u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

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

static uint8_t *find_entry(uint8_t *bytes, const char id[4], uint32_t *index_out)
{
    const uint32_t count = get_u32(bytes + 20u);
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        uint8_t *entry = bytes + DIRECTORY_OFFSET +
                         (size_t)index * DIRECTORY_ENTRY_SIZE;
        if (memcmp(entry, id, 4u) == 0) {
            if (index_out != NULL) {
                *index_out = index;
            }
            return entry;
        }
    }
    return NULL;
}

static uint32_t beat_period(uint32_t tempo_millibpm)
{
    return (uint32_t)(((uint64_t)SAMPLE_RATE * UINT64_C(60000) +
                       tempo_millibpm / 2u) /
                      tempo_millibpm);
}

static int build_result(
    apta_context_t *context,
    apta_session_t **session_out,
    const apta_result_t **result_out)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    const uint32_t first_period = beat_period(TEMPO_12_BIN);
    const uint32_t second_period = beat_period(TEMPO_9_BIN);
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
    budget.maximum_steps = 64u;
    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t accepted = 0u;
        uint32_t index;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            const uint32_t frame = first + index;
            const uint32_t period = frame < SPLIT_FRAME
                                        ? first_period
                                        : second_period;
            const uint32_t origin = frame < SPLIT_FRAME ? 0u : SPLIT_FRAME;
            samples[index] = ((frame - origin) % period) < 192u
                                 ? (int16_t)30000
                                 : 0;
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
    apta_grid_view_t grid;
    apta_grid_revision_view_t revision;
    uint32_t index;

    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_GLOBAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.state == APTA_FEATURE_FINAL);
    CHECK(grid.representation == APTA_GRID_REPRESENTATION_HYBRID);
    CHECK(grid.segment_count >= 2u);
    CHECK(grid.segment_count <= APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS);
    CHECK(grid.beat_count != 0u);
    CHECK(grid.beat_count <= APTA_REFERENCE_GLOBAL_GRID_MAX_BEATS);
    CHECK((grid.flags & APTA_GRID_FLAG_DYNAMIC_TEMPO) != 0u);
    for (index = 1u; index < grid.beat_count; ++index) {
        CHECK(grid.beats[index - 1u].position.whole_frame <=
              grid.beats[index].position.whole_frame);
        CHECK(grid.beats[index - 1u].ordinal < grid.beats[index].ordinal);
    }

    apta_grid_revision_view_init(&revision);
    CHECK(apta_result_get_grid_revision(result, &revision) == APTA_STATUS_OK);
    CHECK(revision.state == APTA_GRID_REVISION_APPLIED);
    CHECK(revision.revision_id != 0u);
    CHECK(revision.proposed_representation == grid.representation);
    CHECK(revision.proposed_segment_count == grid.segment_count);
    CHECK(revision.proposed_beat_count == grid.beat_count);
    return 0;
}

static int expect_rejected(
    apta_context_t *context,
    const uint8_t *bytes,
    size_t size)
{
    const apta_result_t *parsed = NULL;
    const apta_status_t status = apta_result_parse(
        context,
        NULL,
        bytes,
        size,
        &parsed);
    CHECK(status == APTA_ERROR_CORRUPT_DATA ||
          status == APTA_ERROR_UNSUPPORTED ||
          status == APTA_ERROR_LIMIT_EXCEEDED);
    CHECK(parsed == NULL);
    return 0;
}

static void refresh_entry_crc(uint8_t *entry, uint8_t *file)
{
    const size_t offset = (size_t)get_u64(entry + 8u);
    const size_t size = (size_t)get_u64(entry + 16u);
    put_u32(entry + 32u, crc32c(file + offset, size));
}

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE;
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
    uint64_t exact_allocation_size = 0u;
    size_t written = 0u;
    size_t roundtrip_written = 0u;
    size_t prefix;
    uint8_t *grid_entry;
    uint8_t *revision_entry;
    uint32_t grid_index = 0u;
    uint32_t revision_index = 0u;
    apta_parse_options_t strict;

    apta_context_config_init(&config);
    config.requested_capabilities = capabilities;
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    CHECK(apta_context_create(&config, &parse_context) == APTA_STATUS_OK);
    CHECK(build_result(context, &session, &result) == 0);
    CHECK(verify_result(result) == 0);

    CHECK(apta_result_query_serialized_size(result, NULL, &size64) ==
          APTA_STATUS_OK);
    CHECK(size64 > 0u && size64 <= SIZE_MAX);
    bytes = (uint8_t *)malloc((size_t)size64 + 1u);
    copy = (uint8_t *)malloc((size_t)size64 + 1u);
    CHECK(bytes != NULL && copy != NULL);
    CHECK(apta_result_serialize(
              result,
              NULL,
              bytes,
              (size_t)size64,
              &written) == APTA_STATUS_OK);
    CHECK(written == (size_t)size64);
    grid_entry = find_entry(bytes, "GGRD", &grid_index);
    revision_entry = find_entry(bytes, "REVN", &revision_index);
    CHECK(grid_entry != NULL && revision_entry != NULL);
    CHECK(get_u16(grid_entry + 4u) == 1u);
    CHECK(get_u16(revision_entry + 4u) == 1u);
    CHECK(revision_index == grid_index + 1u);
    CHECK(revision_index + 1u == get_u32(bytes + 20u));

    apta_parse_options_init(&strict);
    strict.flags = APTA_PARSE_STRICT;
    CHECK(apta_result_parse(
              parse_context,
              &strict,
              bytes,
              written,
              &parsed) == APTA_STATUS_OK);
    CHECK(parsed != NULL);
    CHECK(verify_result(parsed) == 0);
    CHECK(apta_internal_result_allocation_bytes(
              parsed,
              &exact_allocation_size));
    CHECK(exact_allocation_size > 1u);
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

    {
        apta_parse_options_t limited;
        apta_parse_options_init(&limited);
        limited.maximum_allocation_bytes = exact_allocation_size - 1u;
        CHECK(apta_result_parse(
                  parse_context,
                  &limited,
                  bytes,
                  written,
                  &parsed) == APTA_ERROR_LIMIT_EXCEEDED);
        CHECK(parsed == NULL);

        limited.maximum_allocation_bytes = exact_allocation_size;
        CHECK(apta_result_parse(
                  parse_context,
                  &limited,
                  bytes,
                  written,
                  &parsed) == APTA_STATUS_OK);
        CHECK(parsed != NULL);
        apta_result_release(parsed);
        parsed = NULL;
    }

    for (prefix = 0u; prefix < written; ++prefix) {
        const apta_result_t *truncated = NULL;
        CHECK(apta_result_parse(
                  parse_context,
                  &strict,
                  bytes,
                  prefix,
                  &truncated) < 0);
        CHECK(truncated == NULL);
    }
    memcpy(copy, bytes, written);
    copy[written] = 0u;
    CHECK(expect_rejected(parse_context, copy, written + 1u) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "GGRD", NULL);
    revision_entry = find_entry(copy, "REVN", NULL);
    CHECK(grid_entry != NULL && revision_entry != NULL);
    memcpy(revision_entry, "GGRD", 4u);
    CHECK(expect_rejected(parse_context, copy, written) == 0);

    memcpy(copy, bytes, written);
    revision_entry = find_entry(copy, "REVN", NULL);
    CHECK(revision_entry != NULL);
    memcpy(revision_entry, "UNKN", 4u);
    CHECK(expect_rejected(parse_context, copy, written) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "GGRD", NULL);
    CHECK(grid_entry != NULL);
    put_u16(grid_entry + 4u, 2u);
    CHECK(expect_rejected(parse_context, copy, written) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "GGRD", NULL);
    CHECK(grid_entry != NULL);
    {
        uint8_t *payload = copy + (size_t)get_u64(grid_entry + 8u);
        payload[88] = 1u;
        refresh_entry_crc(grid_entry, copy);
    }
    CHECK(expect_rejected(parse_context, copy, written) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "GGRD", NULL);
    CHECK(grid_entry != NULL);
    {
        uint8_t *payload = copy + (size_t)get_u64(grid_entry + 8u);
        put_u32(payload + 16u, APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS + 1u);
        refresh_entry_crc(grid_entry, copy);
    }
    CHECK(expect_rejected(parse_context, copy, written) == 0);

    memcpy(copy, bytes, written);
    revision_entry = find_entry(copy, "REVN", NULL);
    CHECK(revision_entry != NULL);
    {
        uint8_t *payload = copy + (size_t)get_u64(revision_entry + 8u);
        put_u32(payload + 8u, get_u32(payload + 8u) + 1u);
        refresh_entry_crc(revision_entry, copy);
    }
    CHECK(expect_rejected(parse_context, copy, written) == 0);

    memcpy(copy, bytes, written);
    grid_entry = find_entry(copy, "GGRD", NULL);
    revision_entry = find_entry(copy, "REVN", NULL);
    CHECK(grid_entry != NULL && revision_entry != NULL);
    memcpy(grid_entry, "REVN", 4u);
    memcpy(revision_entry, "GGRD", 4u);
    CHECK(expect_rejected(parse_context, copy, written) == 0);

    {
        apta_parse_options_t limited;
        const apta_result_t *limited_result = NULL;
        apta_parse_options_init(&limited);
        limited.maximum_allocation_bytes = 1u;
        CHECK(apta_result_parse(
                  parse_context,
                  &limited,
                  bytes,
                  written,
                  &limited_result) == APTA_ERROR_LIMIT_EXCEEDED);
        CHECK(limited_result == NULL);
    }

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
