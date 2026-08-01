// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define COL APTA_TEST_COLUMN_FRAMES

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
    apta_focus_t focus;
    apta_work_budget_t budget;
    apta_progress_t progress;
    apta_waveform_overview_view_t overview;
    const apta_result_t *result = NULL;
    int16_t background[COL];
    int16_t focused[COL];
    uint32_t accepted;
    uint32_t index;

    memset(background, 0, sizeof(background));
    for (index = 0u; index < COL; ++index) {
        focused[index] = INT16_MAX;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = (5u * COL);
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = background;
    block.first_frame = 0u;
    block.frame_count = COL;
    accepted = 0u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == COL);

    apta_pcm_block_init(&block);
    block.data = focused;
    block.first_frame = (4u * COL);
    block.frame_count = COL;
    accepted = 0u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == COL);

    apta_focus_init(&focus);
    focus.playhead_frame = (4u * COL);
    focus.lookbehind_frames = 0u;
    focus.lookahead_frames = COL;
    focus.feature_mask = APTA_FEATURE_WAVEFORM_OVERVIEW;
    focus.priority = APTA_PRIORITY_PLAYBACK_CRITICAL;
    CHECK(apta_session_set_focus(session, &focus) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = COL;
    budget.maximum_steps = 4u;
    apta_progress_init(&progress);

    CHECK(apta_session_process(session, &budget, &progress) == APTA_STATUS_MORE_WORK);
    CHECK(progress.consumed_input_frames == COL);
    CHECK(progress.changed_features == APTA_FEATURE_WAVEFORM_OVERVIEW);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].source_range.first_frame == (4u * COL));
    CHECK(overview.spans[0].source_range.end_frame == (5u * COL));
    CHECK(overview.spans[0].first_column_index == 4u);
    CHECK(overview.spans[0].column_count == 1u);
    CHECK(overview.spans[0].columns[0].minimum == INT16_MAX);
    CHECK(overview.spans[0].columns[0].maximum == INT16_MAX);
    CHECK(overview.spans[0].columns[0].rms == UINT16_MAX);
    apta_result_release(result);
    result = NULL;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = COL;
    budget.maximum_steps = 4u;
    CHECK(apta_session_process(session, &budget, NULL) == APTA_STATUS_OK);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.span_count == 2u);
    CHECK(overview.spans[0].source_range.first_frame == 0u);
    CHECK(overview.spans[0].source_range.end_frame == COL);
    CHECK(overview.spans[1].source_range.first_frame == (4u * COL));
    CHECK(overview.spans[1].source_range.end_frame == (5u * COL));

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
