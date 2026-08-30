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
    0.748f, 0.060f, 0.488f, 0.082f, 0.674f, 0.460f,
    0.096f, 0.715f, 0.104f, 0.366f, 0.057f, 0.400f
};

static const float minor_profile[12] = {
    0.712f, 0.084f, 0.455f, 0.270f, 0.360f, 0.320f,
    0.082f, 0.600f, 0.059f, 0.291f, 0.092f, 0.260f
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

    /* Regression: a dominant-heavy C-major chroma (inflated G from bass and
     * V-harmony) must still resolve to C major, not G major. This was the
     * fifth-relation failure mode of the Krumhansl-Kessler profile. */
    memcpy(chroma, major_profile, sizeof(chroma));
    chroma[7] += 0.6f;
    status = apta_internal_key_select_chroma(
        chroma, 8u, candidates, &view);
    CHECK(status == APTA_STATUS_OK);
    CHECK(view.tonic == 0u);
    CHECK(view.mode == APTA_KEY_MODE_MAJOR);

    /* Regression: minor material with a strong fifth must stay minor on the
     * same tonic rather than flipping to the parallel major. */
    memcpy(chroma, minor_profile, sizeof(chroma));
    rotate_profile(chroma, minor_profile, 4u);
    chroma[11] += 0.3f;
    status = apta_internal_key_select_chroma(
        chroma, 8u, candidates, &view);
    CHECK(status == APTA_STATUS_OK);
    CHECK(view.tonic == 4u);
    CHECK(view.mode == APTA_KEY_MODE_MINOR);
    return EXIT_SUCCESS;
}

#ifdef APTA_INTERNAL_KEY_CENTERED_CORRELATION
static int check_common_mode_rejection(void)
{
    float chroma[12];
    apta_key_candidate_t candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT];
    apta_key_view_t view;
    apta_status_t status;
    uint32_t pitch;

    /* A constant broadband floor carries no tonal contrast. Raw cosine
     * similarity incorrectly changes this exact C-major profile to C minor
     * once the floor reaches 1.0; centered correlation must be invariant. */
    memcpy(chroma, major_profile, sizeof(chroma));
    for (pitch = 0u; pitch < 12u; ++pitch) {
        chroma[pitch] += 1.0f;
    }
    status = apta_internal_key_select_chroma(
        chroma, 8u, candidates, &view);
    CHECK(status == APTA_STATUS_OK);
    CHECK(view.tonic == 0u);
    CHECK(view.mode == APTA_KEY_MODE_MAJOR);

    rotate_profile(chroma, minor_profile, 9u);
    for (pitch = 0u; pitch < 12u; ++pitch) {
        chroma[pitch] += 1.0f;
    }
    status = apta_internal_key_select_chroma(
        chroma, 8u, candidates, &view);
    CHECK(status == APTA_STATUS_OK);
    CHECK(view.tonic == 9u);
    CHECK(view.mode == APTA_KEY_MODE_MINOR);
    return EXIT_SUCCESS;
}
#endif

#ifdef APTA_INTERNAL_KEY_TEMPORAL_CHORD
static void set_triad_chroma(
    float chroma[APTA_INTERNAL_KEY_PITCH_CLASSES],
    uint32_t tonic,
    apta_key_mode_t mode)
{
    const uint32_t third =
        (tonic + (mode == APTA_KEY_MODE_MAJOR ? 4u : 3u)) % 12u;
    const uint32_t fifth = (tonic + 7u) % 12u;

    memset(chroma, 0, sizeof(float) * APTA_INTERNAL_KEY_PITCH_CLASSES);
    chroma[tonic] = 1.0f;
    chroma[third] = 1.0f;
    chroma[fifth] = 0.7f;
}

static void add_temporal_chord(
    apta_internal_key_analysis_t *analysis,
    uint32_t tonic,
    apta_key_mode_t mode,
    uint32_t count)
{
    float chroma[APTA_INTERNAL_KEY_PITCH_CLASSES];
    uint32_t index;

    set_triad_chroma(chroma, tonic, mode);
    for (index = 0u; index < count; ++index) {
        apta_internal_key_temporal_vote_chroma(analysis, chroma);
    }
}

static int check_temporal_chord_state(void)
{
    apta_internal_key_analysis_t analysis;
    apta_key_candidate_t candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT];
    apta_key_view_t view;
    float invalid[APTA_INTERNAL_KEY_PITCH_CLASSES] = {0.0f};

    memset(&analysis, 0, sizeof(analysis));
    CHECK(apta_internal_key_select_temporal(
              &analysis, 10u, candidates, &view) ==
          APTA_STATUS_NOT_AVAILABLE);
    invalid[0] = -1.0f;
    apta_internal_key_temporal_vote_chroma(&analysis, invalid);
    CHECK(analysis.temporal_margin_sum == 0.0f);

    add_temporal_chord(&analysis, 0u, APTA_KEY_MODE_MAJOR, 4u);
    add_temporal_chord(&analysis, 7u, APTA_KEY_MODE_MAJOR, 2u);
    add_temporal_chord(&analysis, 5u, APTA_KEY_MODE_MAJOR, 2u);
    add_temporal_chord(&analysis, 9u, APTA_KEY_MODE_MINOR, 2u);
    CHECK(apta_internal_key_select_temporal(
              &analysis, 10u, candidates, &view) == APTA_STATUS_OK);
    CHECK(view.tonic == 0u);
    CHECK(view.mode == APTA_KEY_MODE_MAJOR);
    CHECK(view.state == APTA_FEATURE_STABLE);
    CHECK(candidates[0].score > candidates[1].score);

    memset(&analysis, 0, sizeof(analysis));
    add_temporal_chord(&analysis, 9u, APTA_KEY_MODE_MINOR, 4u);
    add_temporal_chord(&analysis, 4u, APTA_KEY_MODE_MAJOR, 2u);
    add_temporal_chord(&analysis, 2u, APTA_KEY_MODE_MINOR, 2u);
    add_temporal_chord(&analysis, 0u, APTA_KEY_MODE_MAJOR, 2u);
    CHECK(apta_internal_key_select_temporal(
              &analysis, 10u, candidates, &view) == APTA_STATUS_OK);
    CHECK(view.tonic == 9u);
    CHECK(view.mode == APTA_KEY_MODE_MINOR);
    return EXIT_SUCCESS;
}
#endif

