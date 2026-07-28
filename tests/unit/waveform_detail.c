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
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_request_t pcm_request;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_waveform_tile_view_t tile;
    apta_frame_range_t range;
    apta_feature_state_t feature_state;
    apta_confidence_value_t confidence;
    int16_t pcm[1024] = {0};
    uint32_t accepted = 0u;
    uint32_t column;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK((apta_context_get_capabilities(context) &
           APTA_FEATURE_WAVEFORM_DETAIL) != 0u);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
          APTA_STATUS_OK);
    CHECK(pcm_request.range.first_frame == 0u);
    CHECK(pcm_request.range.end_frame == 1024u);
    CHECK(pcm_request.feature_mask ==
          (APTA_FEATURE_WAVEFORM_OVERVIEW |
           APTA_FEATURE_WAVEFORM_DETAIL));

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 1024u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 1024u);
    CHECK(apta_session_signal_end_of_input(session, 1024u) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    CHECK(apta_session_process(session, &budget, NULL) ==
          APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK((apta_result_get_available_features(result) &
           APTA_FEATURE_WAVEFORM_OVERVIEW) != 0u);
    CHECK((apta_result_get_available_features(result) &
           APTA_FEATURE_WAVEFORM_DETAIL) != 0u);

    apta_waveform_tile_view_init(&tile);
    CHECK(apta_result_get_waveform_tile(result, 1u, 0u, &tile) ==
          APTA_STATUS_OK);
    CHECK(tile.level_id == 1u);
    CHECK(tile.tile_index == 0u);
    CHECK(tile.source_range.first_frame == 0u);
    CHECK(tile.source_range.end_frame == 1024u);
    CHECK(tile.first_column_index == 0u);
    CHECK(tile.column_count == 4u);
    CHECK(tile.state == APTA_FEATURE_FINAL);
    CHECK(tile.columns != NULL);

    for (column = 0u; column < tile.column_count; ++column) {
        CHECK(tile.columns[column].minimum == 0);
        CHECK(tile.columns[column].maximum == 0);
        CHECK(tile.columns[column].rms == 0u);
        CHECK(tile.columns[column].flags == APTA_WAVEFORM_COLUMN_VALID);
    }

    apta_frame_range_init(&range);
    range.first_frame = 0u;
    range.end_frame = 1024u;
    feature_state = APTA_FEATURE_ABSENT;
    confidence = 0u;
    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_WAVEFORM_DETAIL,
              &range,
              &feature_state,
              &confidence) == APTA_STATUS_OK);
    CHECK(feature_state == APTA_FEATURE_FINAL);
    CHECK(confidence == APTA_CONFIDENCE_UNKNOWN);

    apta_waveform_tile_view_init(&tile);
    CHECK(apta_result_get_waveform_tile(result, 1u, 1u, &tile) ==
          APTA_STATUS_NOT_AVAILABLE);
    CHECK(tile.confidence == APTA_CONFIDENCE_UNKNOWN);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
