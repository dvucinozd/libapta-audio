// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define SAMPLE_RATE 48000u
#define BLOCK_FRAMES 4096u
#define TOTAL_FRAMES 1048576u
#define SPLIT_FRAME (TOTAL_FRAMES / 2u)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint32_t beat_period(uint32_t tempo_millibpm)
{
    return (uint32_t)(((uint64_t)SAMPLE_RATE * UINT64_C(60000) +
                       tempo_millibpm / 2u) /
                      tempo_millibpm);
}

static int fill_and_push(
    apta_session_t *session,
    uint32_t first,
    uint32_t count,
    uint32_t first_tempo,
    uint32_t second_tempo,
    apta_work_budget_t *budget)
{
    int16_t samples[BLOCK_FRAMES];
    apta_pcm_block_t block;
    uint32_t accepted = 0u;
    uint32_t index;
    const uint32_t first_period = beat_period(first_tempo);
    const uint32_t second_period = beat_period(second_tempo);

    for (index = 0u; index < count; ++index) {
        const uint32_t frame = first + index;
        const uint32_t period = frame < SPLIT_FRAME
                                    ? first_period
                                    : second_period;
        const uint32_t origin = frame < SPLIT_FRAME ? 0u : SPLIT_FRAME;
        samples[index] = ((frame - origin) % period) < 192u
                             ? (int16_t)30000
                             : 0;
    }
    apta_pcm_block_init(&block);
    block.data = samples;
    block.first_frame = first;
    block.frame_count = count;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == count);
    {
        const apta_status_t status = apta_session_process(session, budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    }
    return 0;
}

static int finish_session(apta_session_t *session, apta_work_budget_t *budget)
{
    apta_status_t status;

    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);
    return 0;
}

static int run_constant(apta_context_t *context)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    apta_grid_view_t grid;
    apta_grid_revision_view_t revision;
    uint32_t first;

    apta_session_config_init(&config);
    config.source_sample_rate = SAMPLE_RATE;
    config.channel_count = 1u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.total_frames = TOTAL_FRAMES;
    config.requested_features = features;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
    for (first = 0u; first < TOTAL_FRAMES; first += BLOCK_FRAMES) {
        uint32_t count = TOTAL_FRAMES - first;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        CHECK(fill_and_push(
                  session, first, count, 120000u, 120000u, &budget) == 0);
    }
    CHECK(finish_session(session, &budget) == 0);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_GLOBAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.state == APTA_FEATURE_FINAL);
    CHECK(grid.representation == APTA_GRID_REPRESENTATION_SEGMENTS);
    CHECK(grid.segment_count == 1u);
    CHECK(grid.beat_count == 0u);
    CHECK(grid.segments != NULL);
    CHECK(grid.segments[0].nominal_tempo_millibpm >= 105000u);
    CHECK(grid.segments[0].nominal_tempo_millibpm <= 135000u);
    CHECK(grid.segments[0].revision != 0u);

    apta_grid_revision_view_init(&revision);
    CHECK(apta_result_get_grid_revision(result, &revision) == APTA_STATUS_OK);
    CHECK(revision.revision_id == grid.segments[0].revision);
    CHECK(revision.state == APTA_GRID_REVISION_APPLIED);
    CHECK(revision.proposed_segment_count == 1u);
    CHECK(revision.proposed_beat_count == 0u);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    return 0;
}