#ifdef APTA_INTERNAL_KEY_TEMPORAL_PROFILE
static void add_temporal_profile(
    apta_internal_key_analysis_t *analysis,
    const float profile[APTA_INTERNAL_KEY_PITCH_CLASSES],
    uint32_t tonic,
    float scale,
    uint32_t count)
{
    float chroma[APTA_INTERNAL_KEY_PITCH_CLASSES];
    uint32_t pitch;
    uint32_t index;

    rotate_profile(chroma, profile, tonic);
    for (pitch = 0u; pitch < APTA_INTERNAL_KEY_PITCH_CLASSES; ++pitch) {
        chroma[pitch] *= scale;
    }
    for (index = 0u; index < count; ++index) {
        apta_internal_key_temporal_profile_add_chroma(analysis, chroma);
    }
}

static int check_temporal_profile_state(void)
{
    apta_internal_key_analysis_t analysis;
    apta_key_candidate_t candidates[APTA_INTERNAL_KEY_CANDIDATE_COUNT];
    apta_key_view_t view;
    float invalid[APTA_INTERNAL_KEY_PITCH_CLASSES] = {0.0f};
    float support_sum = 0.0f;
    uint32_t state;

    memset(&analysis, 0, sizeof(analysis));
    CHECK(apta_internal_key_select_temporal_profile(
              &analysis, 7u, candidates, &view) ==
          APTA_STATUS_NOT_AVAILABLE);
    invalid[0] = -1.0f;
    apta_internal_key_temporal_profile_add_chroma(&analysis, invalid);
    CHECK(analysis.temporal_profile_windows == 0u);

    /* Equal-window normalization makes the five quiet A-minor windows beat
     * two arbitrarily loud C-major windows. */
    add_temporal_profile(&analysis, major_profile, 0u, 1000.0f, 2u);
    add_temporal_profile(&analysis, minor_profile, 9u, 0.01f, 5u);
    CHECK(analysis.temporal_profile_windows == 7u);
    for (state = 0u; state < APTA_INTERNAL_KEY_GLOBAL_STATES; ++state) {
        support_sum += analysis.temporal_profile_support[state];
    }
    CHECK(fabsf(support_sum - 7.0f) < 1e-4f);
    CHECK(apta_internal_key_select_temporal_profile(
              &analysis, 7u, candidates, &view) == APTA_STATUS_OK);
    CHECK(view.tonic == 9u);
    CHECK(view.mode == APTA_KEY_MODE_MINOR);
    CHECK(view.state == APTA_FEATURE_STABLE);
    CHECK(candidates[0].score > candidates[1].score);
    return EXIT_SUCCESS;
}
#endif

#ifdef APTA_INTERNAL_KEY_HPCP
static int check_harmonic_projection(void)
{
    float spectrum[APTA_INTERNAL_KEY_BIN_COUNT] = {0.0f};
    float chroma[APTA_INTERNAL_KEY_PITCH_CLASSES];

    spectrum[0] = 13.0f;
    spectrum[19] = 11.0f;
    spectrum[28] = 13.0f;
    apta_internal_key_harmonic_chroma(spectrum, chroma);

    /* C3 retains its direct evidence and receives bounded support from the
     * G4 third harmonic and E5 fifth harmonic. The observed G/E bins remain
     * present as direct pitch evidence; this is a projection, not subtraction. */
    CHECK(fabsf(chroma[0] - (167.0f / 13.0f)) < 1e-5f);
    CHECK(fabsf(chroma[4] - 13.0f) < 1e-5f);
    CHECK(fabsf(chroma[7] - 11.0f) < 1e-5f);
    CHECK(fabsf(chroma[9] - (13.0f / 11.0f)) < 1e-5f);
    return EXIT_SUCCESS;
}
#endif

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
    /* The synthetic fixture is exactly A440. The experimental tuning bank
     * must therefore retain the centre hypothesis at every publication. */
    CHECK(view_out->tuning_offset_cents == 0);
    CHECK(view_out->candidates[0].tuning_offset_cents == 0);
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
#ifdef APTA_INTERNAL_KEY_CENTERED_CORRELATION
    CHECK(check_common_mode_rejection() == EXIT_SUCCESS);
#endif
#ifdef APTA_INTERNAL_KEY_HPCP
    CHECK(check_harmonic_projection() == EXIT_SUCCESS);
#endif
#ifdef APTA_INTERNAL_KEY_TEMPORAL_CHORD
    CHECK(check_temporal_chord_state() == EXIT_SUCCESS);
#endif
#ifdef APTA_INTERNAL_KEY_TEMPORAL_PROFILE
    CHECK(check_temporal_profile_state() == EXIT_SUCCESS);
#endif
    CHECK(check_progressive_publication() == EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
