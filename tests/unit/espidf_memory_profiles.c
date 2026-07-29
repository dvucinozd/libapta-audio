// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "test_alignment.h"

#define SAMPLE_RATE 48000u
#define BLOCK_FRAMES 4096u
#define MAX_WORKSPACE_BYTES 1572864u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 0;                                                        \
        }                                                                    \
    } while (0)

typedef union {
    apta_test_max_align_t alignment;
    uint8_t bytes[MAX_WORKSPACE_BYTES];
} aligned_workspace_t;

typedef struct {
    uint32_t allocate_calls;
    uint32_t outstanding;
} allocator_state_t;

typedef struct {
    const char *name;
    apta_feature_mask_t features;
    uint32_t total_frames;
    uint32_t beat_frames;
    size_t workspace_bytes;
    apta_feature_mask_t final_feature;
} profile_t;

static aligned_workspace_t workspace;
static int16_t samples[BLOCK_FRAMES];

static void *APTA_CALL profile_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    void *memory;
    (void)alignment;
    (void)flags;
    state->allocate_calls += 1u;
    memory = malloc(size);
    if (memory != NULL) {
        state->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL profile_deallocate(void *user_data, void *memory)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

static int run_profile(const profile_t *profile)
{
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    allocator_state_t allocator = {0u, 0u};
    uint32_t first = 0u;
    uint32_t calls_after_create;
    apta_status_t status;
    apta_feature_state_t state = APTA_FEATURE_ABSENT;
    apta_confidence_value_t confidence = APTA_CONFIDENCE_UNKNOWN;
    int success = 0;

    CHECK(profile != NULL);
    CHECK(profile->workspace_bytes <= sizeof(workspace.bytes));
    memset(&workspace, 0, profile->workspace_bytes);

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PUSH;
    session_config.source_sample_rate = SAMPLE_RATE;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = profile->total_frames;
    session_config.requested_features = profile->features;
    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = profile->workspace_bytes;
    session_config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(&session_config, &requirements) ==
          APTA_STATUS_OK);
    CHECK((requirements.flags &
           APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL) != 0u);

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &allocator;
    context_config.allocator.allocate = profile_allocate;
    context_config.allocator.deallocate = profile_deallocate;
    context_config.requested_capabilities = profile->features;
    context_config.memory_limit_bytes = requirements.minimum_bytes;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(session == (apta_session_t *)(void *)workspace.bytes);
    CHECK(allocator.allocate_calls == 2u);
    calls_after_create = allocator.allocate_calls;

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 64u;

    while (first < profile->total_frames) {
        apta_pcm_block_t block;
        uint32_t count = profile->total_frames - first;
        uint32_t accepted = 0u;
        uint32_t index;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            samples[index] =
                ((first + index) % profile->beat_frames) < 192u
                    ? (int16_t)30000
                    : 0;
        }
        apta_pcm_block_init(&block);
        block.data = samples;
        block.first_frame = first;
        block.frame_count = count;
        CHECK(apta_session_push_pcm(session, &block, &accepted) ==
              APTA_STATUS_OK);
        CHECK(accepted == count);
        status = apta_session_process(session, &budget, NULL);
        CHECK(status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
        CHECK(allocator.allocate_calls == calls_after_create);
        first += count;
    }

    CHECK(apta_session_signal_end_of_input(session, profile->total_frames) ==
          APTA_STATUS_OK);
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    CHECK(status == APTA_STATUS_END_OF_INPUT);
    CHECK(allocator.allocate_calls == calls_after_create);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);
    CHECK(apta_result_get_feature_state(
              result,
              profile->final_feature,
              NULL,
              &state,
              &confidence) == APTA_STATUS_OK);
    CHECK(state == APTA_FEATURE_FINAL);

    printf("APTA_ESP_PROFILE name=%s workspace=%zu result_pool=%zu alignment=%zu "
           "total_frames=%u allocator_calls=%u\n",
           profile->name,
           profile->workspace_bytes,
           requirements.minimum_bytes,
           requirements.required_alignment,
           profile->total_frames,
           allocator.allocate_calls);

    success = 1;
    apta_result_release(result);
    result = NULL;
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(allocator.outstanding == 0u);
    return success;
}

int main(void)
{
    static const profile_t profiles[] = {
        {
            "WAVEFORM_8S",
            APTA_FEATURE_WAVEFORM_OVERVIEW,
            SAMPLE_RATE * 8u,
            23040u,
            131072u,
            APTA_FEATURE_WAVEFORM_OVERVIEW
        },
        {
            "PERFORMANCE_LOCAL_6S",
            APTA_FEATURE_WAVEFORM_OVERVIEW |
                APTA_FEATURE_BPM |
                APTA_FEATURE_LOCAL_BEATGRID |
                APTA_FEATURE_CONFIDENCE,
            288000u,
            23040u,
            262144u,
            APTA_FEATURE_LOCAL_BEATGRID
        },
        {
            "GLOBAL_DYNAMIC_10_9S",
            APTA_FEATURE_WAVEFORM_OVERVIEW |
                APTA_FEATURE_BPM |
                APTA_FEATURE_GLOBAL_BEATGRID |
                APTA_FEATURE_DYNAMIC_TEMPO |
                APTA_FEATURE_CONFIDENCE,
            524288u,
            24576u,
            1572864u,
            APTA_FEATURE_GLOBAL_BEATGRID
        }
    };
    size_t index;

    for (index = 0u; index < sizeof(profiles) / sizeof(profiles[0]); ++index) {
        if (!run_profile(&profiles[index])) {
            return 1;
        }
    }
    return 0;
}
