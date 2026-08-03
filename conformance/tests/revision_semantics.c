// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define SAMPLE_RATE 48000u
#define BEAT_FRAMES 23040u
#define TOTAL_FRAMES 524288u
#define LOCK_AFTER 327680u
#define BLOCK_FRAMES 4096u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int push_range(
    apta_session_t *session,
    uint32_t first,
    uint32_t end,
    apta_work_budget_t *budget)
{
    int16_t samples[BLOCK_FRAMES];

    while (first < end) {
        apta_pcm_block_t block;
        uint32_t count = end - first;
        uint32_t accepted = 0u;
        uint32_t index;
        apta_status_t status;

        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            samples[index] = ((first + index) % BEAT_FRAMES) < 192u
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
        status = apta_session_process(session, budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        first += count;
    }
    return 0;
}

int main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_GRID_LOCKING;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    apta_grid_view_t local_grid;
    apta_grid_revision_view_t revision;
    apta_frame_range_t lock_range;
    apta_status_t status;
    uint32_t revision_id;

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

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
    CHECK(push_range(session, 0u, LOCK_AFTER, &budget) == 0);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_grid_view_init(&local_grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &local_grid) == APTA_STATUS_OK);
    CHECK(local_grid.state == APTA_FEATURE_STABLE);
    CHECK(local_grid.segment_count > 0u);
    CHECK(local_grid.segments != NULL);
    CHECK(local_grid.segments[0].frames_per_beat.whole_frames > 0u);

    apta_frame_range_init(&lock_range);
    lock_range.first_frame = local_grid.applicability_range.first_frame +
                             BEAT_FRAMES;
    lock_range.end_frame = local_grid.applicability_range.end_frame -
                           BEAT_FRAMES;
    CHECK(lock_range.first_frame < lock_range.end_frame);
    apta_result_release(result);
    result = NULL;

    CHECK(apta_session_lock_grid_range(session, &lock_range) == APTA_STATUS_OK);
    CHECK(push_range(session, LOCK_AFTER, TOTAL_FRAMES, &budget) == 0);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_grid_revision_view_init(&revision);
    CHECK(apta_result_get_grid_revision(result, &revision) == APTA_STATUS_OK);
    CHECK(revision.state == APTA_GRID_REVISION_PENDING);
    CHECK((revision.flags &
           APTA_GRID_REVISION_FLAG_CONFLICTS_LOCKED_RANGE) != 0u);
    CHECK(revision.revision_id != 0u);
    revision_id = revision.revision_id;
    apta_result_release(result);
    result = NULL;

    CHECK(apta_session_apply_grid_revision(session, revision_id) ==
          APTA_STATUS_OK);
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_grid_revision_view_init(&revision);
    CHECK(apta_result_get_grid_revision(result, &revision) == APTA_STATUS_OK);
    CHECK(revision.revision_id == revision_id);
    CHECK(revision.state == APTA_GRID_REVISION_APPLIED);
    apta_grid_view_init(&local_grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &local_grid) == APTA_STATUS_OK);
    CHECK((local_grid.flags & APTA_GRID_FLAG_LOCKED) != 0u);
    CHECK(local_grid.segment_count > 0u);
    CHECK(local_grid.segments[0].revision == revision_id);
    apta_result_release(result);
    result = NULL;

    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_grid_revision_view_init(&revision);
    CHECK(apta_result_get_grid_revision(result, &revision) == APTA_STATUS_OK);
    CHECK(revision.state == APTA_GRID_REVISION_APPLIED);
    apta_grid_view_init(&local_grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &local_grid) == APTA_STATUS_OK);
    CHECK(local_grid.state == APTA_FEATURE_FINAL);
    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
