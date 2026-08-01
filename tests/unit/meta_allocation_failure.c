// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <apta/apta.h>

#include "apta_test_geometry.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
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

static int build_valid_file(uint8_t output[1024 * APTA_TEST_WORKSPACE_SCALE], size_t *size_out)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_metadata_t metadata;
    apta_pcm_block_t block;
    apta_work_budget_t budget;
    const apta_result_t *result = NULL;
    int16_t pcm[1024] = {0};
    uint64_t required = 0u;
    size_t written = 0u;
    uint32_t accepted = 0u;
    int success = 0;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_session_create(context, &session_config, &session) !=
        APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_metadata_init(&metadata);
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    metadata.producer_name.data = "A";
    metadata.producer_name.size = 1u;
    if (apta_session_set_metadata(session, &metadata) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_pcm_block_init(&block);
    block.data = pcm;
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
        apta_result_query_serialized_size(result, NULL, &required) !=
            APTA_STATUS_OK ||
        required > 1024u ||
        apta_result_serialize(
            result,
            NULL,
            output,
            1024u,
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

static int run_setter_case(uint32_t fail_at_call)
{
    allocator_state_t state = {0u, fail_at_call, 0u};
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_metadata_t metadata;
    const apta_result_t *result = NULL;
    apta_metadata_view_t view;
    apta_status_t status;
    int success = 0;

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &state;
    context_config.allocator.allocate = test_allocate;
    context_config.allocator.deallocate = test_deallocate;
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_session_create(context, &session_config, &session) !=
        APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_metadata_init(&metadata);
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    metadata.producer_name.data = "A";
    metadata.producer_name.size = 1u;
    status = apta_session_set_metadata(session, &metadata);
    if (status != APTA_ERROR_OUT_OF_MEMORY) {
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        goto cleanup;
    }
    apta_metadata_view_init(&view);
    if (apta_result_get_metadata(result, &view) !=
        APTA_STATUS_NOT_AVAILABLE) {
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
    return success && state.outstanding == 0u;
}

static int run_parse_case(
    const uint8_t *file,
    size_t file_size,
    uint32_t fail_at_call)
{
    allocator_state_t state = {0u, fail_at_call, 0u};
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_status_t status;
    int success = 0;

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &state;
    context_config.allocator.allocate = test_allocate;
    context_config.allocator.deallocate = test_deallocate;
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        goto cleanup;
    }

    status = apta_result_parse(
        context,
        NULL,
        file,
        file_size,
        &result);
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
    uint8_t file[1024 * APTA_TEST_WORKSPACE_SCALE];
    size_t file_size = 0u;
    uint32_t fail_at;

    CHECK(build_valid_file(file, &file_size));

    /* Context=1, session=2, initial result=3, setter allocations=4..6. */
    for (fail_at = 4u; fail_at <= 6u; ++fail_at) {
        CHECK(run_setter_case(fail_at));
    }

    /* Context=1, WOVR result/spans/columns=2..4, hardening=5, META=6. */
    for (fail_at = 2u; fail_at <= 6u; ++fail_at) {
        CHECK(run_parse_case(file, file_size, fail_at));
    }

    return 0;
}
