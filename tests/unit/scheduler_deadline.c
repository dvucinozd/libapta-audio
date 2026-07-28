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
    uint64_t deadline)
{
    apta_region_request_t request;
    uint32_t request_id = 0u;

    apta_region_request_init(&request);
    request.range.first_frame = first_frame;
    request.range.end_frame = first_frame + 1024u;
    request.feature_mask = APTA_FEATURE_WAVEFORM_OVERVIEW;
    request.priority = APTA_PRIORITY_NORMAL;
    request.soft_deadline_monotonic_ns = deadline;
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
    uint32_t fifo_first;
    uint32_t later_deadline;
    uint32_t earlier_deadline;
    uint32_t fifo_second;

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

    fifo_first = add_request(session, 0u, 0u);
    later_deadline = add_request(session, 2048u, UINT64_C(5000));
    earlier_deadline = add_request(session, 4096u, UINT64_C(1000));
    fifo_second = add_request(session, 6144u, 0u);
    CHECK(fifo_first != 0u);
    CHECK(later_deadline != 0u);
    CHECK(earlier_deadline != 0u);
    CHECK(fifo_second != 0u);

    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
          APTA_STATUS_OK);
    CHECK(pcm_request.request_token == earlier_deadline);
    CHECK(pcm_request.range.first_frame == 4096u);
    CHECK(pcm_request.priority == APTA_PRIORITY_NORMAL);

    CHECK(apta_session_cancel_region_request(session, earlier_deadline) ==
          APTA_STATUS_OK);
    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
          APTA_STATUS_OK);
    CHECK(pcm_request.request_token == later_deadline);
    CHECK(pcm_request.range.first_frame == 2048u);

    CHECK(apta_session_cancel_region_request(session, later_deadline) ==
          APTA_STATUS_OK);
    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
          APTA_STATUS_OK);
    CHECK(pcm_request.request_token == fifo_first);
    CHECK(pcm_request.range.first_frame == 0u);

    CHECK(apta_session_cancel_region_request(session, fifo_first) ==
          APTA_STATUS_OK);
    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
          APTA_STATUS_OK);
    CHECK(pcm_request.request_token == fifo_second);
    CHECK(pcm_request.range.first_frame == 6144u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
