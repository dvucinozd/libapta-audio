// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
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

typedef union {
    max_align_t alignment;
    uint8_t bytes[65536];
} aligned_workspace_t;

static void configure_session(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = 48000u;
    config->channel_count = 1u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config->total_frames = 2048u;
    config->requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
}

int main(void)
{
    apta_session_config_t config;
    apta_session_config_t invalid_config;
    apta_memory_requirements_t baseline;
    apta_memory_requirements_t bounded;
    apta_memory_requirements_t repeated;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_result_info_t info;
    aligned_workspace_t workspace;
    size_t required_bytes;

    CHECK(APTA_ERROR_RESULT_SLOTS_EXHAUSTED == (apta_status_t)-14);
    CHECK(APTA_ERROR_RESULT_SLOTS_EXHAUSTED < APTA_ERROR_BUFFER_TOO_SMALL);

    memset(&workspace, 0, sizeof(workspace));
    configure_session(&config);

    apta_memory_requirements_init(&baseline);
    CHECK(apta_query_memory_requirements(&config, &baseline) ==
          APTA_STATUS_OK);
    CHECK(baseline.flags == 0u);

    config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
    apta_memory_requirements_init(&bounded);
    CHECK(apta_query_memory_requirements(&config, &bounded) ==
          APTA_STATUS_OK);
    CHECK(bounded.minimum_bytes == bounded.recommended_bytes);
    CHECK(bounded.minimum_bytes > baseline.minimum_bytes);
    CHECK(bounded.required_alignment != 0u);
    CHECK((bounded.required_alignment &
           (bounded.required_alignment - 1u)) == 0u);
    CHECK(bounded.flags ==
          APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL);
    required_bytes = bounded.minimum_bytes;

    apta_memory_requirements_init(&repeated);
    CHECK(apta_query_memory_requirements(&config, &repeated) ==
          APTA_STATUS_OK);
    CHECK(repeated.minimum_bytes == bounded.minimum_bytes);
    CHECK(repeated.recommended_bytes == bounded.recommended_bytes);
    CHECK(repeated.required_alignment == bounded.required_alignment);
    CHECK(repeated.flags == bounded.flags);

    config.memory_budget_bytes = (uint64_t)required_bytes - 1u;
    apta_memory_requirements_init(&repeated);
    CHECK(apta_query_memory_requirements(&config, &repeated) ==
          APTA_ERROR_LIMIT_EXCEEDED);
    config.memory_budget_bytes = (uint64_t)required_bytes;
    apta_memory_requirements_init(&repeated);
    CHECK(apta_query_memory_requirements(&config, &repeated) ==
          APTA_STATUS_OK);

    config.total_frames = APTA_TOTAL_FRAMES_UNKNOWN;
    apta_memory_requirements_init(&repeated);
    CHECK(apta_query_memory_requirements(&config, &repeated) ==
          APTA_ERROR_INVALID_ARGUMENT);
    config.total_frames = 2048u;

    config.requested_features = 0u;
    apta_memory_requirements_init(&repeated);
    CHECK(apta_query_memory_requirements(&config, &repeated) ==
          APTA_ERROR_INVALID_ARGUMENT);
    config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;

    config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS | (1u << 31);
    apta_memory_requirements_init(&repeated);
    CHECK(apta_query_memory_requirements(&config, &repeated) ==
          APTA_ERROR_INVALID_ARGUMENT);
    config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    config.static_workspace = workspace.bytes;
    config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &config, &session) ==
          APTA_STATUS_OK);
    CHECK(session == (apta_session_t *)(void *)workspace.bytes);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    apta_result_info_init(&info);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.generation == 1u);
    CHECK(info.session_state == APTA_SESSION_CREATED);
    CHECK(info.available_features == 0u);
    apta_result_release(result);
    result = NULL;

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    invalid_config = config;
    invalid_config.api_version = APTA_API_VERSION + 1u;
    CHECK(apta_session_create(context, &invalid_config, &session) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(session == NULL);

    invalid_config = config;
    invalid_config.flags |= (1u << 31);
    CHECK(apta_session_create(context, &invalid_config, &session) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(session == NULL);

    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
