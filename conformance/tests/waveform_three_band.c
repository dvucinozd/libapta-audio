// SPDX-License-Identifier: Apache-2.0
/*
 * C1: three-band overview waveform.
 *
 * Covers the three things the feature has to get right: it is accepted at all,
 * the bands actually respond to spectral content in the right order, and the
 * values survive a serialize/parse round trip with HAS_3BAND set.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define COL APTA_TEST_COLUMN_FRAMES
#define RATE 44100u
/* Three columns per tone, three tones. */
#define COLUMNS_PER_TONE 3u
#define TONE_FRAMES (COL * COLUMNS_PER_TONE)
#define TOTAL_FRAMES (TONE_FRAMES * 3u)

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* Corners are 200 Hz and 2 kHz, so these sit clear of both. */
static const double g_tone_hz[3] = {60.0, 800.0, 6000.0};

static void render(float *out)
{
    uint32_t tone;
    uint32_t i;

    for (tone = 0u; tone < 3u; ++tone) {
        for (i = 0u; i < TONE_FRAMES; ++i) {
            const double t = (double)i / (double)RATE;
            out[tone * TONE_FRAMES + i] =
                (float)(0.7 * sin(6.283185307 * g_tone_hz[tone] * t));
        }
    }
}

static int run(apta_context_t *context,
               apta_feature_mask_t features,
               apta_session_t **session_out,
               const float *audio)
{
    apta_session_config_t config;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    uint32_t accepted = 0u;
    apta_status_t status;
    uint32_t guard;

    apta_session_config_init(&config);
    config.source_sample_rate = RATE;
    config.channel_count = 1u;
    config.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
    config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config.total_frames = TOTAL_FRAMES;
    config.requested_features = features;
    if (apta_session_create(context, &config, session_out) != APTA_STATUS_OK) {
        return 1;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 2048u;
    budget.maximum_steps = 64u;

    /* Pushed in chunks: a single push is bounded by the library's maximum. */
    {
        uint32_t pushed = 0u;

        while (pushed < TOTAL_FRAMES) {
            uint32_t count = TOTAL_FRAMES - pushed;

            if (count > 2048u) {
                count = 2048u;
            }
            apta_pcm_block_init(&block);
            block.data = &audio[pushed];
            block.first_frame = pushed;
            block.frame_count = count;
            accepted = 0u;
            if (apta_session_push_pcm(*session_out, &block, &accepted) !=
                    APTA_STATUS_OK ||
                accepted != count) {
                return 1;
            }
            pushed += count;
            if (apta_session_process(*session_out, &budget, NULL) < 0) {
                return 1;
            }
        }
    }
    if (apta_session_signal_end_of_input(*session_out, TOTAL_FRAMES) !=
        APTA_STATUS_OK) {
        return 1;
    }
    for (guard = 0u; guard < 1000u; ++guard) {
        status = apta_session_process(*session_out, &budget, NULL);
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
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_waveform_overview_view_t overview;
    apta_serialize_options_t serialize_options;
    apta_parse_options_t parse_options;
    float *audio;
    uint8_t *serialized;
    size_t written = 0u;
    size_t capacity;
    uint32_t index;
    uint32_t tone;
    apta_waveform_column_t sampled[3];

    audio = (float *)malloc((size_t)TOTAL_FRAMES * sizeof(*audio));
    CHECK(audio != NULL);
    render(audio);

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_3BAND;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    /* 1. Bands alone are not a valid request: they qualify the overview. */
    {
        apta_session_config_t bad;
        apta_session_t *rejected = NULL;

        apta_session_config_init(&bad);
        bad.source_sample_rate = RATE;
        bad.channel_count = 1u;
        bad.sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
        bad.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
        bad.total_frames = TOTAL_FRAMES;
        bad.requested_features = APTA_FEATURE_WAVEFORM_3BAND;
        CHECK(apta_session_create(context, &bad, &rejected) ==
              APTA_ERROR_INVALID_ARGUMENT);
        CHECK(rejected == NULL);
    }

    /* 2. Overview without bands must leave the bytes and the flag clear. */
    CHECK(run(context, APTA_FEATURE_WAVEFORM_OVERVIEW, &session, audio) == 0);
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.span_count == 1u);
    for (index = 0u; index < overview.spans[0].column_count; ++index) {
        const apta_waveform_column_t *c = &overview.spans[0].columns[index];
        CHECK((c->flags & APTA_WAVEFORM_COLUMN_HAS_3BAND) == 0u);
        CHECK(c->low == 0u && c->mid == 0u && c->high == 0u);
    }
    apta_result_release(result);
    result = NULL;
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    /* 3. With bands requested the session is accepted and every column
     *    carries all three values and the flag. */
    CHECK(run(context,
              APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_3BAND,
              &session,
              audio) == 0);
    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_waveform_overview_view_init(&overview);
    CHECK(apta_result_get_waveform_overview(result, 0u, &overview) ==
          APTA_STATUS_OK);
    CHECK(overview.span_count == 1u);
    CHECK(overview.spans[0].column_count >= 9u);
    for (index = 0u; index < overview.spans[0].column_count; ++index) {
        CHECK((overview.spans[0].columns[index].flags &
               APTA_WAVEFORM_COLUMN_HAS_3BAND) != 0u);
    }

    /* 4. Content: take the last column of each tone, by which point the
     *    one-pole states have settled, and require the matching band to
     *    dominate the other two. */
    for (tone = 0u; tone < 3u; ++tone) {
        const uint32_t column = (tone + 1u) * COLUMNS_PER_TONE - 1u;
        CHECK(column < overview.spans[0].column_count);
        sampled[tone] = overview.spans[0].columns[column];
    }

    /* 60 Hz -> low */
    CHECK(sampled[0].low > sampled[0].mid);
    CHECK(sampled[0].low > sampled[0].high);
    /* 800 Hz -> mid */
    CHECK(sampled[1].mid > sampled[1].low);
    CHECK(sampled[1].mid > sampled[1].high);
    /* 6 kHz -> high */
    CHECK(sampled[2].high > sampled[2].low);
    CHECK(sampled[2].high > sampled[2].mid);

    /* Each band must also peak on its own tone rather than merely winning
     * locally, which is what distinguishes a filterbank from three copies of
     * the same envelope. */
    CHECK(sampled[0].low > sampled[1].low);
    CHECK(sampled[0].low > sampled[2].low);
    CHECK(sampled[1].mid > sampled[0].mid);
    CHECK(sampled[1].mid > sampled[2].mid);
    CHECK(sampled[2].high > sampled[0].high);
    CHECK(sampled[2].high > sampled[1].high);

    /* 5. Round trip: the values survive serialization and parsing. */
    apta_serialize_options_init(&serialize_options);
    capacity = 64u * 1024u;
    serialized = (uint8_t *)malloc(capacity);
    CHECK(serialized != NULL);
    CHECK(apta_result_serialize(result, &serialize_options, serialized,
                                capacity, &written) == APTA_STATUS_OK);
    CHECK(written > 0u && written <= capacity);

    apta_parse_options_init(&parse_options);
    CHECK(apta_result_parse(context, &parse_options, serialized, written,
                            &parsed) == APTA_STATUS_OK);
    CHECK(parsed != NULL);

    {
        apta_waveform_overview_view_t restored;

        apta_waveform_overview_view_init(&restored);
        CHECK(apta_result_get_waveform_overview(parsed, 0u, &restored) ==
              APTA_STATUS_OK);
        CHECK(restored.span_count == overview.span_count);
        CHECK(restored.spans[0].column_count ==
              overview.spans[0].column_count);
        for (index = 0u; index < restored.spans[0].column_count; ++index) {
            const apta_waveform_column_t *a = &overview.spans[0].columns[index];
            const apta_waveform_column_t *b = &restored.spans[0].columns[index];
            CHECK(b->low == a->low);
            CHECK(b->mid == a->mid);
            CHECK(b->high == a->high);
            CHECK((b->flags & APTA_WAVEFORM_COLUMN_HAS_3BAND) != 0u);
        }
    }

    apta_result_release(parsed);
    apta_result_release(result);
    free(serialized);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    free(audio);
    return 0;
}
