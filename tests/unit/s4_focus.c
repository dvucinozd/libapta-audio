// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define TOTAL_FRAMES 288000u
#define BLOCK_FRAMES 4096u
#define BEAT_FRAMES 23040u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int check_grid_range(
    const apta_result_t *result,
    uint64_t first,
    uint64_t end)
{
    apta_grid_view_t grid;
    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.requested_range.first_frame == first);
    CHECK(grid.requested_range.end_frame == end);
    CHECK(grid.applicability_range.first_frame == first);
    CHECK(grid.applicability_range.end_frame == end);
    return 0;
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
    const apta_result_t *old_result = NULL;
    const apta_result_t *new_result = NULL;
    apta_focus_t focus;
    apta_work_budget_t budget;
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    apta_generation_t old_generation;
    apta_status_t status;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = features;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_focus_init(&focus);
    focus.playhead_frame = 192000u;
    focus.lookbehind_frames = 48000u;
    focus.lookahead_frames = 48000u;
    focus.feature_mask =
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    CHECK(apta_session_set_focus(session, &focus) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;
    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t accepted = 0u;
        uint32_t index;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            samples[index] = ((first + index) % BEAT_FRAMES) < 128u
                                 ? (int16_t)30000
                                 : 0;
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

    old_result = apta_session_acquire_result(session);
    CHECK(old_result != NULL);
    CHECK(check_grid_range(old_result, 144000u, 240000u) == 0);
    old_generation = apta_result_get_generation(old_result);

    focus.playhead_frame = 240000u;
    focus.lookbehind_frames = 24000u;
    focus.lookahead_frames = 24000u;
    CHECK(apta_session_set_focus(session, &focus) == APTA_STATUS_OK);
    status = apta_session_process(session, &budget, NULL);
    CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);

    new_result = apta_session_acquire_result(session);
    CHECK(new_result != NULL);
    CHECK(apta_result_get_generation(new_result) > old_generation);
    CHECK(check_grid_range(new_result, 216000u, 264000u) == 0);
    CHECK(check_grid_range(old_result, 144000u, 240000u) == 0);

    apta_result_release(new_result);
    apta_result_release(old_result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
