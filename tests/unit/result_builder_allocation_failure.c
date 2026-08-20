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

typedef struct {
    uint32_t allocation_call;
    uint32_t deallocation_call;
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
    if (state->fail_at_call == state->allocation_call) {
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
        state->deallocation_call += 1u;
        state->outstanding -= 1u;
    }
}

static void set_range(apta_frame_range_t *range, uint64_t first, uint64_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

static int run_case(
    uint32_t fail_at_call,
    uint32_t *allocation_count_out)
{
    allocator_state_t state = {0u, 0u, fail_at_call, 0u};
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_result_builder_options_t options;
    apta_result_builder_t *builder = NULL;
    apta_result_provenance_t provenance;
    apta_source_info_t source;
    apta_metadata_t metadata;
    apta_waveform_column_t column = {
        -1, 2, 1u, 0u, 0u, 0u, APTA_WAVEFORM_COLUMN_VALID};
    apta_waveform_span_t span;
    apta_waveform_overview_view_t overview;
    const apta_result_t *result = NULL;
    apta_status_t status;
    int saw_oom = 0;
    int success = 0;

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &state;
    context_config.allocator.allocate = test_allocate;
    context_config.allocator.deallocate = test_deallocate;
    status = apta_context_create(&context_config, &context);
    if (status == APTA_ERROR_OUT_OF_MEMORY) {
        saw_oom = 1;
        goto cleanup;
    }
    if (status != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_result_builder_options_init(&options);
    status = apta_result_builder_create(context, &options, &builder);
    if (status == APTA_ERROR_OUT_OF_MEMORY) {
        saw_oom = 1;
        goto cleanup;
    }
    if (status != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_result_provenance_init(&provenance);
    provenance.origin = APTA_RESULT_PROVENANCE_EXTERNAL_IMPORT;
    provenance.source_name.data = "allocator-fixture";
    provenance.source_name.size = 17u;
    status = apta_result_builder_set_provenance(builder, &provenance);
    if (status == APTA_ERROR_OUT_OF_MEMORY) {
        saw_oom = 1;
        goto cleanup;
    }
    if (status != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_source_info_init(&source);
    source.total_frames = 1024u;
    source.sample_rate = 48000u;
    source.channel_count = 1u;
    source.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    status = apta_result_builder_set_source_info(builder, &source);
    if (status != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_metadata_init(&metadata);
    metadata.producer_name.data = "R";
    metadata.producer_name.size = 1u;
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    status = apta_result_builder_set_metadata(builder, &metadata);
    if (status == APTA_ERROR_OUT_OF_MEMORY) {
        saw_oom = 1;
        goto cleanup;
    }
    if (status != APTA_STATUS_OK) {
        goto cleanup;
    }

    apta_waveform_overview_view_init(&overview);
    overview.level.frames_per_column = 1024u;
    overview.state = APTA_FEATURE_FINAL;
    overview.confidence = 90u;
    memset(&span, 0, sizeof(span));
    set_range(&span.source_range, 0u, 1024u);
    span.column_count = 1u;
    span.columns = &column;
    overview.span_count = 1u;
    overview.spans = &span;
    status = apta_result_builder_set_waveform_overview(builder, &overview);
    if (status == APTA_ERROR_OUT_OF_MEMORY) {
        saw_oom = 1;
        goto cleanup;
    }
    if (status != APTA_STATUS_OK) {
        goto cleanup;
    }

    status = apta_result_builder_finalize(builder, &result);
    if (status == APTA_ERROR_OUT_OF_MEMORY) {
        saw_oom = 1;
        if (result != NULL) {
            goto cleanup;
        }
    } else if (status != APTA_STATUS_OK || result == NULL) {
        goto cleanup;
    }

    success = fail_at_call == 0u || saw_oom;

cleanup:
    if (saw_oom) {
        success = 1;
    }
    if (result != NULL) {
        apta_result_release(result);
    }
    apta_result_builder_destroy(builder);
    if (context != NULL && apta_context_destroy(context) != APTA_STATUS_OK) {
        success = 0;
    }
    if (allocation_count_out != NULL) {
        *allocation_count_out = state.allocation_call;
    }
    return success && state.outstanding == 0u &&
           state.deallocation_call + (fail_at_call == 1u ? 0u : 0u) <=
               state.allocation_call;
}

int main(void)
{
    uint32_t total_allocations = 0u;
    uint32_t fail_at;

    CHECK(run_case(0u, &total_allocations));
    CHECK(total_allocations > 8u);
    /* Allocation 1 creates the context; builder-owned paths start at 2. */
    for (fail_at = 2u; fail_at <= total_allocations; ++fail_at) {
        if (!run_case(fail_at, NULL)) {
            fprintf(stderr, "allocation failure case %u/%u failed\n",
                    fail_at, total_allocations);
            return 1;
        }
    }
    return 0;
}
