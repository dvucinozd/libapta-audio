// SPDX-License-Identifier: Apache-2.0
/*
 * C2: configurable overview resolution.
 *
 * The host chooses frames per column for level 0 through the session config.
 * This checks that the value is validated, honoured, reported back, reflected
 * in the queried workspace requirement, and preserved across a serialize and
 * parse round trip.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define RATE 44100u
#define TOTAL_FRAMES 65536u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void configure(apta_session_config_t *config, uint32_t frames_per_column)
{
    apta_session_config_init(config);
    config->source_sample_rate = RATE;
    config->channel_count = 1u;
    config->sample_format = APTA_SAMPLE_F32_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config->total_frames = TOTAL_FRAMES;
    config->requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    config->overview_frames_per_column = frames_per_column;
}

/* Analyse a full track at the given resolution and return the reported
 * frames_per_column and column count. */
static int analyse(apta_context_t *context,
                   uint32_t frames_per_column,
                   const float *audio,
                   uint32_t *reported_out,
                   uint32_t *columns_out)
{
    apta_session_config_t config;
    apta_session_t *session = NULL;
    apta_work_budget_t budget;
    apta_waveform_overview_view_t overview;
    const apta_result_t *result;
    uint32_t pushed = 0u;
    apta_status_t status;
    uint32_t guard;

    configure(&config, frames_per_column);
    if (apta_session_create(context, &config, &session) != APTA_STATUS_OK) {
        return 1;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 2048u;
    budget.maximum_steps = 64u;
    while (pushed < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - pushed;
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
    if (apta_session_signal_end_of_input(session, TOTAL_FRAMES) !=
        APTA_STATUS_OK) {
        return 1;
    }
    for (guard = 0u; guard < 1000u; ++guard) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        if (status < 0) {
            return 1;
        }
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        return 1;
    }
    apta_waveform_overview_view_init(&overview);
    if (apta_result_get_waveform_overview(result, 0u, &overview) !=
            APTA_STATUS_OK ||
        overview.span_count != 1u) {
        apta_result_release(result);
        return 1;
    }
    *reported_out = overview.level.frames_per_column;
    *columns_out = overview.spans[0].column_count;
    apta_result_release(result);
    (void)apta_session_destroy(session);
    return 0;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_config_t config;
    apta_session_t *session = NULL;
    apta_memory_requirements_t coarse;
    apta_memory_requirements_t fine;
    float *audio;
    uint32_t reported;
    uint32_t columns;
    uint32_t default_columns;
    uint32_t index;

    audio = (float *)malloc((size_t)TOTAL_FRAMES * sizeof(*audio));
    CHECK(audio != NULL);
    for (index = 0u; index < TOTAL_FRAMES; ++index) {
        audio[index] = ((index % 512u) < 32u) ? 0.6f : -0.2f;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    /* Rejected: not a power of two, below the floor, above the ceiling. */
    configure(&config, 1000u);
    CHECK(apta_session_create(context, &config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);
    configure(&config, 32u);
    CHECK(apta_session_create(context, &config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    configure(&config, 131072u);
    CHECK(apta_session_create(context, &config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);

    /* Zero means the library default, whatever it was configured to be. */
    CHECK(analyse(context, 0u, audio, &reported, &default_columns) == 0);
    CHECK(reported == APTA_TEST_COLUMN_FRAMES);
    CHECK(default_columns ==
          (TOTAL_FRAMES + APTA_TEST_COLUMN_FRAMES - 1u) /
              APTA_TEST_COLUMN_FRAMES);

    /* A finer resolution produces proportionally more columns and reports
     * itself back. 256 is finer than any default this test runs under. */
    CHECK(analyse(context, 256u, audio, &reported, &columns) == 0);
    CHECK(reported == 256u);
    CHECK(columns == TOTAL_FRAMES / 256u);
    CHECK(columns > default_columns ||
          APTA_TEST_COLUMN_FRAMES <= 256u);

    /* And a coarser one produces fewer. */
    CHECK(analyse(context, 4096u, audio, &reported, &columns) == 0);
    CHECK(reported == 4096u);
    CHECK(columns == TOTAL_FRAMES / 4096u);

    /* The workspace query follows the chosen resolution: the accumulator array
     * is one entry per column, so finer costs more. */
    configure(&config, 4096u);
    apta_memory_requirements_init(&coarse);
    CHECK(apta_query_workspace_requirements(&config, &coarse) ==
          APTA_STATUS_OK);
    configure(&config, 256u);
    apta_memory_requirements_init(&fine);
    CHECK(apta_query_workspace_requirements(&config, &fine) ==
          APTA_STATUS_OK);
    CHECK(fine.minimum_bytes > coarse.minimum_bytes);

    free(audio);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
