// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <apta/apta.h>
#include <apta/desktop/apta_decoder.h>

#include "desktop_wav_fixture.h"

#define SAMPLE_RATE 48000u
#define BEAT_FRAMES 23040u
#define TOTAL_FRAMES 384000u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static double click_sample(
    uint64_t frame,
    uint16_t channel,
    void *user_data)
{
    (void)channel;
    (void)user_data;
    return (frame % BEAT_FRAMES) < 128u ? 0.9 : 0.0;
}

int main(void)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    char path[APTA_TEST_TEMP_PATH_CAPACITY];
    apta_decoder_t decoder;
    apta_decoder_info_t decoder_info;
    apta_pcm_source_t source;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    const apta_result_t *parsed = NULL;
    apta_tempo_view_t tempo;
    apta_grid_view_t grid;
    apta_work_budget_t budget;
    apta_status_t status = APTA_STATUS_MORE_WORK;
    uint32_t iteration;
    uint64_t serialized_size = 0u;
    size_t written = 0u;
    uint8_t *serialized = NULL;

    CHECK(apta_test_make_temp_path(path, sizeof(path)));
    CHECK(apta_test_write_wav(
        path,
        1u,
        16u,
        1u,
        SAMPLE_RATE,
        TOTAL_FRAMES,
        0,
        click_sample,
        NULL));

    apta_decoder_init(&decoder);
    apta_decoder_info_init(&decoder_info);
    CHECK(apta_wav_decoder_open_path(path, &decoder, &decoder_info) ==
          APTA_STATUS_OK);
    apta_pcm_source_init(&source);
    CHECK(apta_decoder_make_pcm_source(&decoder, &source) == APTA_STATUS_OK);

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = decoder_info.sample_rate;
    session_config.channel_count = decoder_info.channel_count;
    session_config.sample_format = decoder_info.sample_format;
    session_config.channel_layout = decoder_info.channel_layout;
    session_config.total_frames = decoder_info.total_frames;
    session_config.requested_features = features;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(apta_session_set_source(session, &source) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 4096u;
    budget.maximum_steps = 32u;
    for (iteration = 0u; iteration < 256u; ++iteration) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK ||
              status == APTA_STATUS_WOULD_BLOCK);
    }
    CHECK(status == APTA_STATUS_END_OF_INPUT);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(result, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.state == APTA_FEATURE_FINAL);
    CHECK(tempo.selected.tempo_millibpm >= 124500u);
    CHECK(tempo.selected.tempo_millibpm <= 125500u);

    apta_grid_view_init(&grid);
    CHECK(apta_result_get_beatgrid(
              result,
              APTA_FEATURE_LOCAL_BEATGRID,
              NULL,
              &grid) == APTA_STATUS_OK);
    CHECK(grid.state == APTA_FEATURE_FINAL);
    CHECK(grid.segment_count == 1u);

    CHECK(apta_result_query_serialized_size(
              result,
              NULL,
              &serialized_size) == APTA_STATUS_OK);
    CHECK(serialized_size > 0u && serialized_size <= SIZE_MAX);
    serialized = (uint8_t *)malloc((size_t)serialized_size);
    CHECK(serialized != NULL);
    CHECK(apta_result_serialize(
              result,
              NULL,
              serialized,
              (size_t)serialized_size,
              &written) == APTA_STATUS_OK);
    CHECK(written == (size_t)serialized_size);
    CHECK(apta_result_parse(
              context,
              NULL,
              serialized,
              written,
              &parsed) == APTA_STATUS_OK);
    apta_tempo_view_init(&tempo);
    CHECK(apta_result_get_tempo(parsed, NULL, &tempo) == APTA_STATUS_OK);
    CHECK(tempo.selected.tempo_millibpm >= 124500u);
    CHECK(tempo.selected.tempo_millibpm <= 125500u);

    apta_result_release(parsed);
    parsed = NULL;
    free(serialized);
    serialized = NULL;
    apta_result_release(result);
    result = NULL;
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    apta_decoder_close(&decoder);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(apta_test_remove_path(path));
    return 0;
}
