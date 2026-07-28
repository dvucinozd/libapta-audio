// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint32_t add_request(
    apta_session_t *session,
    apta_source_frame_t first_frame,
    uint8_t priority)
{
    apta_region_request_t request;
    uint32_t request_id = 0u;

    apta_region_request_init(&request);
    request.range.first_frame = first_frame;
    request.range.end_frame = first_frame + 1024u;
    request.feature_mask = APTA_FEATURE_WAVEFORM_OVERVIEW;
    request.priority = priority;
    if (apta_session_request_region(session, &request, &request_id) !=
        APTA_STATUS_OK) {
        return 0u;
    }
    return request_id;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_request_t pcm_request;
    uint32_t background_request;
    uint32_t normal_request;
    uint32_t decision;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 8192u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    background_request = add_request(
        session,
        0u,
        APTA_PRIORITY_BACKGROUND);
    normal_request = add_request(
        session,
        4096u,
        APTA_PRIORITY_NORMAL);
    CHECK(background_request != 0u);
    CHECK(normal_request != 0u);

    for (decision = 1u; decision <= 8u; ++decision) {
        apta_pcm_request_init(&pcm_request);
        CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
              APTA_STATUS_OK);
        CHECK(pcm_request.request_token == normal_request);
        CHECK(pcm_request.range.first_frame == 4096u);
        CHECK(pcm_request.priority == APTA_PRIORITY_NORMAL);
    }

    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
          APTA_STATUS_OK);
    CHECK(pcm_request.request_token == background_request);
    CHECK(pcm_request.range.first_frame == 0u);
    CHECK(pcm_request.priority == APTA_PRIORITY_NORMAL);

    CHECK(apta_session_cancel_region_request(session, normal_request) ==
          APTA_STATUS_OK);
    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
          APTA_STATUS_OK);
    CHECK(pcm_request.request_token == background_request);
    CHECK(pcm_request.priority == APTA_PRIORITY_BACKGROUND);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
