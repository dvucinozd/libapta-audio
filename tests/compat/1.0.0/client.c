// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <string.h>

#include <apta/apta.h>

#define REQUIRE(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_result_info_t result_info;
    apta_source_info_t source_info;

    apta_context_config_init(&context_config);
    context_config.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 123u);
    REQUIRE(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.source_fingerprint_kind =
        APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256;
    session_config.source_fingerprint[0] = 0x10u;
    session_config.source_fingerprint[31] = 0x01u;
    REQUIRE(apta_session_create(context, &session_config, &session) ==
            APTA_STATUS_OK);

    result = apta_session_acquire_result(session);
    REQUIRE(result != NULL);

    apta_result_info_init(&result_info);
    result_info.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 999u);
    REQUIRE(apta_result_get_info(result, &result_info) == APTA_STATUS_OK);
    REQUIRE(result_info.producer_api_version == APTA_API_VERSION);

    apta_source_info_init(&source_info);
    source_info.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 7u);
    REQUIRE(apta_result_get_source_info(result, &source_info) == APTA_STATUS_OK);
    REQUIRE(source_info.sample_rate == 48000u);
    REQUIRE(source_info.channel_count == 1u);
    REQUIRE(source_info.fingerprint_kind ==
            APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256);
    REQUIRE(source_info.fingerprint[0] == 0x10u);
    REQUIRE(source_info.fingerprint[31] == 0x01u);

    apta_result_release(result);
    REQUIRE(apta_session_destroy(session) == APTA_STATUS_OK);
    REQUIRE(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
