// SPDX-License-Identifier: Apache-2.0
/* Phase 7: S4/S6 refresh work must honor the shared process-step budget,
 * publish only complete evidence generations, finish a one-step final drain,
 * and remain cancellable between correlation slices. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_internal.h"
#include "apta_s6_internal.h"

#define SAMPLE_RATE 48000u
#define BEAT_FRAMES 23040u
#define TOTAL_FRAMES 524288u
#define BLOCK_FRAMES 4096u
#define MAX_DRAIN_CALLS 4096u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t tempo_millibpm;
    uint32_t local_tempo_millibpm;
    uint64_t local_anchor;
    uint32_t global_segment_count;
    apta_grid_segment_t global_segments[
        APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS];
} analysis_snapshot_t;

static int push_clicks(
    apta_session_t *session,
    uint32_t total_frames,
    apta_work_budget_t *budget)
{
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;

    while (first < total_frames) {
        apta_pcm_block_t block;
        apta_progress_t progress;
        uint32_t count = total_frames - first;
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
        apta_progress_init(&progress);
        status = apta_session_process(session, budget, &progress);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        CHECK(budget->maximum_steps == 0u ||
              progress.completed_steps <= budget->maximum_steps);
        first += count;
    }
    return 0;
}

static uint64_t APTA_CALL fake_clock(void *user_data)
{
    uint64_t *now = (uint64_t *)user_data;

    *now += UINT64_C(1000);
    return *now;
}

static int create_session(
    apta_feature_mask_t features,
    uint32_t total_frames,
    uint64_t *clock_state,
    apta_context_t **context_out,
    apta_session_t **session_out)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    if (clock_state != NULL) {
        context_config.clock.user_data = clock_state;
        context_config.clock.monotonic_time_ns = fake_clock;
    }
    CHECK(apta_context_create(&context_config, context_out) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = total_frames;
    session_config.requested_features = features;
    CHECK(apta_session_create(*context_out, &session_config, session_out) ==
          APTA_STATUS_OK);
    return 0;
}

static int capture_final(
    apta_session_t *session,
    analysis_snapshot_t *snapshot)
{
    const apta_result_t *result = apta_session_acquire_result(session);
    apta_tempo_view_t tempo;
    apta_grid_view_t local_grid;
    apta_grid_view_t global_grid;

    CHECK(result != NULL);
    memset(snapshot, 0, sizeof(*snapshot));
    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.state == APTA_FEATURE_FINAL);
    snapshot->tempo_millibpm = tempo.selected.tempo_millibpm;

    apta_grid_view_init(&local_grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &local_grid) == APTA_STATUS_OK);
    CHECK(local_grid.state == APTA_FEATURE_FINAL);
    CHECK(local_grid.segment_count == 1u);
    snapshot->local_tempo_millibpm =
        local_grid.segments[0].nominal_tempo_millibpm;
    snapshot->local_anchor =
        local_grid.segments[0].anchor_position.whole_frame;

    apta_grid_view_init(&global_grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_GLOBAL_BEATGRID,
              NULL,
              &global_grid) == APTA_STATUS_OK);
    CHECK(global_grid.state == APTA_FEATURE_FINAL);
    CHECK(global_grid.segment_count != 0u);
    CHECK(global_grid.segment_count <=
          APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS);
    snapshot->global_segment_count = global_grid.segment_count;
    memcpy(snapshot->global_segments,
           global_grid.segments,
           (size_t)global_grid.segment_count *
               sizeof(snapshot->global_segments[0]));
    apta_result_release(result);
    return 0;
}

static int snapshots_equal(
    const analysis_snapshot_t *left,
    const analysis_snapshot_t *right)
{
    uint32_t index;

    if (left->tempo_millibpm != right->tempo_millibpm ||
        left->local_tempo_millibpm != right->local_tempo_millibpm ||
        left->local_anchor != right->local_anchor ||
        left->global_segment_count != right->global_segment_count) {
        return 0;
    }
    for (index = 0u; index < left->global_segment_count; ++index) {
        const apta_grid_segment_t *a = &left->global_segments[index];
        const apta_grid_segment_t *b = &right->global_segments[index];

        if (a->nominal_tempo_millibpm != b->nominal_tempo_millibpm ||
            a->anchor_position.whole_frame !=
                b->anchor_position.whole_frame ||
            a->anchor_position.fraction_q32 !=
                b->anchor_position.fraction_q32 ||
            a->confidence != b->confidence ||
            a->flags != b->flags ||
            a->applicability_range.first_frame !=
                b->applicability_range.first_frame ||
            a->applicability_range.end_frame !=
                b->applicability_range.end_frame) {
            return 0;
        }
    }
    return 1;
}

static int verify_one_step_final_drain(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_GLOBAL_BEATGRID |
        APTA_FEATURE_DYNAMIC_TEMPO |
        APTA_FEATURE_CONFIDENCE;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    apta_status_t status;
    uint32_t calls = 0u;
    int observed_s4_partial = 0;
    int observed_s6_partial = 0;
    analysis_snapshot_t bounded_snapshot;
    analysis_snapshot_t unbounded_snapshot;

    CHECK(create_session(
              features, TOTAL_FRAMES, NULL, &context, &session) == 0);
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
    CHECK(push_clicks(session, TOTAL_FRAMES, &budget) == 0);
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);

    budget.maximum_steps = 1u;
    do {
        apta_progress_t progress;
        const uint64_t s4_mutation_before = session->s4_mutation_serial;
        const uint64_t s6_mutation_before = session->s6->mutation_serial;
        const int s4_active_before = session->s4_refresh_active != 0u;
        const int s6_active_before = session->s6->refresh_stage != 0u;

        apta_progress_init(&progress);
        status = apta_session_process(session, &budget, &progress);
        CHECK(status == APTA_STATUS_OK ||
              status == APTA_STATUS_MORE_WORK ||
              status == APTA_STATUS_END_OF_INPUT);
        CHECK(progress.completed_steps <= 1u);

        if (session->s4_refresh_active != 0u) {
            observed_s4_partial = 1;
            CHECK(session->s4_mutation_serial == s4_mutation_before);
        } else if (s4_active_before) {
            CHECK(session->s4_mutation_serial >= s4_mutation_before);
        }
        if (session->s6->refresh_stage != 0u) {
            observed_s6_partial = 1;
            CHECK(session->s6->mutation_serial == s6_mutation_before);
        } else if (s6_active_before) {
            CHECK(session->s6->mutation_serial >= s6_mutation_before);
        }
        calls += 1u;
        CHECK(calls < MAX_DRAIN_CALLS);
    } while (status != APTA_STATUS_END_OF_INPUT);

    CHECK(observed_s4_partial);
    CHECK(observed_s6_partial);
    CHECK(!apta_internal_analysis_pending(session));
    CHECK(capture_final(session, &bounded_snapshot) == 0);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);

    /* The same final evidence generation, processed without a step limit,
     * must produce the same semantic result as the one-step state machines. */
    context = NULL;
    session = NULL;
    CHECK(create_session(
              features, TOTAL_FRAMES, NULL, &context, &session) == 0);
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
    CHECK(push_clicks(session, TOTAL_FRAMES, &budget) == 0);
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    budget.maximum_steps = 0u;
    do {
        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK ||
              status == APTA_STATUS_MORE_WORK ||
              status == APTA_STATUS_END_OF_INPUT);
    } while (status != APTA_STATUS_END_OF_INPUT);
    CHECK(capture_final(session, &unbounded_snapshot) == 0);
    CHECK(snapshots_equal(&bounded_snapshot, &unbounded_snapshot));
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

