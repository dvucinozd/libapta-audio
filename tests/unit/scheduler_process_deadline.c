// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    request.priority = APTA_PRIORITY_INTERACTIVE;
    request.soft_deadline_monotonic_ns = deadline;
    if (apta_session_request_region(session, &request, &request_id) !=
        APTA_STATUS_OK) {
        return 0u;
    }
    return request_id;
}

static int push_block(
    apta_session_t *session,
    apta_source_frame_t first_frame,
    const int16_t pcm[1024])
{
    apta_pcm_block_t block;
    uint32_t accepted = 0u;

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = first_frame;
    block.frame_count = 1024u;
    return apta_session_push_pcm(session, &block, &accepted) ==
               APTA_STATUS_OK &&
           accepted == 1024u;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    apta_waveform_overview_view_t overview;
    int16_t later_pcm[1024];
    int16_t earlier_pcm[1024];
    uint32_t later_request;
    uint32_t earlier_request;
    uint32_t index;

    memset(later_pcm, 0, sizeof(later_pcm));
    for (index = 0u; index < 1024u; ++index) {
        earlier_pcm[index] = INT16_MAX;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 5120u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    later_request = add_request(session, 0u, UINT64_C(5000));
    earlier_request = add_request(session, 4096u, UINT64_C(1000));
    CHECK(later_request != 0u);
    CHECK(earlier_request != 0u);

    CHECK(push_block(session, 0u, later_pcm));
    CHECK(push_block(session, 4096u, earlier_pcm));

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    CHECK(apta_session_process(session, &budget, NULL) ==
          APTA_STATUS_MORE_WORK);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].source_range.first_frame == 4096u);
    CHECK(overview.spans[0].source_range.end_frame == 5120u);
    CHECK(overview.spans[0].columns[0].minimum == INT16_MAX);
    CHECK(overview.spans[0].columns[0].maximum == INT16_MAX);
    apta_result_release(result);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
