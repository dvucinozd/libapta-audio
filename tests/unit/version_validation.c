// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    apta_result_info_t info;
    const apta_result_t *result;
    int16_t pcm[2] = {0, 0};
    uint32_t accepted = 77u;

    apta_context_config_init(&context_config);
    context_config.api_version = 0u;
    CHECK(apta_context_create(&context_config, &context) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(context == NULL);

    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 2u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    session_config.api_version = 0u;

    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(session == NULL);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 2u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 1u;
    block.api_version = 0u;

    CHECK(apta_session_push_pcm(session, &block, &accepted) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(accepted == 0u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    apta_result_info_init(&info);
    info.api_version = 0u;
    CHECK(apta_result_get_info(result, &info) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);

    return 0;
}
