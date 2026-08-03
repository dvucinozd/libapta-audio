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

static void configure_session(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = 48000u;
    config->channel_count = 2u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
}

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

    CHECK(APTA_API_VERSION_GET_MAJOR(APTA_API_VERSION) == 1u);
    CHECK(APTA_API_VERSION_GET_MINOR(APTA_API_VERSION) == 0u);
    CHECK(APTA_API_VERSION_GET_PATCH(APTA_API_VERSION) == 0u);

    /* Patch differences never change the 1.x ABI contract. */
    apta_context_config_init(&context_config);
    context_config.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 4095u);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;

    /* Wrong major, newer caller minor and undersized prefixes are rejected. */
    apta_context_config_init(&context_config);
    context_config.api_version = APTA_API_VERSION_ENCODE(0u, 3u, 0u);
    CHECK(apta_context_create(&context_config, &context) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(context == NULL);

    apta_context_config_init(&context_config);
    context_config.api_version = APTA_API_VERSION_ENCODE(1u, 1u, 0u);
    CHECK(apta_context_create(&context_config, &context) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(context == NULL);

    apta_context_config_init(&context_config);
    context_config.struct_size = (uint32_t)sizeof(context_config) - 1u;
    CHECK(apta_context_create(&context_config, &context) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(context == NULL);

    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    configure_session(&session_config);
    session_config.api_version = APTA_API_VERSION_ENCODE(2u, 0u, 0u);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(session == NULL);

    configure_session(&session_config);
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 1u;
    block.api_version = APTA_API_VERSION_ENCODE(1u, 1u, 0u);
    CHECK(apta_session_push_pcm(session, &block, &accepted) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(accepted == 0u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    apta_result_info_init(&info);
    info.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 77u);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.api_version == APTA_API_VERSION);

    apta_result_info_init(&info);
    info.api_version = APTA_API_VERSION_ENCODE(1u, 1u, 0u);
    CHECK(apta_result_get_info(result, &info) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);

    return 0;
}
