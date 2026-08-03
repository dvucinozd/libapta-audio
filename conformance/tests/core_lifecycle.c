// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
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
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_result_info_t info;
    int16_t pcm[256] = {0};
    uint32_t accepted = 0u;
    apta_status_t status;
    uint32_t guard;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(context != NULL);
    CHECK((apta_context_get_capabilities(context) &
           APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 256u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    session_config.overview_frames_per_column = 256u;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(session != NULL);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.session_state == APTA_SESSION_CREATED);
    CHECK(info.generation > 0u);
    apta_result_release(result);
    result = NULL;

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 256u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 256u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_ACTIVE);
    CHECK(apta_session_signal_end_of_input(session, 256u) == APTA_STATUS_OK);
    CHECK(apta_session_get_state(session) == APTA_SESSION_DRAINING);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 256u;
    budget.maximum_steps = 8u;
    status = APTA_STATUS_MORE_WORK;
    for (guard = 0u; guard < 64u; ++guard) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    }
    CHECK(status == APTA_STATUS_END_OF_INPUT);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.session_state == APTA_SESSION_COMPLETED);
    CHECK(info.generation > 0u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);
    CHECK(apta_result_get_generation(result) > 0u);
    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
