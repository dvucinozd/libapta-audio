// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define VALID_SIZE (376u * APTA_TEST_WORKSPACE_SCALE)
#define WDTL_DIRECTORY_OFFSET 136u
#define WDTL_PAYLOAD_OFFSET 272u
#define WDTL_PAYLOAD_SIZE 104u

static void put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFu);
    destination[1] = (uint8_t)((value >> 8u) & 0xFFu);
    destination[2] = (uint8_t)((value >> 16u) & 0xFFu);
    destination[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void put_u64(uint8_t *destination, uint64_t value)
{
    put_u32(destination, (uint32_t)(value & UINT32_MAX));
    put_u32(destination + 4u, (uint32_t)(value >> 32u));
}

static uint32_t crc32c(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t index;

    for (index = 0u; index < size; ++index) {
        uint32_t byte = data[index];
        uint32_t bit;

        crc ^= byte;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^
                  ((crc & 1u) != 0u ? UINT32_C(0x82F63B78) : 0u);
        }
    }
    return crc ^ UINT32_MAX;
}

static void refresh_wdtl_crc(uint8_t bytes[VALID_SIZE])
{
    put_u32(
        bytes + WDTL_DIRECTORY_OFFSET + 32u,
        crc32c(bytes + WDTL_PAYLOAD_OFFSET, WDTL_PAYLOAD_SIZE));
}

static int build_valid(
    apta_context_t *context,
    uint8_t bytes[VALID_SIZE])
{
    apta_session_config_t session_config;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    int16_t pcm[1024] = {0};
    uint64_t size = 0u;
    size_t written = 0u;
    uint32_t accepted = 0u;

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    if (apta_session_create(context, &session_config, &session) !=
        APTA_STATUS_OK) {
        return 0;
    }

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 1024u;
    if (apta_session_push_pcm(session, &block, &accepted) != APTA_STATUS_OK ||
        accepted != 1024u ||
        apta_session_signal_end_of_input(session, 1024u) != APTA_STATUS_OK) {
        (void)apta_session_destroy(session);
        return 0;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    if (apta_session_process(session, &budget, NULL) !=
        APTA_STATUS_END_OF_INPUT) {
        (void)apta_session_destroy(session);
        return 0;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL ||
        apta_result_query_serialized_size(result, NULL, &size) !=
            APTA_STATUS_OK ||
        size != VALID_SIZE ||
        apta_result_serialize(
            result,
            NULL,
            bytes,
            VALID_SIZE,
            &written) != APTA_STATUS_OK ||
        written != VALID_SIZE) {
        apta_result_release(result);
        (void)apta_session_destroy(session);
        return 0;
    }

    apta_result_release(result);
    return apta_session_destroy(session) == APTA_STATUS_OK;
}

static int expect_status(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *bytes,
    apta_status_t expected)
{
    const apta_result_t *result = NULL;
    apta_status_t status = apta_result_parse(
        context,
        options,
        bytes,
        VALID_SIZE,
        &result);

    if (result != NULL) {
        apta_result_release(result);
    }
    return status == expected && result == NULL;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_parse_options_t options;
    const apta_result_t *overview_only = NULL;
    uint8_t valid[VALID_SIZE];
    uint8_t mutated[VALID_SIZE];

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(build_valid(context, valid));

    memcpy(mutated, valid, sizeof(mutated));
    mutated[WDTL_PAYLOAD_OFFSET + 63u] = 1u;
    refresh_wdtl_crc(mutated);
    CHECK(expect_status(context, NULL, mutated, APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    put_u64(mutated + WDTL_PAYLOAD_OFFSET + 48u, 0u);
    refresh_wdtl_crc(mutated);
    CHECK(expect_status(context, NULL, mutated, APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    put_u32(mutated + WDTL_PAYLOAD_OFFSET + 56u, APTA_FEATURE_ABSENT);
    refresh_wdtl_crc(mutated);
    CHECK(expect_status(context, NULL, mutated, APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    put_u16(mutated + WDTL_PAYLOAD_OFFSET + 64u, 1u);
    put_u16(mutated + WDTL_PAYLOAD_OFFSET + 66u, 0u);
    refresh_wdtl_crc(mutated);
    CHECK(expect_status(context, NULL, mutated, APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    put_u32(mutated + WDTL_PAYLOAD_OFFSET + 16u, 2u);
    refresh_wdtl_crc(mutated);
    CHECK(expect_status(context, NULL, mutated, APTA_ERROR_UNSUPPORTED));

    memcpy(mutated, valid, sizeof(mutated));
    put_u32(mutated + WDTL_PAYLOAD_OFFSET + 0u, 2u);
    refresh_wdtl_crc(mutated);
    CHECK(expect_status(context, NULL, mutated, APTA_ERROR_CORRUPT_DATA));

    apta_parse_options_init(&options);
    options.maximum_waveform_columns = 3u;
    CHECK(expect_status(context, &options, valid, APTA_ERROR_LIMIT_EXCEEDED));

    memcpy(mutated, valid, sizeof(mutated));
    memcpy(mutated + WDTL_DIRECTORY_OFFSET, "XDTL", 4u);
    CHECK(apta_result_parse(
              context,
              NULL,
              mutated,
              sizeof(mutated),
              &overview_only) == APTA_STATUS_OK);
    CHECK(overview_only != NULL);
    CHECK(apta_result_get_available_features(overview_only) ==
          APTA_FEATURE_WAVEFORM_OVERVIEW);
    apta_result_release(overview_only);

    memcpy(mutated, valid, sizeof(mutated));
    mutated[WDTL_PAYLOAD_OFFSET + 64u] ^= 1u;
    CHECK(expect_status(context, NULL, mutated, APTA_ERROR_CORRUPT_DATA));

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
