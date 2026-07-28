// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define BIN_FRAMES 256u
#define TOTAL_BINS 1024u
#define TOTAL_FRAMES (BIN_FRAMES * TOTAL_BINS)
#define BLOCK_FRAMES 4096u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint32_t expected_tempo(uint32_t sample_rate, uint32_t lag_bins)
{
    uint64_t numerator = (uint64_t)sample_rate * UINT64_C(60000);
    uint64_t denominator = (uint64_t)lag_bins * BIN_FRAMES;
    return (uint32_t)((numerator + denominator / 2u) / denominator);
}

static int run_vector(
    apta_context_t *context,
    uint32_t sample_rate,
    uint32_t lag_bins)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    apta_tempo_view_t tempo;
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    uint32_t beat_frames = lag_bins * BIN_FRAMES;
    uint32_t expected = expected_tempo(sample_rate, lag_bins);
    apta_status_t status;

    apta_session_config_init(&config);
    config.source_sample_rate = sample_rate;
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
            samples[index] = ((first + index) % beat_frames) < 128u
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
    CHECK(tempo.selected.tempo_millibpm + 1u >= expected);
    CHECK(tempo.selected.tempo_millibpm <= expected + 1u);
    CHECK(tempo.selected.state == APTA_FEATURE_FINAL);
    CHECK(tempo.selected.confidence >= 50u);
    CHECK(tempo.candidate_count >= 1u);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    return 0;
}

int main(void)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    const uint32_t lags[] = {45u, 60u, 75u, 90u, 100u, 125u, 150u, 180u, 225u, 281u};
    apta_context_config_t config;
    apta_context_t *context = NULL;
    uint32_t index;

    apta_context_config_init(&config);
    config.requested_capabilities = capabilities;
    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);

    for (index = 0u; index < sizeof(lags) / sizeof(lags[0]); ++index) {
        CHECK(run_vector(context, 48000u, lags[index]) == 0);
    }
    CHECK(run_vector(context, 44100u, 90u) == 0);

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
