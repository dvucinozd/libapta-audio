// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#define COLUMN_FRAMES 256u
#define TOTAL_FRAMES (2u * COLUMN_FRAMES)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int drain(apta_session_t *session)
{
    apta_work_budget_t budget;
    uint32_t guard;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 256u;
    budget.maximum_steps = 8u;
    for (guard = 0u; guard < 128u; ++guard) {
        const apta_status_t status =
            apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            return 0;
        }
        if (status < 0 || status == APTA_STATUS_WOULD_BLOCK) {
            return 1;
        }
    }
    return 1;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    const apta_result_t *result = NULL;
    apta_waveform_overview_view_t view;
    apta_source_info_t source;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    apta_frame_range_t full_range;
    int16_t pcm[TOTAL_FRAMES];
    uint32_t accepted = 0u;
    uint32_t index;

    memset(pcm, 0, sizeof(pcm));
    for (index = COLUMN_FRAMES; index < TOTAL_FRAMES; ++index) {
        pcm[index] = (index & 1u) == 0u ? INT16_MIN : INT16_MAX;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK((apta_context_get_capabilities(context) &
           APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    session_config.overview_frames_per_column = COLUMN_FRAMES;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = TOTAL_FRAMES;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == TOTAL_FRAMES);
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    CHECK(drain(session) == 0);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK((apta_result_get_available_features(result) &
           APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u);

    apta_source_info_init(&source);
    CHECK(apta_result_get_source_info(result, &source) == APTA_STATUS_OK);
    CHECK(source.total_frames == TOTAL_FRAMES);
    CHECK(source.sample_rate == 48000u);
    CHECK(source.channel_count == 1u);
    CHECK(source.channel_layout == APTA_CHANNEL_LAYOUT_MONO);

    apta_waveform_overview_view_init(&view);
    CHECK(apta_result_get_waveform_overview(result, 0u, &view) ==
          APTA_STATUS_OK);
    CHECK(view.state == APTA_FEATURE_FINAL);
    CHECK(view.level.frames_per_column == COLUMN_FRAMES);
    CHECK(view.span_count == 1u);
    CHECK(view.spans[0].source_range.first_frame == 0u);
    CHECK(view.spans[0].source_range.end_frame == TOTAL_FRAMES);
    CHECK(view.spans[0].first_column_index == 0u);
    CHECK(view.spans[0].column_count == 2u);
    CHECK(view.spans[0].columns != NULL);

    CHECK(view.spans[0].columns[0].minimum == 0);
    CHECK(view.spans[0].columns[0].maximum == 0);
    CHECK(view.spans[0].columns[0].rms == 0u);
    CHECK((view.spans[0].columns[0].flags &
           APTA_WAVEFORM_COLUMN_VALID) != 0u);

    CHECK(view.spans[0].columns[1].minimum <=
          view.spans[0].columns[1].maximum);
    CHECK((view.spans[0].columns[1].flags &
           APTA_WAVEFORM_COLUMN_VALID) != 0u);

    apta_frame_range_init(&full_range);
    full_range.first_frame = 0u;
    full_range.end_frame = TOTAL_FRAMES;
    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_WAVEFORM_OVERVIEW,
              &full_range,
              &state,
              &confidence) == APTA_STATUS_OK);
    CHECK(state == APTA_FEATURE_FINAL);
    CHECK(confidence == APTA_CONFIDENCE_UNKNOWN || confidence <= 100u);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
