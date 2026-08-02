// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint16_t get_u16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8u));
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

static void put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8u);
    destination[2] = (uint8_t)(value >> 16u);
    destination[3] = (uint8_t)(value >> 24u);
}

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

static uint8_t *find_meta_directory(uint8_t *file)
{
    uint32_t count = get_u32(file + 20u);
    uint64_t directory_offset = get_u64(file + 24u);
    uint32_t index;

    for (index = 0u; index < count; ++index) {
        uint8_t *entry = file + (size_t)directory_offset +
                         (size_t)index * 40u;
        if (memcmp(entry, "META", 4u) == 0) {
            return entry;
        }
    }
    return NULL;
}

static void refresh_meta_crc(uint8_t *file)
{
    uint8_t *entry = find_meta_directory(file);
    uint64_t offset = get_u64(entry + 8u);
    uint64_t size = get_u64(entry + 16u);

    put_u32(
        entry + 32u,
        reference_crc32c(file + (size_t)offset, (size_t)size));
}

static int build_file(
    apta_context_t *context,
    uint8_t *output,
    size_t output_capacity,
    size_t *size_out,
    int empty)
{
    apta_session_config_t config;
    apta_session_t *session = NULL;
    apta_metadata_t metadata;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    int16_t pcm[1024] = {0};
    uint64_t required = 0u;
    size_t written = 0u;
    uint32_t accepted = 0u;
    int success = 0;

    apta_session_config_init(&config);
    config.source_sample_rate = 48000u;
    config.channel_count = 1u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.total_frames = 1024u;
    config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_session_create(context, &config, &session) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_metadata_init(&metadata);
    if (!empty) {
        metadata.flags =
            APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
            APTA_METADATA_FLAG_COMMENTS_PRESENT;
        metadata.producer_name.data = "A";
        metadata.producer_name.size = 1u;
        metadata.comments.data = "B";
        metadata.comments.size = 1u;
    }
    if (apta_session_set_metadata(session, &metadata) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 1024u;
    if (apta_session_push_pcm(session, &block, &accepted) != APTA_STATUS_OK ||
        accepted != 1024u ||
        apta_session_signal_end_of_input(session, 1024u) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    if (apta_session_process(session, &budget, NULL) !=
        APTA_STATUS_END_OF_INPUT) {
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL ||
        apta_result_query_serialized_size(result, NULL, &required) !=
            APTA_STATUS_OK ||
        required > output_capacity ||
        apta_result_serialize(
            result,
            NULL,
            output,
            output_capacity,
            &written) != APTA_STATUS_OK ||
        written != (size_t)required) {
        goto cleanup;
    }

    *size_out = written;
    success = 1;

cleanup:
    if (result != NULL) {
        apta_result_release(result);
    }
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    return success;
}

static int expect_status(
    apta_context_t *context,
    const uint8_t *file,
    size_t size,
    apta_status_t expected)
{
    const apta_result_t *result = NULL;
    apta_status_t status = apta_result_parse(
        context,
        NULL,
        file,
        size,
        &result);

    if (status != expected) {
        if (result != NULL) {
            apta_result_release(result);
        }
        return 0;
    }
    if (result != NULL) {
        apta_result_release(result);
    }
    return expected == APTA_STATUS_OK ? result != NULL : result == NULL;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    uint8_t valid[1024 * APTA_TEST_WORKSPACE_SCALE];
    uint8_t empty[1024];
    uint8_t mutated[1024];
    size_t valid_size = 0u;
    size_t empty_size = 0u;
    uint8_t *entry;
    uint8_t *payload;
    uint64_t payload_offset;
    const apta_result_t *result = NULL;
    apta_metadata_view_t view;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(build_file(context, valid, sizeof(valid), &valid_size, 0));
    CHECK(build_file(context, empty, sizeof(empty), &empty_size, 1));

    entry = find_meta_directory(valid);
    CHECK(entry != NULL);
    CHECK(get_u16(entry + 4u) == 1u);
    payload_offset = get_u64(entry + 8u);
    payload = valid + (size_t)payload_offset;
    CHECK(get_u64(entry + 16u) == 7u);
    CHECK(memcmp(payload, "\xA2\x01\x61" "A" "\x07\x61" "B", 7u) == 0);
    CHECK(expect_status(context, valid, valid_size, APTA_STATUS_OK));

    memcpy(mutated, valid, valid_size);
    payload = mutated + (size_t)payload_offset;
    payload[0] = 0xA1u;
    refresh_meta_crc(mutated);
    CHECK(expect_status(
        context,
        mutated,
        valid_size,
        APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, valid_size);
    payload = mutated + (size_t)payload_offset;
    payload[0] = 0xBFu;
    refresh_meta_crc(mutated);
    CHECK(expect_status(
        context,
        mutated,
        valid_size,
        APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, valid_size);
    payload = mutated + (size_t)payload_offset;
    payload[4] = 0x01u;
    refresh_meta_crc(mutated);
    CHECK(expect_status(
        context,
        mutated,
        valid_size,
        APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, valid_size);
    payload = mutated + (size_t)payload_offset;
    payload[2] = 0x41u;
    refresh_meta_crc(mutated);
    CHECK(expect_status(
        context,
        mutated,
        valid_size,
        APTA_ERROR_CORRUPT_DATA));

    memcpy(mutated, valid, valid_size);
    entry = find_meta_directory(mutated);
    put_u16(entry + 4u, 2u);
    CHECK(expect_status(
        context,
        mutated,
        valid_size,
        APTA_ERROR_UNSUPPORTED));

    memcpy(mutated, valid, valid_size);
    payload = mutated + (size_t)payload_offset;
    payload[4] = 0x08u;
    refresh_meta_crc(mutated);
    CHECK(apta_result_parse(
              context,
              NULL,
              mutated,
              valid_size,
              &result) == APTA_STATUS_OK);
    CHECK(result != NULL);
    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(result, &view) == APTA_STATUS_OK);
    CHECK((view.flags & APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT) != 0u);
    CHECK((view.flags & APTA_METADATA_FLAG_COMMENTS_PRESENT) == 0u);
    apta_result_release(result);
    result = NULL;

    CHECK(apta_result_parse(
              context,
              NULL,
              empty,
              empty_size,
              &result) == APTA_STATUS_OK);
    CHECK(result != NULL);
    apta_metadata_view_init(&view);
    CHECK(apta_result_get_metadata(result, &view) == APTA_STATUS_OK);
    CHECK(view.flags == 0u);
    CHECK(view.application_source_id_kind == APTA_METADATA_SOURCE_ID_NONE);
    apta_result_release(result);

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
