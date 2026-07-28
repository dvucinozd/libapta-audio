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

int main(void)
{
    const uint32_t total_frames = 144000u;
    const uint32_t block_frames = 4096u;
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    apta_work_budget_t budget;
    int16_t silence[4096];
    uint32_t first = 0u;
    apta_status_t status;

    memset(silence, 0, sizeof(silence));
    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = total_frames;
    session_config.requested_features = features;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = block_frames;
    budget.maximum_steps = 32u;
    while (first < total_frames) {
        apta_pcm_block_t block;
        uint32_t count = total_frames - first;
        uint32_t accepted = 0u;
        if (count > block_frames) {
            count = block_frames;
        }
        apta_pcm_block_init(&block);
        block.data = silence;
        block.first_frame = first;
        block.frame_count = count;
        CHECK(apta_session_push_pcm(session, &block, &accepted) ==
              APTA_STATUS_OK);
        CHECK(accepted == count);
        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        first += count;
    }

    CHECK(apta_session_signal_end_of_input(session, total_frames) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) ==
          APTA_STATUS_NOT_AVAILABLE);
    CHECK(tempo.selected.tempo_millibpm == 0u);
    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_NOT_AVAILABLE);
    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_BPM,
              NULL,
              &state,
              &confidence) == APTA_STATUS_NOT_AVAILABLE);
    CHECK(state == APTA_FEATURE_ABSENT);
    CHECK(confidence == APTA_CONFIDENCE_UNKNOWN);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
