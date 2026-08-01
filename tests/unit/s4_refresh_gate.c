// SPDX-License-Identifier: Apache-2.0
/* A2: the tempo refresh is gated on evidence growth. Gating the computation
 * must not gate publication: generations must still appear as the analysis
 * progresses, and the final state must still reach APTA_FEATURE_FINAL. */
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define TOTAL_FRAMES 288000u
#define BLOCK_FRAMES 512u
#define BEAT_FRAMES 23040u

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
    apta_work_budget_t budget;
    apta_tempo_view_t tempo;
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    apta_generation_t last_generation = 0u;
    uint32_t tempo_generations = 0u;
    unsigned long process_calls = 0ul;
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

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;

    /* Small increments: one block advances the evidence range by two
     * 256-frame onset bins, well under the gate's minimum, so most calls take
     * the gated path. */
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
        process_calls += 1ul;
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);

        result = apta_session_acquire_result(session);
        if (result != NULL) {
            const apta_generation_t generation =
                apta_result_get_generation(result);
            apta_tempo_view_init(&tempo);
            if (generation != last_generation &&
                apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK &&
                tempo.selected.state != APTA_FEATURE_ABSENT) {
                tempo_generations += 1u;
                last_generation = generation;
            }
            apta_result_release(result);
        }

        first += count;
    }

    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
        process_calls += 1ul;
        CHECK(status >= 0);
    } while (status != APTA_STATUS_END_OF_INPUT);

    /* The gate must let the last estimate through, or the state never leaves
     * STABLE. Checked first because it is the assertion that fails when the
     * draining and completed bypasses are dropped: verified by rebuilding with
     * -DAPTA_INTERNAL_S4_REFRESH_MIN_NEW_BINS=1000000u, where this check
     * passes with the bypasses and fails without them. */
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.state == APTA_FEATURE_FINAL);
    CHECK(tempo.selected.tempo_millibpm != 0u);
    apta_result_release(result);

    /* Publication still happens while the computation is gated: the estimate
     * is reused, but the ranges and the published generation keep moving. */
    CHECK(tempo_generations > 1u);

    /* Small increments, so the gate had many opportunities to skip. */
    CHECK(process_calls > 400ul);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
