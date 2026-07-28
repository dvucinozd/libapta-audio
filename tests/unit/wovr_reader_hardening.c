// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define FILE_SIZE 268u
#define PAYLOAD_OFFSET 136u
#define PAYLOAD_SIZE 132u

typedef enum {
    APTA_TEST_VALID_TWO_SPAN = 0,
    APTA_TEST_FINAL_GAP = 1,
    APTA_TEST_DUPLICATE_PACKED_DATA = 2
} apta_hardening_case_t;

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

static void build_two_span_file(
    uint8_t output[FILE_SIZE],
    apta_hardening_case_t test_case)
{
    uint8_t *directory;
    uint8_t *payload;
    uint8_t *first_span;
    uint8_t *second_span;
    uint64_t total_frames;
    uint32_t logical_columns;
    uint32_t second_first_column;
    uint32_t second_data_offset;

    memset(output, 0, FILE_SIZE);

    total_frames = test_case == APTA_TEST_FINAL_GAP ? 3072u : 2048u;
    logical_columns = test_case == APTA_TEST_FINAL_GAP ? 3u : 2u;
    second_first_column = test_case == APTA_TEST_FINAL_GAP ? 2u : 1u;
    second_data_offset =
        test_case == APTA_TEST_DUPLICATE_PACKED_DATA ? 0u : 1u;

    output[0] = 'A';
    output[1] = 'P';
    output[2] = 'T';
    output[3] = 'A';
    put_u16(output + 4u, 96u);
    put_u16(output + 6u, 1u);
    put_u16(output + 8u, (uint16_t)APTA_SPEC_VERSION_MAJOR);
    put_u16(output + 10u, (uint16_t)APTA_SPEC_VERSION_MINOR);
    put_u32(output + 12u, APTA_API_VERSION);
    put_u32(output + 16u, 0u);
    put_u32(output + 20u, 1u);
    put_u64(output + 24u, 96u);
    put_u64(output + 32u, FILE_SIZE);
    put_u64(output + 40u, total_frames);
    put_u32(output + 48u, 48000u);
    put_u16(output + 52u, 1u);
    put_u16(output + 54u, APTA_CHANNEL_LAYOUT_MONO);

    directory = output + 96u;
    directory[0] = 'W';
    directory[1] = 'O';
    directory[2] = 'V';
    directory[3] = 'R';
    put_u16(directory + 4u, 1u);
    put_u16(directory + 6u, 1u);
    put_u64(directory + 8u, PAYLOAD_OFFSET);
    put_u64(directory + 16u, PAYLOAD_SIZE);
    put_u64(directory + 24u, PAYLOAD_SIZE);

    payload = output + PAYLOAD_OFFSET;
    put_u32(payload + 0u, 0u);
    put_u32(payload + 4u, 1024u);
    put_u64(payload + 8u, 0u);
    put_u32(payload + 16u, logical_columns);
    put_u32(payload + 20u, 2u);
    put_u64(payload + 24u, 48u);
    put_u64(payload + 32u, 112u);
    put_u32(payload + 40u, APTA_FEATURE_FINAL);

    first_span = payload + 48u;
    put_u64(first_span + 0u, 0u);
    put_u64(first_span + 8u, 1024u);
    put_u32(first_span + 16u, 0u);
    put_u32(first_span + 20u, 1u);
    put_u32(first_span + 24u, 0u);

    second_span = payload + 80u;
    put_u64(second_span + 0u, (uint64_t)second_first_column * 1024u);
    put_u64(second_span + 8u, total_frames);
    put_u32(second_span + 16u, second_first_column);
    put_u32(second_span + 20u, 1u);
    put_u32(second_span + 24u, second_data_offset);

    payload[112u + 9u] = APTA_WAVEFORM_COLUMN_VALID;
    payload[122u + 9u] = APTA_WAVEFORM_COLUMN_VALID;

    put_u32(directory + 32u, reference_crc32c(payload, PAYLOAD_SIZE));
    put_u32(output + 92u, reference_crc32c(output, 92u));
}

static apta_status_t parse_case(
    apta_context_t *context,
    const uint8_t file[FILE_SIZE])
{
    apta_parse_options_t options;
    const apta_result_t *result = NULL;
    apta_status_t status;

    apta_parse_options_init(&options);
    status = apta_result_parse(
        context,
        &options,
        file,
        FILE_SIZE,
        &result);
    if (result != NULL) {
        apta_result_release(result);
    }
    return status;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    uint8_t file[FILE_SIZE];

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    build_two_span_file(file, APTA_TEST_VALID_TWO_SPAN);
    CHECK(parse_case(context, file) == APTA_STATUS_OK);

    build_two_span_file(file, APTA_TEST_FINAL_GAP);
    CHECK(parse_case(context, file) == APTA_ERROR_CORRUPT_DATA);

    build_two_span_file(file, APTA_TEST_DUPLICATE_PACKED_DATA);
    CHECK(parse_case(context, file) == APTA_ERROR_CORRUPT_DATA);

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
