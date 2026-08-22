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

static void init_result_info(apta_result_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->struct_size = (uint32_t)sizeof(*info);
    info->api_version = APTA_API_VERSION;
}

static void init_progress(apta_progress_t *progress)
{
    memset(progress, 0, sizeof(*progress));
    progress->struct_size = (uint32_t)sizeof(*progress);
    progress->api_version = APTA_API_VERSION;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_result_info_t info;
    apta_pcm_block_t block;
    apta_focus_t focus;
    apta_work_budget_t budget;
    apta_progress_t progress;
    int16_t pcm[8] = {0, 0, 1000, -1000, 32767, -32768, 0, 0};
    uint32_t accepted_frames = 0u;

    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(context != NULL);
    CHECK(apta_context_get_capabilities(context) ==
          (APTA_FEATURE_WAVEFORM_OVERVIEW |
           APTA_FEATURE_WAVEFORM_DETAIL |
           /* C1: three-band overview waveform is now implemented, so the
            * advertised capability set genuinely grew. */
           APTA_FEATURE_WAVEFORM_3BAND |
           APTA_FEATURE_BPM |
           APTA_FEATURE_LOCAL_BEATGRID |
           APTA_FEATURE_GLOBAL_BEATGRID |
           APTA_FEATURE_DYNAMIC_TEMPO |
           APTA_FEATURE_CONFIDENCE |
           APTA_FEATURE_GRID_LOCKING |
           APTA_FEATURE_METER_DOWNBEAT));

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 2u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
    session_config.requested_features = 0u;

    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);
    CHECK(session != NULL);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    init_result_info(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.generation == 1u);
    CHECK(info.session_state == APTA_SESSION_CREATED);
    apta_result_release(result);
    result = NULL;

    apta_focus_init(&focus);
    focus.playhead_frame = 0u;
    focus.lookahead_frames = 48000u;
    focus.feature_mask = 0u;
    CHECK(apta_session_set_focus(session, &focus) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 4u;

    CHECK(apta_session_push_pcm(session, &block, &accepted_frames) == APTA_STATUS_OK);
    CHECK(accepted_frames == 4u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_ACTIVE);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    init_result_info(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.generation == 2u);
    CHECK(info.session_state == APTA_SESSION_ACTIVE);
    apta_result_release(result);
    result = NULL;

    CHECK(apta_session_signal_end_of_input(session, 4u) == APTA_STATUS_OK);
    CHECK(apta_session_get_state(session) == APTA_SESSION_DRAINING);

    apta_work_budget_init(&budget);
    budget.maximum_steps = 1u;
    init_progress(&progress);

    CHECK(apta_session_process(session, &budget, &progress) ==
          APTA_STATUS_END_OF_INPUT);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);
    CHECK(progress.published_generation == 4u);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    init_result_info(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.generation == 4u);
    CHECK(info.session_state == APTA_SESSION_COMPLETED);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);
    CHECK(apta_result_get_generation(result) == 4u);

    apta_result_release(result);
    result = NULL;

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;

    return 0;
}
