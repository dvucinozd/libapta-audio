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

static uint32_t reference_crc32c(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;

    for (index = 0u; index < size; ++index) {
        uint32_t bit;
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (UINT32_C(0x82F63B78) & mask);
        }
    }

    return crc ^ UINT32_C(0xFFFFFFFF);
}

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

static void refresh_header_crc(uint8_t *data)
{
    put_u32(data + 92u, reference_crc32c(data, 92u));
}

static void refresh_section_crc(uint8_t *data)
{
    uint8_t *directory = data + 96u;
    uint64_t section_offset = get_u64(directory + 8u);
    uint64_t section_size = get_u64(directory + 16u);

    put_u32(
        directory + 32u,
        reference_crc32c(
            data + (size_t)section_offset,
            (size_t)section_size));
}

static int create_valid_file(apta_context_t *context, uint8_t output[236 * APTA_TEST_WORKSPACE_SCALE])
{
    apta_session_config_t session_config;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_serialize_options_t options;
    int16_t pcm[2048] = {0};
    uint32_t accepted = 0u;
    size_t written = 0u;

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 2048u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_session_create(context, &session_config, &session) != APTA_STATUS_OK) {
        return 0;
    }

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 2048u;
    if (apta_session_push_pcm(session, &block, &accepted) != APTA_STATUS_OK ||
        accepted != 2048u ||
        apta_session_signal_end_of_input(session, 2048u) != APTA_STATUS_OK) {
        (void)apta_session_destroy(session);
        return 0;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 2048u;
    budget.maximum_steps = 8u;
    if (apta_session_process(session, &budget, NULL) != APTA_STATUS_END_OF_INPUT) {
        (void)apta_session_destroy(session);
        return 0;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        (void)apta_session_destroy(session);
        return 0;
    }

    apta_serialize_options_init(&options);
    if (apta_result_serialize(
            result,
            &options,
            output,
            236u,
            &written) != APTA_STATUS_OK ||
        written != 236u) {
        apta_result_release(result);
        (void)apta_session_destroy(session);
        return 0;
    }

    apta_result_release(result);
    return apta_session_destroy(session) == APTA_STATUS_OK;
}

static int expect_parse_status(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *data,
    size_t size,
    apta_status_t expected)
{
    const apta_result_t *result = NULL;
    apta_status_t status = apta_result_parse(
        context,
        options,
        data,
        size,
        &result);

    if (status != expected) {
        if (result != NULL) {
            apta_result_release(result);
        }
        fprintf(stderr, "expected status %d, received %d\n", expected, status);
        return 0;
    }

    if (expected < 0 && result != NULL) {
        apta_result_release(result);
        return 0;
    }
    if (expected == APTA_STATUS_OK) {
        if (result == NULL) {
            return 0;
        }
        apta_result_release(result);
    }
    return 1;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_parse_options_t options;
    uint8_t valid[236 * APTA_TEST_WORKSPACE_SCALE];
    uint8_t mutated[236];

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(create_valid_file(context, valid));

    apta_parse_options_init(&options);
    CHECK(expect_parse_status(
        context, &options, valid, 95u, APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    mutated[0] = 'X';
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    mutated[92] ^= 1u;
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    put_u64(mutated + 24u, 97u);
    refresh_header_crc(mutated);
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    mutated[96u + 32u] ^= 1u;
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    memcpy(mutated + 96u, "UNKN", 4u);
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_ERROR_UNSUPPORTED));

    memcpy(mutated, valid, sizeof(mutated));
    put_u32(mutated + 96u + 36u, 1u);
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_ERROR_CORRUPT_DATA));

    options.flags = 0u;
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_STATUS_OK));
    apta_parse_options_init(&options);

    memcpy(mutated, valid, sizeof(mutated));
    put_u64(mutated + 136u + 56u, 0u);
    refresh_section_crc(mutated);
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, sizeof(mutated));
    mutated[136u + 80u + 9u] |= 0x80u;
    refresh_section_crc(mutated);
    CHECK(expect_parse_status(
        context, &options, mutated, sizeof(mutated), APTA_ERROR_CORRUPT_DATA));

    options.maximum_waveform_columns = 1u;
    CHECK(expect_parse_status(
        context, &options, valid, sizeof(valid), APTA_ERROR_LIMIT_EXCEEDED));
    apta_parse_options_init(&options);

    options.maximum_allocation_bytes = 1u;
    CHECK(expect_parse_status(
        context, &options, valid, sizeof(valid), APTA_ERROR_LIMIT_EXCEEDED));

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    return 0;
}
