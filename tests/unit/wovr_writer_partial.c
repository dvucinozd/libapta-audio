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

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_serialize_options_t options;
    int16_t pcm[1024] = {0};
    uint8_t output[226];
    uint64_t size = 0u;
    size_t written = 0u;
    uint32_t accepted = 0u;
    const uint8_t *payload;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 44100u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = APTA_TOTAL_FRAMES_UNKNOWN;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 2048u;
    block.frame_count = 1024u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 1024u);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    CHECK(apta_session_process(session, &budget, NULL) == APTA_STATUS_OK);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    apta_serialize_options_init(&options);
    CHECK(apta_result_query_serialized_size(result, &options, &size) ==
          APTA_STATUS_OK);
    CHECK(size == sizeof(output));
    CHECK(apta_result_serialize(
              result,
              &options,
              output,
              sizeof(output),
              &written) == APTA_STATUS_OK);
    CHECK(written == sizeof(output));

    CHECK(get_u32(output + 16u) == 3u);
    CHECK(get_u64(output + 40u) == UINT64_MAX);
    CHECK(get_u32(output + 48u) == 44100u);
    CHECK(get_u16(output + 52u) == 1u);

    payload = output + 136u;
    CHECK(get_u32(payload + 16u) == 3u);
    CHECK(get_u32(payload + 20u) == 1u);
    CHECK(get_u64(payload + 48u) == 2048u);
    CHECK(get_u64(payload + 56u) == 3072u);
    CHECK(get_u32(payload + 64u) == 2u);
    CHECK(get_u32(payload + 68u) == 1u);
    CHECK(get_u32(payload + 72u) == 0u);
    CHECK(get_u32(payload + 40u) == APTA_FEATURE_PARTIAL);
    CHECK(payload[89] == APTA_WAVEFORM_COLUMN_VALID);

    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}