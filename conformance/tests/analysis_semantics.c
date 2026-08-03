// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define SAMPLE_RATE 48000u
#define BEAT_FRAMES 23040u
#define TOTAL_FRAMES 384000u
#define BLOCK_FRAMES 4096u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int push_audio(apta_session_t *session)
{
    int16_t samples[BLOCK_FRAMES];
    apta_work_budget_t budget;
    uint32_t first = 0u;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;

    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t accepted = 0u;
        uint32_t index;
        apta_status_t status;

        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            const uint32_t offset = (first + index) % BEAT_FRAMES;
            samples[index] = offset < 128u ? (int16_t)30000 : 0;
        }
        apta_pcm_block_init(&block);
        block.data = samples;
        block.first_frame = first;
        block.frame_count = count;
        CHECK(apta_session_push_pcm(session, &block, &accepted) ==
              APTA_STATUS_OK);
        CHECK(accepted == count);
        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        first += count;
    }

    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    for (first = 0u; first < 128u; ++first) {
        const apta_status_t status =
            apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            return 0;
        }
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    }
    return 1;
}

static int valid_range(apta_frame_range_t range)
{
    return range.first_frame <= range.end_frame &&
           range.end_frame <= TOTAL_FRAMES;
}

int main(void)
{
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
    uint32_t index;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK((apta_context_get_capabilities(context) & features) == features);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = features;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(push_audio(session) == 0);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.state == APTA_FEATURE_FINAL);
    CHECK(tempo.selected.tempo_millibpm > 0u);
    CHECK(tempo.selected.confidence <= 100u);
    CHECK(valid_range(tempo.selected.evidence_range));
    CHECK(valid_range(tempo.selected.applicability_range));
    CHECK(tempo.candidate_count > 0u);
    CHECK(tempo.candidates != NULL);
    for (index = 1u; index < tempo.candidate_count; ++index) {
        CHECK(tempo.candidates[index - 1u].score >=
              tempo.candidates[index].score);
    }

    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result, APTA_FEATURE_LOCAL_BEATGRID, NULL, &grid) ==
          APTA_STATUS_OK);
    CHECK(grid.state == APTA_FEATURE_FINAL);
    CHECK(grid.confidence <= 100u);
    CHECK(valid_range(grid.requested_range));
    CHECK(valid_range(grid.evidence_range));
    CHECK(valid_range(grid.applicability_range));
    CHECK(grid.coverage_range_count > 0u);
    CHECK(grid.coverage_ranges != NULL);
    for (index = 0u; index < grid.coverage_range_count; ++index) {
        CHECK(valid_range(grid.coverage_ranges[index]));
    }
    if (grid.representation == APTA_GRID_REPRESENTATION_SEGMENTS) {
        CHECK(grid.segment_count > 0u);
        CHECK(grid.segments != NULL);
        for (index = 0u; index < grid.segment_count; ++index) {
            CHECK(valid_range(grid.segments[index].applicability_range));
            CHECK(grid.segments[index].frames_per_beat.whole_frames > 0u);
            CHECK(grid.segments[index].state == APTA_FEATURE_FINAL);
        }
    } else {
        CHECK(grid.representation == APTA_GRID_REPRESENTATION_EXPLICIT ||
              grid.representation == APTA_GRID_REPRESENTATION_HYBRID);
        CHECK(grid.beat_count > 0u);
        CHECK(grid.beats != NULL);
    }

    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_BPM,
              NULL,
              &state,
              &confidence) == APTA_STATUS_OK);
    CHECK(state == APTA_FEATURE_FINAL);
    CHECK(confidence <= 100u);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
