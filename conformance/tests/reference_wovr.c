// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

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
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *source_result = NULL;
    const apta_result_t *parsed_result = NULL;
    apta_result_info_t info;
    apta_waveform_overview_view_t source_view;
    apta_waveform_overview_view_t parsed_view;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    apta_serialize_options_t serialize_options;
    apta_parse_options_t parse_options;
    int16_t pcm[2048];
    uint8_t serialized[236];
    uint8_t reserialized[236];
    size_t written = 0u;
    size_t rewritten = 0u;
    uint32_t accepted = 0u;
    uint32_t index;

    for (index = 0u; index < 2048u; ++index) {
        pcm[index] = (int16_t)((int32_t)(index % 257u) - 128);
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 2048u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 2048u;
    CHECK(apta_session_push_pcm(session, &block, &accepted) == APTA_STATUS_OK);
    CHECK(accepted == 2048u);
    CHECK(apta_session_signal_end_of_input(session, 2048u) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 2048u;
    budget.maximum_steps = 8u;
    CHECK(apta_session_process(session, &budget, NULL) == APTA_STATUS_END_OF_INPUT);

    source_result = apta_session_acquire_result(session);
    CHECK(source_result != NULL);

    apta_serialize_options_init(&serialize_options);
    CHECK(apta_result_serialize(
              source_result,
              &serialize_options,
              serialized,
              sizeof(serialized),
              &written) == APTA_STATUS_OK);
    CHECK(written == sizeof(serialized));

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    apta_result_release(source_result);
    source_result = NULL;

    apta_parse_options_init(&parse_options);
    parse_options.maximum_file_bytes = sizeof(serialized) - 1u;
    CHECK(apta_result_parse(
              context,
              &parse_options,
              serialized,
              sizeof(serialized),
              &parsed_result) == APTA_ERROR_LIMIT_EXCEEDED);
    CHECK(parsed_result == NULL);

    apta_parse_options_init(&parse_options);
    CHECK(apta_result_parse(
              context,
              &parse_options,
              serialized,
              sizeof(serialized),
              &parsed_result) == APTA_STATUS_OK);
    CHECK(parsed_result != NULL);

    apta_result_info_init(&info);
    CHECK(apta_result_get_info(parsed_result, &info) == APTA_STATUS_OK);
    CHECK(info.container_version == 1u);
    CHECK(info.specification_major == APTA_SPEC_VERSION_MAJOR);
    CHECK(info.specification_minor == APTA_SPEC_VERSION_MINOR);
    CHECK(info.available_features == APTA_FEATURE_WAVEFORM_OVERVIEW);
    CHECK(info.session_state == APTA_SESSION_COMPLETED);

    apta_waveform_overview_view_init(&parsed_view);
    CHECK(apta_result_get_waveform_overview(parsed_result, 0u, &parsed_view) ==
          APTA_STATUS_OK);
    CHECK(parsed_view.state == APTA_FEATURE_FINAL);
    CHECK(parsed_view.level.frames_per_column == 1024u);
    CHECK(parsed_view.span_count == 1u);
    CHECK(parsed_view.spans[0].source_range.first_frame == 0u);
    CHECK(parsed_view.spans[0].source_range.end_frame == 2048u);
    CHECK(parsed_view.spans[0].column_count == 2u);

    CHECK(apta_result_serialize(
              parsed_result,
              &serialize_options,
              reserialized,
              sizeof(reserialized),
              &rewritten) == APTA_STATUS_OK);
    CHECK(rewritten == written);
    CHECK(memcmp(serialized, reserialized, written) == 0);

    apta_waveform_overview_view_init(&source_view);
    source_view = parsed_view;
    CHECK(source_view.spans[0].columns[0].minimum <=
          source_view.spans[0].columns[0].maximum);
    CHECK(source_view.spans[0].columns[1].minimum <=
          source_view.spans[0].columns[1].maximum);

    apta_result_release(parsed_result);
    parsed_result = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    return 0;
}
