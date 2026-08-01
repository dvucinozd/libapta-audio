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

static int process_all(apta_session_t *session)
{
    apta_work_budget_t budget;
    apta_progress_t progress;
    apta_status_t status;
    uint32_t guard;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 256u;
    budget.maximum_steps = 1u;

    for (guard = 0u; guard < 100u; ++guard) {
        apta_progress_init(&progress);
        status = apta_session_process(session, &budget, &progress);
        if (status == APTA_STATUS_OK || status == APTA_STATUS_WOULD_BLOCK ||
            status == APTA_STATUS_END_OF_INPUT) {
            return status < 0 ? 1 : 0;
        }
        if (status != APTA_STATUS_MORE_WORK) {
            return 1;
        }
        if (progress.consumed_input_frames > 256u ||
            progress.completed_steps > 1u) {
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
    apta_pcm_request_t pcm_request;
    apta_waveform_overview_view_t overview;
    apta_frame_range_t full_range;
    apta_feature_state_t feature_state;
    apta_confidence_value_t confidence;
    const apta_result_t *result = NULL;
    int16_t first_column[COL];
    int16_t second_column[COL];
    uint32_t accepted;
    uint32_t index;

    memset(first_column, 0, sizeof(first_column));
    for (index = 0u; index < COL; ++index) {
        second_column[index] = INT16_MAX;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_context_get_capabilities(context) ==
          APTA_FEATURE_WAVEFORM_OVERVIEW);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = (2u * COL);
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;

    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = second_column;
    block.first_frame = COL;
    block.frame_count = COL;
    accepted = 0u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == COL);

    accepted = 77u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_ERROR_CONFLICT);
    CHECK(accepted == 0u);

    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) == APTA_STATUS_OK);
    CHECK(pcm_request.range.first_frame == 0u);
    CHECK(pcm_request.range.end_frame == COL);
    CHECK(pcm_request.feature_mask == APTA_FEATURE_WAVEFORM_OVERVIEW);

    CHECK(process_all(session) == 0);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK((apta_result_get_available_features(result) &
           APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u);

    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.level.frames_per_column == COL);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].source_range.first_frame == COL);
    CHECK(overview.spans[0].source_range.end_frame == (2u * COL));
    CHECK(overview.spans[0].first_column_index == 1u);
    CHECK(overview.spans[0].column_count == 1u);
    CHECK(overview.spans[0].columns[0].minimum == INT16_MAX);
    CHECK(overview.spans[0].columns[0].maximum == INT16_MAX);
    CHECK(overview.spans[0].columns[0].rms == UINT16_MAX);
    CHECK((overview.spans[0].columns[0].flags &
           APTA_WAVEFORM_COLUMN_CLIPPED) != 0u);
    CHECK(overview.state == APTA_FEATURE_PARTIAL);
    apta_result_release(result);
    result = NULL;

    apta_pcm_block_init(&block);
    block.data = first_column;
    block.first_frame = 0u;
    block.frame_count = COL;
    accepted = 0u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == COL);
    CHECK(process_all(session) == 0);

    CHECK(apta_session_signal_end_of_input(session, (2u * COL)) == APTA_STATUS_OK);
    CHECK(process_all(session) == 0);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.state == APTA_FEATURE_FINAL);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].source_range.first_frame == 0u);
    CHECK(overview.spans[0].source_range.end_frame == (2u * COL));
    CHECK(overview.spans[0].first_column_index == 0u);
    CHECK(overview.spans[0].column_count == 2u);

    CHECK(overview.spans[0].columns[0].minimum == 0);
    CHECK(overview.spans[0].columns[0].maximum == 0);
    CHECK(overview.spans[0].columns[0].rms == 0u);
    CHECK(overview.spans[0].columns[0].flags == APTA_WAVEFORM_COLUMN_VALID);

    CHECK(overview.spans[0].columns[1].minimum == INT16_MAX);
    CHECK(overview.spans[0].columns[1].maximum == INT16_MAX);
    CHECK(overview.spans[0].columns[1].rms == UINT16_MAX);

    apta_frame_range_init(&full_range);
    full_range.first_frame = 0u;
    full_range.end_frame = (2u * COL);
    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_WAVEFORM_OVERVIEW,
              &full_range,
              &feature_state,
              &confidence) == APTA_STATUS_OK);
    CHECK(feature_state == APTA_FEATURE_FINAL);
    CHECK(confidence == APTA_CONFIDENCE_UNKNOWN);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
