// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "test_alignment.h"

#include "apta_test_geometry.h"

#define COL APTA_TEST_COLUMN_FRAMES

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef union {
    apta_test_max_align_t alignment;
    uint8_t bytes[64 * APTA_TEST_WORKSPACE_SCALE];
} aligned_small_workspace_t;

static void configure_waveform_session(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = 48000u;
    config->channel_count = 1u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config->total_frames = 4096u;
    config->requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
}

int main(void)
{
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_memory_requirements_t bpm_requirements;
    apta_memory_requirements_t quality_requirements;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    int16_t pcm[4096];
    aligned_small_workspace_t workspace;
    uint32_t accepted;
    apta_status_t status;
    uint32_t guard;

    memset(pcm, 0, sizeof(pcm));
    memset(&workspace, 0, sizeof(workspace));
    configure_waveform_session(&session_config);
    apta_memory_requirements_init(&requirements);

    CHECK(apta_query_memory_requirements(&session_config, &requirements) ==
          APTA_STATUS_OK);
    CHECK(requirements.minimum_bytes > 0u);
    CHECK(requirements.recommended_bytes >= requirements.minimum_bytes);
    CHECK(requirements.required_alignment != 0u);
    CHECK((requirements.required_alignment &
           (requirements.required_alignment - 1u)) == 0u);

    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_BPM;
    apta_memory_requirements_init(&bpm_requirements);
    CHECK(apta_query_memory_requirements(
              &session_config, &bpm_requirements) == APTA_STATUS_OK);
    session_config.requested_features |= APTA_FEATURE_CALIBRATED_QUALITY;
    apta_memory_requirements_init(&quality_requirements);
    CHECK(apta_query_memory_requirements(
              &session_config, &quality_requirements) == APTA_STATUS_OK);
    CHECK(quality_requirements.recommended_bytes >
          bpm_requirements.recommended_bytes);

    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_CALIBRATED_QUALITY;
    apta_memory_requirements_init(&quality_requirements);
    CHECK(apta_query_memory_requirements(
              &session_config, &quality_requirements) ==
          APTA_ERROR_INVALID_ARGUMENT);
    apta_memory_requirements_init(&quality_requirements);
    CHECK(apta_query_workspace_requirements(
              &session_config, &quality_requirements) ==
          APTA_ERROR_INVALID_ARGUMENT);
    configure_waveform_session(&session_config);

    apta_context_config_init(&context_config);
    context_config.memory_limit_bytes = requirements.minimum_bytes - 1u;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;

    apta_context_config_init(&context_config);
    context_config.memory_limit_bytes = requirements.minimum_bytes;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;

    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_OUT_OF_MEMORY);
    CHECK(session == NULL);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    session_config.static_workspace = NULL;
    session_config.static_workspace_size = 0u;

    apta_context_config_init(&context_config);
    context_config.memory_limit_bytes = requirements.recommended_bytes;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 4096u;
    accepted = 0u;
    status = apta_session_push_pcm(session, &block, &accepted);
    CHECK(status == APTA_STATUS_OK);
    CHECK(accepted == 4096u);
    CHECK(apta_session_signal_end_of_input(session, 4096u) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 4096u;
    budget.maximum_steps = 16u;

    for (guard = 0u; guard < 8u; ++guard) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    }

    CHECK(status == APTA_STATUS_END_OF_INPUT);
    CHECK(apta_session_get_state(session) == APTA_SESSION_COMPLETED);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