static int run_dynamic(apta_context_t *context)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    const apta_result_t *mid_result = NULL;
    const apta_result_t *final_result = NULL;
    apta_grid_view_t mid_grid;
    apta_grid_view_t final_grid;
    apta_grid_revision_view_t mid_revision;
    apta_grid_revision_view_t final_revision;
    uint32_t mid_segment_count = 0u;
    uint32_t mid_revision_id = 0u;
    uint32_t first;

    apta_session_config_init(&config);
    config.source_sample_rate = SAMPLE_RATE;
    config.channel_count = 1u;
    config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.total_frames = TOTAL_FRAMES;
    config.requested_features = features;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
    for (first = 0u; first < TOTAL_FRAMES; first += BLOCK_FRAMES) {
        uint32_t count = TOTAL_FRAMES - first;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        CHECK(fill_and_push(
                  session, first, count, 120000u, 150000u, &budget) == 0);
        if (first + count == SPLIT_FRAME) {
            mid_result = apta_session_acquire_result(session);
            CHECK(mid_result != NULL);
            apta_grid_view_init(&mid_grid);
            if (apta_result_get_beatgrid(
                    mid_result,
                    APTA_FEATURE_GLOBAL_BEATGRID,
                    NULL,
                    &mid_grid) == APTA_STATUS_OK) {
                mid_segment_count = mid_grid.segment_count;
                apta_grid_revision_view_init(&mid_revision);
                CHECK(apta_result_get_grid_revision(
                          mid_result,
                          &mid_revision) == APTA_STATUS_OK);
                mid_revision_id = mid_revision.revision_id;
            }
        }
    }
    CHECK(finish_session(session, &budget) == 0);

    final_result = apta_session_acquire_result(session);
    CHECK(final_result != NULL);
    apta_grid_view_init(&final_grid);
    CHECK(apta_result_get_beatgrid(
              final_result,
              APTA_FEATURE_GLOBAL_BEATGRID,
              NULL,
              &final_grid) == APTA_STATUS_OK);
    CHECK(final_grid.state == APTA_FEATURE_FINAL);
    CHECK(final_grid.representation == APTA_GRID_REPRESENTATION_HYBRID);
    CHECK(final_grid.segment_count >= 2u);
    CHECK(final_grid.segment_count <=
          APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS);
    CHECK(final_grid.beat_count != 0u);
    CHECK(final_grid.beat_count <= APTA_REFERENCE_GLOBAL_GRID_MAX_BEATS);
    CHECK(final_grid.beats != NULL);
    CHECK((final_grid.flags & APTA_GRID_FLAG_DYNAMIC_TEMPO) != 0u);
    CHECK(final_grid.segments[0].nominal_tempo_millibpm >= 105000u);
    CHECK(final_grid.segments[0].nominal_tempo_millibpm <= 135000u);
    CHECK(final_grid.segments[final_grid.segment_count - 1u]
              .nominal_tempo_millibpm >= 135000u);
    CHECK(final_grid.segments[final_grid.segment_count - 1u]
              .nominal_tempo_millibpm <= 170000u);
    {
        uint32_t index;
        for (index = 1u; index < final_grid.beat_count; ++index) {
            CHECK(final_grid.beats[index - 1u].position.whole_frame <=
                  final_grid.beats[index].position.whole_frame);
            CHECK(final_grid.beats[index - 1u].ordinal <
                  final_grid.beats[index].ordinal);
        }
    }

    apta_grid_revision_view_init(&final_revision);
    CHECK(apta_result_get_grid_revision(
              final_result,
              &final_revision) == APTA_STATUS_OK);
    CHECK(final_revision.state == APTA_GRID_REVISION_APPLIED);
    CHECK(final_revision.proposed_segment_count == final_grid.segment_count);
    CHECK(final_revision.proposed_beat_count == final_grid.beat_count);
    CHECK((final_revision.flags &
           APTA_GRID_REVISION_FLAG_DYNAMIC_TEMPO) != 0u);

    if (mid_result != NULL && mid_revision_id != 0u) {
        apta_grid_view_t retained_grid;
        apta_grid_revision_view_t retained_revision;
        apta_grid_view_init(&retained_grid);
        CHECK(apta_result_get_beatgrid(
                  mid_result,
                  APTA_FEATURE_GLOBAL_BEATGRID,
                  NULL,
                  &retained_grid) == APTA_STATUS_OK);
        apta_grid_revision_view_init(&retained_revision);
        CHECK(apta_result_get_grid_revision(
                  mid_result,
                  &retained_revision) == APTA_STATUS_OK);
        CHECK(retained_grid.segment_count == mid_segment_count);
        CHECK(retained_revision.revision_id == mid_revision_id);
        CHECK(final_revision.revision_id >= retained_revision.revision_id);
    }

    apta_result_release(mid_result);
    apta_result_release(final_result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    return 0;
}

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE;
    apta_context_config_t config;
    apta_context_t *context = NULL;

    apta_context_config_init(&config);
    config.requested_capabilities = capabilities;
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    CHECK(run_constant(context) == 0);
    CHECK(run_dynamic(context) == 0);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
