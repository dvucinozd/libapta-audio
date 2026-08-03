// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define TOTAL_FRAMES 1024u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int drain(apta_session_t *session)
{
    apta_work_budget_t budget;
    uint32_t guard;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = TOTAL_FRAMES;
    budget.maximum_steps = 16u;
    for (guard = 0u; guard < 64u; ++guard) {
        apta_status_t status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            return 0;
        }
        if (status < 0 || status == APTA_STATUS_WOULD_BLOCK) {
            return 1;
        }
    }
    return 1;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    const apta_result_t *source = NULL;
    const apta_result_t *parsed = NULL;
    apta_waveform_overview_view_t source_view;
    apta_waveform_overview_view_t parsed_view;
    apta_result_info_t info;
    uint8_t *encoded = NULL;
    uint8_t *rewritten = NULL;
    uint64_t required = 0u;
    size_t written = 0u;
    size_t rewritten_size = 0u;
    int16_t pcm[TOTAL_FRAMES];
    uint32_t accepted = 0u;
    uint32_t index;

    for (index = 0u; index < TOTAL_FRAMES; ++index) {
        pcm[index] = (int16_t)((int32_t)(index % 257u) - 128);
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 44100u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    session_config.overview_frames_per_column = 256u;
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = TOTAL_FRAMES;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == TOTAL_FRAMES);
    CHECK(apta_session_signal_end_of_input(session, TOTAL_FRAMES) ==
          APTA_STATUS_OK);
    CHECK(drain(session) == 0);

    source = apta_session_acquire_result(session);
    CHECK(source != NULL);
    CHECK(apta_result_query_serialized_size(source, NULL, &required) ==
          APTA_STATUS_OK);
    CHECK(required > 0u && required <= SIZE_MAX);

    encoded = (uint8_t *)malloc((size_t)required);
    rewritten = (uint8_t *)malloc((size_t)required);
    CHECK(encoded != NULL && rewritten != NULL);
    CHECK(apta_result_serialize(
              source, NULL, encoded, (size_t)required, &written) ==
          APTA_STATUS_OK);
    CHECK(written == (size_t)required);

    CHECK(apta_result_parse(context, NULL, encoded, written, &parsed) ==
          APTA_STATUS_OK);
    CHECK(parsed != NULL);

    apta_result_info_init(&info);
    CHECK(apta_result_get_info(parsed, &info) == APTA_STATUS_OK);
    CHECK(info.container_version == APTA_CONTAINER_VERSION);
    CHECK(info.specification_major == APTA_SPEC_VERSION_MAJOR);
    CHECK(info.specification_minor == APTA_SPEC_VERSION_MINOR);
    CHECK(info.available_features == APTA_FEATURE_WAVEFORM_OVERVIEW);
    CHECK(info.session_state == APTA_SESSION_COMPLETED);

    apta_waveform_overview_view_init(&source_view);
    apta_waveform_overview_view_init(&parsed_view);
    CHECK(apta_result_get_waveform_overview(source, 0u, &source_view) ==
          APTA_STATUS_OK);
    CHECK(apta_result_get_waveform_overview(parsed, 0u, &parsed_view) ==
          APTA_STATUS_OK);
    CHECK(parsed_view.level.frames_per_column ==
          source_view.level.frames_per_column);
    CHECK(parsed_view.state == source_view.state);
    CHECK(parsed_view.span_count == source_view.span_count);
    CHECK(parsed_view.span_count == 1u);
    CHECK(parsed_view.spans[0].column_count ==
          source_view.spans[0].column_count);
    CHECK(memcmp(
              parsed_view.spans[0].columns,
              source_view.spans[0].columns,
              source_view.spans[0].column_count *
                  sizeof(apta_waveform_column_t)) == 0);

    CHECK(apta_result_serialize(
              parsed,
              NULL,
              rewritten,
              (size_t)required,
              &rewritten_size) == APTA_STATUS_OK);
    CHECK(rewritten_size == written);
    CHECK(memcmp(encoded, rewritten, written) == 0);

    apta_result_release(parsed);
    apta_result_release(source);
    free(rewritten);
    free(encoded);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
