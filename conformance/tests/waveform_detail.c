// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

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
    const apta_source_frame_t region_first = 32768u;
    const apta_source_frame_t region_end = 33792u;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_region_request_t request;
    apta_request_progress_t request_progress;
    apta_pcm_request_t pcm_request;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    apta_waveform_tile_view_t tile;
    int16_t pcm[1024] = {0};
    uint32_t request_id = 0u;
    uint32_t accepted = 0u;
    apta_status_t status;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 65536u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_region_request_init(&request);
    request.range.first_frame = region_first;
    request.range.end_frame = region_end;
    request.feature_mask = APTA_FEATURE_WAVEFORM_DETAIL;
    request.priority = APTA_PRIORITY_PLAYBACK_CRITICAL;
    CHECK(apta_session_request_region(session, &request, &request_id) ==
          APTA_STATUS_OK);
    CHECK(request_id != 0u);

    apta_request_progress_init(&request_progress);
    CHECK(apta_session_get_request_progress(
              session,
              request_id,
              &request_progress) == APTA_STATUS_OK);
    CHECK(request_progress.state == APTA_REQUEST_QUEUED);
    CHECK(request_progress.requested_features ==
          APTA_FEATURE_WAVEFORM_DETAIL);

    apta_pcm_request_init(&pcm_request);
    CHECK(apta_session_next_pcm_request(session, &pcm_request) ==
          APTA_STATUS_OK);
    CHECK(pcm_request.range.first_frame == region_first);
    CHECK(pcm_request.range.end_frame == region_end);
    CHECK(pcm_request.request_token == request_id);
    CHECK(pcm_request.feature_mask == APTA_FEATURE_WAVEFORM_DETAIL);

    apta_work_budget_init(&budget);
    budget.maximum_steps = 1u;
    CHECK(apta_session_process(session, &budget, NULL) ==
          APTA_STATUS_WOULD_BLOCK);

    apta_request_progress_init(&request_progress);
    CHECK(apta_session_get_request_progress(
              session,
              request_id,
              &request_progress) == APTA_STATUS_OK);
    CHECK(request_progress.state == APTA_REQUEST_WAITING_FOR_PCM);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = region_first;
    block.frame_count = 1024u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 1024u);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    status = apta_session_process(session, &budget, NULL);
    CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);

    apta_request_progress_init(&request_progress);
    CHECK(apta_session_get_request_progress(
              session,
              request_id,
              &request_progress) == APTA_STATUS_OK);
    CHECK(request_progress.state == APTA_REQUEST_SATISFIED);
    CHECK(request_progress.satisfied_features ==
          APTA_FEATURE_WAVEFORM_DETAIL);
    CHECK(request_progress.progress_permille == 1000u);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK((apta_result_get_available_features(result) &
           APTA_FEATURE_WAVEFORM_DETAIL) != 0u);

    apta_waveform_tile_view_init(&tile);
    CHECK(apta_result_get_waveform_tile(result, 1u, 2u, &tile) ==
          APTA_STATUS_OK);
    CHECK(tile.tile_index == 2u);
    CHECK(tile.source_range.first_frame == region_first);
    CHECK(tile.source_range.end_frame == region_end);
    CHECK(tile.first_column_index == 128u);
    CHECK(tile.column_count == 4u);
    CHECK(tile.state == APTA_FEATURE_PARTIAL);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
