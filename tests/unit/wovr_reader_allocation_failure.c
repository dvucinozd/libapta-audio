// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#define FILE_SIZE 226u
#define PAYLOAD_OFFSET 136u
#define PAYLOAD_SIZE 90u

typedef struct {
    uint32_t allocation_call;
    uint32_t fail_at_call;
    uint32_t outstanding;
} parser_allocator_state_t;

static void *APTA_CALL parser_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    parser_allocator_state_t *state =
        (parser_allocator_state_t *)user_data;
    void *memory;

    (void)alignment;
    (void)flags;

    state->allocation_call += 1u;
    if (state->fail_at_call != 0u &&
        state->allocation_call == state->fail_at_call) {
        return NULL;
    }

    memory = malloc(size);
    if (memory != NULL) {
        state->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL parser_deallocate(void *user_data, void *memory)
{
    parser_allocator_state_t *state =
        (parser_allocator_state_t *)user_data;

    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

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

static void build_valid_file(uint8_t output[FILE_SIZE])
{
    uint8_t *directory;
    uint8_t *payload;
    uint8_t *span;

    memset(output, 0, FILE_SIZE);

    output[0] = 'A';
    output[1] = 'P';
    output[2] = 'T';
    output[3] = 'A';
    put_u16(output + 4u, 96u);
    put_u16(output + 6u, 1u);
    put_u16(output + 8u, (uint16_t)APTA_SPEC_VERSION_MAJOR);
    put_u16(output + 10u, (uint16_t)APTA_SPEC_VERSION_MINOR);
    put_u32(output + 12u, APTA_API_VERSION);
    put_u32(output + 20u, 1u);
    put_u64(output + 24u, 96u);
    put_u64(output + 32u, FILE_SIZE);
    put_u64(output + 40u, 1024u);
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
    put_u32(payload + 4u, 1024u);
    put_u32(payload + 16u, 1u);
    put_u32(payload + 20u, 1u);
    put_u64(payload + 24u, 48u);
    put_u64(payload + 32u, 80u);
    put_u32(payload + 40u, APTA_FEATURE_FINAL);

    span = payload + 48u;
    put_u64(span + 8u, 1024u);
    put_u32(span + 20u, 1u);
    payload[80u + 9u] = APTA_WAVEFORM_COLUMN_VALID;

    put_u32(directory + 32u, reference_crc32c(payload, PAYLOAD_SIZE));
    put_u32(output + 92u, reference_crc32c(output, 92u));
}

static int run_case(
    const uint8_t file[FILE_SIZE],
    uint32_t fail_at_call,
    apta_status_t expected_status)
{
    parser_allocator_state_t state = {0u, fail_at_call, 0u};
    apta_context_config_t context_config;
    apta_parse_options_t parse_options;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_status_t status;

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &state;
    context_config.allocator.allocate = parser_allocate;
    context_config.allocator.deallocate = parser_deallocate;
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;

    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        return 0;
    }

    apta_parse_options_init(&parse_options);
    status = apta_result_parse(
        context,
        &parse_options,
        file,
        FILE_SIZE,
        &result);

    if (status != expected_status) {
        if (result != NULL) {
            apta_result_release(result);
        }
        (void)apta_context_destroy(context);
        return 0;
    }

    if (expected_status == APTA_STATUS_OK) {
        if (result == NULL) {
            (void)apta_context_destroy(context);
            return 0;
        }
        apta_result_release(result);
    } else if (result != NULL) {
        apta_result_release(result);
        (void)apta_context_destroy(context);
        return 0;
    }

    if (apta_context_destroy(context) != APTA_STATUS_OK) {
        return 0;
    }
    return state.outstanding == 0u;
}

int main(void)
{
    uint8_t file[FILE_SIZE];
    uint32_t fail_at;

    build_valid_file(file);

    CHECK(run_case(file, 0u, APTA_STATUS_OK));

    /* Call 1 creates the context; calls 2..5 are parser-owned allocations. */
    for (fail_at = 2u; fail_at <= 5u; ++fail_at) {
        CHECK(run_case(file, fail_at, APTA_ERROR_OUT_OF_MEMORY));
    }

    return 0;
}
