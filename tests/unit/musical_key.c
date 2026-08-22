// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "../../src/key/apta_key_internal.h"

#define SAMPLE_RATE 48000u
#define BLOCK_FRAMES 4096u
#define PROVISIONAL_END ((uint64_t)SAMPLE_RATE)
#define STABLE_END ((uint64_t)SAMPLE_RATE * 5u)
#define FINAL_END ((uint64_t)SAMPLE_RATE * 6u)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return EXIT_FAILURE;                                             \
        }                                                                    \
    } while (0)

static const float major_profile[12] = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
    2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};

static const float minor_profile[12] = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
    2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

static void rotate_profile(
    float out[12],
    const float profile[12],
    uint32_t tonic)
{
    uint32_t pitch;
    for (pitch = 0u; pitch < 12u; ++pitch) {
        out[pitch] = profile[(pitch + 12u - tonic) % 12u];
    }
}

static int check_selector(void)
{
    float chroma[12];
    apta_key_candidate_t candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT];
    apta_key_view_t view;
    apta_status_t status;

    memcpy(chroma, major_profile, sizeof(chroma));
    status = apta_internal_key_select_chroma(
        chroma, 4u, candidates, &view);
    CHECK(status == APTA_STATUS_OK);
    CHECK(view.tonic == 0u);
    CHECK(view.mode == APTA_KEY_MODE_MAJOR);
    CHECK(view.state == APTA_FEATURE_STABLE);
    CHECK(view.candidate_count == APTA_INTERNAL_KEY_CANDIDATE_COUNT);
    CHECK(candidates[0].score >= candidates[1].score);
    CHECK(candidates[1].score >= candidates[2].score);

    rotate_profile(chroma, minor_profile, 9u);
    status = apta_internal_key_select_chroma(
        chroma, 1u, candidates, &view);
    CHECK(status == APTA_STATUS_OK);
    CHECK(view.tonic == 9u);
    CHECK(view.mode == APTA_KEY_MODE_MINOR);
    CHECK(view.state == APTA_FEATURE_PROVISIONAL);

    memset(chroma, 0, sizeof(chroma));
    status = apta_internal_key_select_chroma(
        chroma, 4u, candidates, &view);
    CHECK(status == APTA_STATUS_NOT_AVAILABLE);
    return EXIT_SUCCESS;
}

static int16_t chord_sample(uint64_t frame)
{
    const float t = (float)(frame % SAMPLE_RATE) / (float)SAMPLE_RATE;
    const float two_pi = 6.28318530718f;
    const float sample =
        0.30f * sinf(two_pi * 261.6256f * t) +
        0.25f * sinf(two_pi * 329.6276f * t) +
        0.25f * sinf(two_pi * 391.9954f * t);
    float scaled = sample * 30000.0f;

    if (scaled > 32767.0f) {
        scaled = 32767.0f;
    } else if (scaled < -32768.0f) {
        scaled = -32768.0f;
    }
    return (int16_t)scaled;
}

static int push_range(apta_session_t *session, uint64_t first, uint64_t end)
{
    int16_t samples[BLOCK_FRAMES];
    apta_work_budget_t budget;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;

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
            samples[index] = chord_sample(first + index);
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
    return EXIT_SUCCESS;
}

static int read_key(
    const apta_result_t *result,
    apta_feature_state_t expected_state,
    apta_key_view_t *view_out)
{
    apta_feature_state_t state;
    apta_confidence_value_t confidence;

    apta_key_view_init(view_out);
    CHECK(apta_result_get_key(result, NULL, view_out) == APTA_STATUS_OK);
    CHECK(view_out->state == expected_state);
    CHECK(view_out->candidate_count == APTA_INTERNAL_KEY_CANDIDATE_COUNT);
    CHECK(view_out->candidates != NULL);
    CHECK(view_out->candidates[0].score >= view_out->candidates[1].score);
    CHECK(view_out->candidates[1].score >= view_out->candidates[2].score);
    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_MUSICAL_KEY,
              NULL,
              &state,
              &confidence) == APTA_STATUS_OK);
    CHECK(state == expected_state);
    CHECK(confidence == view_out->confidence);
    return EXIT_SUCCESS;
}

static int check_progressive_publication(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_MUSICAL_KEY;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *provisional = NULL;
    const apta_result_t *stable = NULL;
    const apta_result_t *final_result = NULL;
    apta_key_view_t provisional_view;
    apta_key_view_t stable_view;
    apta_key_view_t final_view;
    apta_generation_t provisional_generation;
    apta_generation_t stable_generation;
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
    session_config.total_frames = FINAL_END;
    session_config.requested_features = features;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    CHECK(push_range(session, 0u, PROVISIONAL_END) == EXIT_SUCCESS);
    provisional = apta_session_acquire_result(session);
    CHECK(provisional != NULL);
    CHECK(read_key(provisional, APTA_FEATURE_PROVISIONAL, &provisional_view) ==
          EXIT_SUCCESS);
    provisional_generation = apta_result_get_generation(provisional);

    CHECK(push_range(session, PROVISIONAL_END, STABLE_END) == EXIT_SUCCESS);
    stable = apta_session_acquire_result(session);
    CHECK(stable != NULL);
    CHECK(read_key(stable, APTA_FEATURE_STABLE, &stable_view) == EXIT_SUCCESS);
    stable_generation = apta_result_get_generation(stable);
    CHECK(stable_generation > provisional_generation);

    /* Immutable publication contract: a retained quick-pass generation cannot
     * be rewritten when the full-pass evidence matures. */
    CHECK(apta_result_get_generation(provisional) == provisional_generation);
    CHECK(read_key(provisional, APTA_FEATURE_PROVISIONAL, &provisional_view) ==
          EXIT_SUCCESS);

    CHECK(push_range(session, STABLE_END, FINAL_END) == EXIT_SUCCESS);
    CHECK(apta_session_signal_end_of_input(session, FINAL_END) ==
          APTA_STATUS_OK);
    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;
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

    final_result = apta_session_acquire_result(session);
    CHECK(final_result != NULL);
    CHECK(read_key(final_result, APTA_FEATURE_FINAL, &final_view) == EXIT_SUCCESS);
    CHECK(apta_result_get_generation(final_result) > stable_generation);

    CHECK(apta_result_get_generation(provisional) == provisional_generation);
    CHECK(read_key(provisional, APTA_FEATURE_PROVISIONAL, &provisional_view) ==
          EXIT_SUCCESS);
    CHECK(apta_result_get_generation(stable) == stable_generation);
    CHECK(read_key(stable, APTA_FEATURE_STABLE, &stable_view) == EXIT_SUCCESS);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(read_key(provisional, APTA_FEATURE_PROVISIONAL, &provisional_view) ==
          EXIT_SUCCESS);
    CHECK(read_key(stable, APTA_FEATURE_STABLE, &stable_view) == EXIT_SUCCESS);
    CHECK(read_key(final_result, APTA_FEATURE_FINAL, &final_view) == EXIT_SUCCESS);

    apta_result_release(provisional);
    apta_result_release(stable);
    apta_result_release(final_result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return EXIT_SUCCESS;
}

int main(void)
{
    CHECK(check_selector() == EXIT_SUCCESS);
    CHECK(check_progressive_publication() == EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
