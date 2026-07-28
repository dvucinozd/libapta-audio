// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#define FILE_SIZE 376u

typedef struct {
    uint32_t allocation_call;
    uint32_t fail_at_call;
    uint32_t outstanding;
} parser_allocator_state_t;

static void *APTA_CALL parser_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    parser_allocator_state_t *state =
        (parser_allocator_state_t *)user_data;
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

static void APTA_CALL parser_deallocate(void *user_data, void *memory)
{
    parser_allocator_state_t *state =
        (parser_allocator_state_t *)user_data;

    if (memory != NULL) {
        free(memory);
        state->outstanding -= 1u;
    }
}

static int build_valid_file(uint8_t output[FILE_SIZE])
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    int16_t pcm[1024] = {0};
    uint64_t size = 0u;
    size_t written = 0u;
    uint32_t accepted = 0u;
    int success = 0;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;
    if (apta_session_create(context, &session_config, &session) !=
        APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.first_frame = 0u;
    block.frame_count = 1024u;
    if (apta_session_push_pcm(session, &block, &accepted) != APTA_STATUS_OK ||
        accepted != 1024u ||
        apta_session_signal_end_of_input(session, 1024u) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 1024u;
    budget.maximum_steps = 4u;
    if (apta_session_process(session, &budget, NULL) !=
        APTA_STATUS_END_OF_INPUT) {
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL ||
        apta_result_query_serialized_size(result, NULL, &size) !=
            APTA_STATUS_OK ||
        size != FILE_SIZE ||
        apta_result_serialize(
            result,
            NULL,
            output,
            FILE_SIZE,
            &written) != APTA_STATUS_OK ||
        written != FILE_SIZE) {
        goto cleanup;
    }

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

static int run_case(
    const uint8_t file[FILE_SIZE],
    uint32_t fail_at_call,
    apta_status_t expected_status)
{
    parser_allocator_state_t state = {0u, fail_at_call, 0u};
    apta_context_config_t context_config;
    apta_parse_options_t parse_options;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_status_t status;

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &state;
    context_config.allocator.allocate = parser_allocate;
    context_config.allocator.deallocate = parser_deallocate;
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL;

    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        return 0;
    }

    apta_parse_options_init(&parse_options);
    status = apta_result_parse(
        context,
        &parse_options,
        file,
        FILE_SIZE,
        &result);

    if (status != expected_status) {
        if (result != NULL) {
            apta_result_release(result);
        }
        (void)apta_context_destroy(context);
        return 0;
    }

    if (expected_status == APTA_STATUS_OK) {
        if (result == NULL ||
            (apta_result_get_available_features(result) &
             APTA_FEATURE_WAVEFORM_DETAIL) == 0u) {
            if (result != NULL) {
                apta_result_release(result);
            }
            (void)apta_context_destroy(context);
            return 0;
        }
        apta_result_release(result);
    } else if (result != NULL) {
        apta_result_release(result);
        (void)apta_context_destroy(context);
        return 0;
    }

    if (apta_context_destroy(context) != APTA_STATUS_OK) {
        return 0;
    }
    return state.outstanding == 0u;
}

int main(void)
{
    uint8_t file[FILE_SIZE];
    uint32_t fail_at;

    CHECK(build_valid_file(file));
    CHECK(run_case(file, 0u, APTA_STATUS_OK));

    /* Call 1 creates the context; calls 2..8 are parser-owned allocations. */
    for (fail_at = 2u; fail_at <= 8u; ++fail_at) {
        CHECK(run_case(file, fail_at, APTA_ERROR_OUT_OF_MEMORY));
    }

    return 0;
}