static int verify_mid_refresh_cancellation(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    apta_progress_t progress;
    uint64_t mutation_before;

    CHECK(create_session(
              features, TOTAL_FRAMES, NULL, &context, &session) == 0);
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;
    CHECK(push_clicks(session, TOTAL_FRAMES, &budget) == 0);
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);

    mutation_before = session->s4_mutation_serial;
    budget.maximum_steps = 1u;
    apta_progress_init(&progress);
    CHECK(apta_session_process(session, &budget, &progress) ==
          APTA_STATUS_MORE_WORK);
    CHECK(progress.completed_steps == 1u);
    CHECK(session->s4_refresh_active != 0u);
    CHECK(session->s4_mutation_serial == mutation_before);

    apta_session_request_cancel(session);
    apta_progress_init(&progress);
    CHECK(apta_session_process(session, &budget, &progress) ==
          APTA_ERROR_CANCELLED);
    CHECK(session->s4_mutation_serial == mutation_before);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

static int verify_soft_deadline_reaches_analysis(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    apta_progress_t progress;
    apta_status_t status;
    uint64_t clock_state = 0u;

    CHECK(create_session(
              features,
              TOTAL_FRAMES,
              &clock_state,
              &context,
              &session) == 0);
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;
    CHECK(push_clicks(session, TOTAL_FRAMES, &budget) == 0);
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);

    /* The first clock sample establishes a 1-us deadline and the S4 wrapper's
     * next sample reaches it. With no PCM left, zero completed steps proves
     * that the deadline created at the public boundary also governs analysis
     * work rather than only waveform input chunks. */
    budget.maximum_steps = 0u;
    budget.soft_time_budget_us = 1u;
    apta_progress_init(&progress);
    status = apta_session_process(session, &budget, &progress);
    CHECK(status == APTA_STATUS_MORE_WORK);
    CHECK(progress.completed_steps == 0u);
    CHECK(apta_internal_s4_refresh_pending(session));

    budget.soft_time_budget_us = 0u;
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

int main(void)
{
    CHECK(verify_one_step_final_drain() == 0);
    CHECK(verify_mid_refresh_cancellation() == 0);
    CHECK(verify_soft_deadline_reaches_analysis() == 0);
    return 0;
}
