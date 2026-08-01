// SPDX-License-Identifier: Apache-2.0
/*
 * A beat period that falls between onset bins must still be reported.
 *
 * The lag search is an integer argmax over 256-frame bins, so on its own it can
 * only report the tempi those bins land on. Near 128 BPM they are 1.6 BPM
 * apart, and since the published grid is one anchor plus a constant period, a
 * tempo error that large slips half a beat inside two minutes.
 *
 * The click train here beats every 20,608 frames, which is exactly 80.5 bins.
 * That is the worst case: the true period sits halfway between lag 80
 * (129.199 BPM) and lag 81 (127.604 BPM), so an integer-only estimator is
 * wrong by about 0.8 BPM whichever it picks.
 */
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define SAMPLE_RATE 44100u
#define BIN_FRAMES 256u
/* 80.5 bins: deliberately between two integer lags. */
#define BEAT_FRAMES 20608u
/* Long enough that the refinement can measure across its full span of beats and
 * still keep half the evidence, and short enough to stay inside the onset
 * ring. */
#define TOTAL_BINS 3072u
#define TOTAL_FRAMES (BIN_FRAMES * TOTAL_BINS)
#define BLOCK_FRAMES 4096u

/* 44100 * 60000 / 20608 */
#define EXPECTED_MILLIBPM 128397u
/* What the two neighbouring integer lags would report. */
#define LAG_80_MILLIBPM 129199u
#define LAG_81_MILLIBPM 127604u
/* Comfortably inside either neighbour, well outside the refinement's own
 * resolution of roughly 100 millibpm at this tempo. */
#define TOLERANCE_MILLIBPM 300u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    apta_tempo_view_t tempo;
    static int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    uint32_t reported;
    uint32_t error;
    apta_status_t status;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

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
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.state == APTA_FEATURE_FINAL);

    reported = tempo.selected.tempo_millibpm;
    error = reported > EXPECTED_MILLIBPM ? reported - EXPECTED_MILLIBPM
                                         : EXPECTED_MILLIBPM - reported;
    if (error > TOLERANCE_MILLIBPM) {
        fprintf(stderr,
                "tempo %u millibpm, expected %u +/- %u (error %u).\n"
                "The neighbouring integer lags report %u and %u; landing on\n"
                "one of those means the sub-bin refinement did not run.\n",
                reported, EXPECTED_MILLIBPM, TOLERANCE_MILLIBPM, error,
                LAG_81_MILLIBPM, LAG_80_MILLIBPM);
        return 1;
    }

    /* The grid period must carry the refinement too, or the extra precision
     * stops at the tempo field and the grid still drifts. */
    {
        apta_grid_view_t grid;
        apta_grid_view_init(&grid);
        if (apta_result_get_beatgrid(
                result, APTA_FEATURE_LOCAL_BEATGRID, NULL, &grid) ==
                APTA_STATUS_OK &&
            grid.segment_count > 0u) {
            const apta_grid_segment_t *segment = &grid.segments[0];
            const uint64_t whole = segment->frames_per_beat.whole_frames;

            CHECK(whole + 1u >= BEAT_FRAMES);
            CHECK(whole <= BEAT_FRAMES);
        }
    }

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
