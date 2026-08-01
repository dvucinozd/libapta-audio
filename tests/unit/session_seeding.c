// SPDX-License-Identifier: Apache-2.0
/*
 * D1: resume analysis from a parsed result.
 *
 * Analyses half a track, serializes, creates a fresh session, seeds it,
 * analyses the remainder, and compares the final overview against an
 * uninterrupted run of the same input.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define COL APTA_TEST_COLUMN_FRAMES
#define RATE 44100u
#define TOTAL_COLUMNS 16u
#define TOTAL_FRAMES (COL * TOTAL_COLUMNS)
#define HALF_FRAMES (COL * (TOTAL_COLUMNS / 2u))

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void configure(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = RATE;
    config->channel_count = 1u;
    config->sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config->total_frames = TOTAL_FRAMES;
    config->requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
}

/* Push [first, end) and drive processing. */
static int feed(apta_session_t *session,
                const float *audio,
                uint32_t first,
                uint32_t end)
{
    apta_work_budget_t budget;
    uint32_t pushed = first;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 2048u;
    budget.maximum_steps = 64u;
    while (pushed < end) {
        apta_pcm_block_t block;
        uint32_t count = end - pushed;
        uint32_t accepted = 0u;

        if (count > 2048u) {
            count = 2048u;
        }
        apta_pcm_block_init(&block);
        block.data = &audio[pushed];
        block.first_frame = pushed;
        block.frame_count = count;
        if (apta_session_push_pcm(session, &block, &accepted) !=
                APTA_STATUS_OK ||
            accepted != count) {
            return 1;
        }
        pushed += count;
        if (apta_session_process(session, &budget, NULL) < 0) {
            return 1;
        }
    }
    return 0;
}

static int drain(apta_session_t *session)
{
    apta_work_budget_t budget;
    apta_status_t status;
    uint32_t guard;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 2048u;
    budget.maximum_steps = 64u;
    if (apta_session_signal_end_of_input(session, TOTAL_FRAMES) !=
        APTA_STATUS_OK) {
        return 1;
    }
    for (guard = 0u; guard < 2000u; ++guard) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            return 0;
        }
        if (status < 0) {
            return 1;
        }
    }
    return 1;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    const apta_result_t *checkpoint = NULL;
    apta_serialize_options_t serialize_options;
    apta_parse_options_t parse_options;
    apta_waveform_overview_view_t reference;
    apta_waveform_overview_view_t resumed;
    float *audio;
    uint8_t *serialized;
    size_t written = 0u;
    const size_t capacity = 256u * 1024u;
    apta_waveform_column_t expected[TOTAL_COLUMNS];
    uint32_t index;

    audio = (float *)malloc((size_t)TOTAL_FRAMES * sizeof(*audio));
    CHECK(audio != NULL);
    for (index = 0u; index < TOTAL_FRAMES; ++index) {
        audio[index] = ((index % 977u) < 64u) ? 0.7f : -0.25f;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    /* 1. Uninterrupted reference run. */
    configure(&config);
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(feed(session, audio, 0u, TOTAL_FRAMES) == 0);
    CHECK(drain(session) == 0);
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&reference);
    CHECK(apta_result_get_waveform_overview(result, 0u, &reference) ==
          APTA_STATUS_OK);
    CHECK(reference.span_count == 1u);
    CHECK(reference.spans[0].column_count == TOTAL_COLUMNS);
    for (index = 0u; index < TOTAL_COLUMNS; ++index) {
        expected[index] = reference.spans[0].columns[index];
    }
    apta_result_release(result);
    result = NULL;
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    /* 2. Analyse the first half and serialize the partial result. */
    configure(&config);
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(feed(session, audio, 0u, HALF_FRAMES) == 0);
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    serialized = (uint8_t *)malloc(capacity);
    CHECK(serialized != NULL);
    apta_serialize_options_init(&serialize_options);
    CHECK(apta_result_serialize(result, &serialize_options, serialized,
                                capacity, &written) == APTA_STATUS_OK);
    apta_result_release(result);
    result = NULL;
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    apta_parse_options_init(&parse_options);
    CHECK(apta_result_parse(context, &parse_options, serialized, written,
                            &checkpoint) == APTA_STATUS_OK);
    CHECK(checkpoint != NULL);

    /* 3. Seeding is rejected outside APTA_SESSION_CREATED and on mismatch. */
    configure(&config);
    config.overview_frames_per_column = COL * 2u;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(apta_session_seed_from_result(session, checkpoint) ==
          APTA_ERROR_CONFLICT);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    configure(&config);
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(apta_session_seed_from_result(session, NULL) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_session_seed_from_result(NULL, checkpoint) ==
          APTA_ERROR_INVALID_ARGUMENT);

    /* 4. Seed, then analyse only the remainder. */
    CHECK(apta_session_seed_from_result(session, checkpoint) ==
          APTA_STATUS_OK);
    /* Once the session has left CREATED, seeding is refused. */
    CHECK(feed(session, audio, HALF_FRAMES, TOTAL_FRAMES) == 0);
    CHECK(apta_session_seed_from_result(session, checkpoint) ==
          APTA_ERROR_INVALID_STATE);
    CHECK(drain(session) == 0);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&resumed);
    CHECK(apta_result_get_waveform_overview(result, 0u, &resumed) ==
          APTA_STATUS_OK);

    /* Coverage is complete and contiguous, as in the uninterrupted run. */
    CHECK(resumed.state == APTA_FEATURE_FINAL);
    CHECK(resumed.span_count == 1u);
    CHECK(resumed.spans[0].source_range.first_frame == 0u);
    CHECK(resumed.spans[0].source_range.end_frame == TOTAL_FRAMES);
    CHECK(resumed.spans[0].column_count == TOTAL_COLUMNS);

    /* Columns analysed after the seed must match the reference exactly. The
     * seeded half is reconstructed from quantized container values, so it is
     * compared with a tolerance of one count. */
    for (index = 0u; index < TOTAL_COLUMNS; ++index) {
        const apta_waveform_column_t *a = &expected[index];
        const apta_waveform_column_t *b = &resumed.spans[0].columns[index];

        if (index >= TOTAL_COLUMNS / 2u) {
            CHECK(b->minimum == a->minimum);
            CHECK(b->maximum == a->maximum);
            CHECK(b->rms == a->rms);
        } else {
            CHECK(b->minimum >= a->minimum - 1 && b->minimum <= a->minimum + 1);
            CHECK(b->maximum >= a->maximum - 1 && b->maximum <= a->maximum + 1);
            CHECK((b->rms > a->rms ? b->rms - a->rms : a->rms - b->rms) <= 1u);
        }
        CHECK((b->flags & APTA_WAVEFORM_COLUMN_VALID) != 0u);
    }

    apta_result_release(result);
    apta_result_release(checkpoint);
    free(serialized);
    free(audio);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
