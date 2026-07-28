// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define SAMPLE_RATE 48000u
#define TEMPO_MILLIBPM 125000u
#define BEAT_FRAMES 23040u
#define TOTAL_FRAMES 384000u
#define STABLE_FRAMES 288000u
#define BLOCK_FRAMES 4096u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int16_t click_sample(uint64_t frame)
{
    const uint32_t offset = (uint32_t)(frame % BEAT_FRAMES);
    return offset < 128u ? (int16_t)30000 : 0;
}

static int push_range(
    apta_session_t *session,
    uint64_t first,
    uint64_t end)
{
    int16_t samples[BLOCK_FRAMES];
    apta_work_budget_t budget;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;

    while (first < end) {
        apta_pcm_block_t block;
        uint32_t count = (uint32_t)(end - first);
        uint32_t accepted = 0u;
        uint32_t index;
        apta_status_t status;

        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            samples[index] = click_sample(first + index);
        }

        apta_pcm_block_init(&block);
        block.data = samples;
        block.first_frame = first;
        block.frame_count = count;
        status = apta_session_push_pcm(session, &block, &accepted);
        CHECK(status == APTA_STATUS_OK);
        CHECK(accepted == count);

        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        first += count;
    }
    return 0;
}

static int check_tempo_and_grid(
    const apta_result_t *result,
    apta_feature_state_t minimum_state,
    uint32_t require_locked)
{
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    uint32_t index;

    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.tempo_millibpm >= TEMPO_MILLIBPM - 500u);
    CHECK(tempo.selected.tempo_millibpm <= TEMPO_MILLIBPM + 500u);
    CHECK(tempo.selected.state >= minimum_state);
    CHECK(tempo.selected.confidence >= 50u);
    CHECK(tempo.candidate_count >= 1u);
    CHECK(tempo.candidate_count <= APTA_REFERENCE_TEMPO_MAX_CANDIDATES);
    CHECK(tempo.candidates != NULL);
    for (index = 1u; index < tempo.candidate_count; ++index) {
        CHECK(tempo.candidates[index - 1u].score >= tempo.candidates[index].score);
    }

    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.representation == APTA_GRID_REPRESENTATION_SEGMENTS);
    CHECK(grid.state >= minimum_state);
    CHECK(grid.confidence >= 50u);
    CHECK(grid.coverage_range_count == 1u);
    CHECK(grid.segment_count == 1u);
    CHECK(grid.segments != NULL);
    CHECK(grid.segments[0].nominal_tempo_millibpm >=
          TEMPO_MILLIBPM - 500u);
    CHECK(grid.segments[0].nominal_tempo_millibpm <=
          TEMPO_MILLIBPM + 500u);
    CHECK(grid.segments[0].frames_per_beat.whole_frames >=
          BEAT_FRAMES - 128u);
    CHECK(grid.segments[0].frames_per_beat.whole_frames <=
          BEAT_FRAMES + 128u);
    CHECK(grid.segments[0].beat_count > 0u);
    if (require_locked) {
        CHECK((grid.flags & APTA_GRID_FLAG_LOCKED) != 0u);
        CHECK((grid.segments[0].flags & APTA_GRID_FLAG_LOCKED) != 0u);
    }

    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_BPM,
              NULL,
              &state,
              &confidence) == APTA_STATUS_OK);
    CHECK(state >= minimum_state);
    CHECK(confidence >= 50u);
    return 0;
}

int main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_GRID_LOCKING;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_frame_range_t lock_range;
    apta_frame_range_t outside;
    apta_grid_view_t unavailable;
    apta_work_budget_t budget;
    apta_status_t status;
    uint32_t attempts;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = features;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    CHECK(push_range(session, 0u, STABLE_FRAMES) == 0);
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(check_tempo_and_grid(result, APTA_FEATURE_STABLE, 0u) == 0);
    apta_result_release(result);
    result = NULL;

    apta_frame_range_init(&lock_range);
    lock_range.first_frame = BEAT_FRAMES;
    lock_range.end_frame = STABLE_FRAMES - BEAT_FRAMES;
    CHECK(apta_session_lock_grid_range(session, &lock_range) == APTA_STATUS_OK);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(check_tempo_and_grid(result, APTA_FEATURE_STABLE, 1u) == 0);
    apta_result_release(result);
    result = NULL;

    CHECK(push_range(session, STABLE_FRAMES, TOTAL_FRAMES) == 0);
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;
    status = APTA_STATUS_MORE_WORK;
    for (attempts = 0u; attempts < 64u; ++attempts) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK ||
              status == APTA_STATUS_WOULD_BLOCK);
    }
    CHECK(status == APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(check_tempo_and_grid(result, APTA_FEATURE_FINAL, 1u) == 0);

    apta_frame_range_init(&outside);
    outside.first_frame = TOTAL_FRAMES + 1u;
    outside.end_frame = TOTAL_FRAMES + 100u;
    apta_grid_view_init(&unavailable);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              &outside,
              &unavailable) == APTA_STATUS_NOT_AVAILABLE);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(check_tempo_and_grid(result, APTA_FEATURE_FINAL, 1u) == 0);
    apta_result_release(result);
    result = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
