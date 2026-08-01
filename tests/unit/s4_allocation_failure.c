// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define TOTAL_FRAMES 144000u
#define BLOCK_FRAMES 4096u
#define BEAT_FRAMES 23040u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    uint32_t allocation_call;
    uint32_t fail_at_call;
    uint32_t outstanding;
} allocator_state_t;

static void *APTA_CALL test_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    void *memory;
    (void)alignment;
    (void)flags;
    state->allocation_call += 1u;
    if (state->fail_at_call != 0u &&
        state->allocation_call == state->fail_at_call) {
        return NULL;
    }
    memory = malloc(size);
    if (memory != NULL) {
        state->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL test_deallocate(void *user_data, void *memory)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

static int build_valid_file(uint8_t output[4096 * APTA_TEST_WORKSPACE_SCALE], size_t *size_out)
{
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    int16_t samples[BLOCK_FRAMES];
    uint32_t first = 0u;
    uint64_t required = 0u;
    size_t written = 0u;
    apta_status_t status;
    int success = 0;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = TOTAL_FRAMES;
    session_config.requested_features = features;
    if (apta_session_create(context, &session_config, &session) !=
        APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = BLOCK_FRAMES;
    budget.maximum_steps = 32u;
    while (first < TOTAL_FRAMES) {
        apta_pcm_block_t block;
        uint32_t count = TOTAL_FRAMES - first;
        uint32_t accepted = 0u;
        uint32_t index;
        if (count > BLOCK_FRAMES) {
            count = BLOCK_FRAMES;
        }
        for (index = 0u; index < count; ++index) {
            samples[index] = ((first + index) % BEAT_FRAMES) < 128u
                                 ? (int16_t)30000
                                 : 0;
        }
        apta_pcm_block_init(&block);
        block.data = samples;
        block.first_frame = first;
        block.frame_count = count;
        if (apta_session_push_pcm(session, &block, &accepted) !=
                APTA_STATUS_OK ||
            accepted != count ||
            apta_session_process(session, &budget, NULL) < 0) {
            goto cleanup;
        }
        first += count;
    }
    if (apta_session_signal_end_of_input(session, TOTAL_FRAMES) !=
        APTA_STATUS_OK) {
        goto cleanup;
    }
    do {
        status = apta_session_process(session, &budget, NULL);
    } while (status == APTA_STATUS_OK || status == APTA_STATUS_MORE_WORK);
    if (status != APTA_STATUS_END_OF_INPUT) {
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL ||
        apta_result_query_serialized_size(result, NULL, &required) !=
            APTA_STATUS_OK ||
        required > 4096u ||
        apta_result_serialize(
            result,
            NULL,
            output,
            4096u,
            &written) != APTA_STATUS_OK ||
        written != (size_t)required) {
        goto cleanup;
    }

    *size_out = written;
    success = 1;

cleanup:
    if (result != NULL) {
        apta_result_release(result);
    }
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    if (context != NULL && apta_context_destroy(context) != APTA_STATUS_OK) {
        success = 0;
    }
    return success;
}

static int run_parse_case(
    const uint8_t *file,
    size_t file_size,
    uint32_t fail_at_call)
{
    const apta_feature_mask_t capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    allocator_state_t state = {0u, fail_at_call, 0u};
    apta_context_config_t config;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_status_t status;
    int success = 0;

    apta_context_config_init(&config);
    config.allocator.user_data = &state;
    config.allocator.allocate = test_allocate;
    config.allocator.deallocate = test_deallocate;
    config.requested_capabilities = capabilities;
    if (apta_context_create(&config, &context) != APTA_STATUS_OK) {
        goto cleanup;
    }

    status = apta_result_parse(context, NULL, file, file_size, &result);
    if (status != APTA_ERROR_OUT_OF_MEMORY || result != NULL) {
        goto cleanup;
    }
    success = 1;

cleanup:
    if (result != NULL) {
        apta_result_release(result);
    }
    if (context != NULL && apta_context_destroy(context) != APTA_STATUS_OK) {
        success = 0;
    }
    return success && state.outstanding == 0u;
}

int main(void)
{
    uint8_t file[4096 * APTA_TEST_WORKSPACE_SCALE];
    size_t file_size = 0u;
    uint32_t fail_at;

    CHECK(build_valid_file(file, &file_size));
    /* Context=1, WOVR result/spans/columns=2..4, hardening=5,
       TEMP candidates=6, LGRD coverage/segment=7..8. */
    for (fail_at = 2u; fail_at <= 8u; ++fail_at) {
        CHECK(run_parse_case(file, file_size, fail_at));
    }
    return 0;
}
