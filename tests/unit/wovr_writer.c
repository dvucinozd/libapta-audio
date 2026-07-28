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

int main(void)
{
    static const uint8_t crc_vector[] = "123456789";
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_serialize_options_t options;
    int16_t pcm[2048] = {0};
    uint8_t output[236];
    uint8_t small_output[235];
    uint64_t serialized_size = 0u;
    size_t bytes_written = 0u;
    uint32_t accepted = 0u;
    uint32_t index;
    const uint8_t *directory;
    const uint8_t *payload;

    CHECK(reference_crc32c(crc_vector, 9u) == UINT32_C(0xE3069283));

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 2048u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 2048u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 2048u);
    CHECK(apta_session_signal_end_of_input(session, 2048u) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 2048u;
    budget.maximum_steps = 8u;
    CHECK(apta_session_process(session, &budget, NULL) == APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(apta_result_get_available_features(result) ==
          APTA_FEATURE_WAVEFORM_OVERVIEW);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    apta_serialize_options_init(&options);
    CHECK(apta_result_query_serialized_size(result, &options, &serialized_size) ==
          APTA_STATUS_OK);
    CHECK(serialized_size == sizeof(output));

    options.maximum_output_bytes = sizeof(output) - 1u;
    serialized_size = UINT64_MAX;
    CHECK(apta_result_query_serialized_size(result, &options, &serialized_size) ==
          APTA_ERROR_LIMIT_EXCEEDED);
    CHECK(serialized_size == 0u);
    options.maximum_output_bytes = 0u;

    bytes_written = SIZE_MAX;
    CHECK(apta_result_serialize(
              result,
              &options,
              small_output,
              sizeof(small_output),
              &bytes_written) == APTA_ERROR_BUFFER_TOO_SMALL);
    CHECK(bytes_written == 0u);

    memset(output, 0xA5, sizeof(output));
    CHECK(apta_result_serialize(
              result,
              &options,
              output,
              sizeof(output),
              &bytes_written) == APTA_STATUS_OK);
    CHECK(bytes_written == sizeof(output));

    CHECK(memcmp(output, "APTA", 4u) == 0);
    CHECK(get_u16(output + 4u) == 96u);
    CHECK(get_u16(output + 6u) == 1u);
    CHECK(get_u16(output + 8u) == APTA_SPEC_VERSION_MAJOR);
    CHECK(get_u16(output + 10u) == APTA_SPEC_VERSION_MINOR);
    CHECK(get_u32(output + 12u) == APTA_API_VERSION);
    CHECK(get_u32(output + 16u) == 0u);
    CHECK(get_u32(output + 20u) == 1u);
    CHECK(get_u64(output + 24u) == 96u);
    CHECK(get_u64(output + 32u) == sizeof(output));
    CHECK(get_u64(output + 40u) == 2048u);
    CHECK(get_u32(output + 48u) == 48000u);
    CHECK(get_u16(output + 52u) == 1u);
    CHECK(get_u16(output + 54u) == APTA_CHANNEL_LAYOUT_MONO);
    for (index = 56u; index < 92u; ++index) {
        CHECK(output[index] == 0u);
    }
    CHECK(get_u32(output + 92u) == reference_crc32c(output, 92u));

    directory = output + 96u;
    CHECK(memcmp(directory, "WOVR", 4u) == 0);
    CHECK(get_u16(directory + 4u) == 1u);
    CHECK(get_u16(directory + 6u) == 1u);
    CHECK(get_u64(directory + 8u) == 136u);
    CHECK(get_u64(directory + 16u) == 100u);
    CHECK(get_u64(directory + 24u) == 100u);
    CHECK(get_u32(directory + 36u) == 0u);

    payload = output + 136u;
    CHECK(get_u32(directory + 32u) == reference_crc32c(payload, 100u));
    CHECK(get_u32(payload + 0u) == 0u);
    CHECK(get_u32(payload + 4u) == 1024u);
    CHECK(get_u64(payload + 8u) == 0u);
    CHECK(get_u32(payload + 16u) == 2u);
    CHECK(get_u32(payload + 20u) == 1u);
    CHECK(get_u64(payload + 24u) == 48u);
    CHECK(get_u64(payload + 32u) == 80u);
    CHECK(get_u32(payload + 40u) == APTA_FEATURE_FINAL);
    CHECK(get_u32(payload + 44u) == 0u);

    CHECK(get_u64(payload + 48u) == 0u);
    CHECK(get_u64(payload + 56u) == 2048u);
    CHECK(get_u32(payload + 64u) == 0u);
    CHECK(get_u32(payload + 68u) == 2u);
    CHECK(get_u32(payload + 72u) == 0u);
    CHECK(get_u32(payload + 76u) == 0u);

    for (index = 0u; index < 2u; ++index) {
        const uint8_t *column = payload + 80u + index * 10u;
        CHECK(get_u16(column + 0u) == 0u);
        CHECK(get_u16(column + 2u) == 0u);
        CHECK(get_u16(column + 4u) == 0u);
        CHECK(column[6] == 0u);
        CHECK(column[7] == 0u);
        CHECK(column[8] == 0u);
        CHECK(column[9] == APTA_WAVEFORM_COLUMN_VALID);
    }

    apta_result_release(result);
    result = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    return 0;
}