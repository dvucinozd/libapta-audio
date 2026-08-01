// SPDX-License-Identifier: Apache-2.0
/* A4: WAVEFORM_OVERVIEW | CONFIDENCE must not activate the tempo engine.
 * The overview reports its own coverage confidence, and nothing fabricates a
 * tempo the host never asked for. */
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define TOTAL_FRAMES 132096u
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

int main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_CONFIDENCE;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    apta_waveform_overview_view_t overview;
    apta_tempo_view_t tempo;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    apta_confidence_value_t partial_confidence;
    apta_status_t status;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    /* CONFIDENCE must still be advertised as a capability. */
    CHECK((apta_context_get_capabilities(context) &
           APTA_FEATURE_CONFIDENCE) != 0u);

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

    /* Halfway through, coverage confidence must be partial: neither unknown
     * nor complete. */
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

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    partial_confidence = overview.confidence;
    apta_result_release(result);
    CHECK(partial_confidence != APTA_CONFIDENCE_UNKNOWN);

    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
        CHECK(status >= 0);
    } while (status != APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    /* The overview carries a meaningful confidence of its own. */
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.state == APTA_FEATURE_FINAL);
    CHECK(overview.confidence != APTA_CONFIDENCE_UNKNOWN);
    CHECK(overview.confidence <= APTA_CONFIDENCE_MAX);
    /* Complete coverage, so it must report full confidence and must not have
     * gone backwards from the partial reading. */
    CHECK(overview.confidence == APTA_CONFIDENCE_MAX);
    CHECK(overview.confidence >= partial_confidence);

    /* The same value reaches the generic accessor. */
    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_WAVEFORM_OVERVIEW,
              NULL,
              &state,
              &confidence) == APTA_STATUS_OK);
    CHECK(state == APTA_FEATURE_FINAL);
    CHECK(confidence == overview.confidence);

    /* No tempo was requested, so none may be reported. */
    apta_tempo_view_init(&tempo);
    status = apta_result_get_tempo(result, NULL, &tempo);
    CHECK(status == APTA_STATUS_NOT_AVAILABLE ||
          (status == APTA_STATUS_OK &&
           tempo.selected.state == APTA_FEATURE_ABSENT));
    CHECK((apta_result_get_available_features(result) &
           APTA_FEATURE_BPM) == 0u);

    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_BPM,
              NULL,
              &state,
              &confidence) == APTA_STATUS_NOT_AVAILABLE);
    CHECK(state == APTA_FEATURE_ABSENT);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
