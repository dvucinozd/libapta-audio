// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_INTERNAL_H
#define APTA_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include <apta/apta.h>

#define APTA_INTERNAL_MAX_REGION_REQUESTS 16u

typedef struct {
    void *raw_memory;
    size_t allocated_size;
    size_t requested_size;
} apta_allocation_header_t;

typedef struct {
    uint32_t request_id;
    apta_region_request_t request;
    apta_request_state_t state;
    uint32_t diagnostic_code;
} apta_internal_request_t;

struct apta_context {
    apta_allocator_t allocator;
    apta_logger_t logger;
    apta_clock_t clock;

    apta_feature_mask_t capabilities;
    uint64_t memory_limit_bytes;
    uint32_t flags;

    atomic_size_t allocated_bytes;
    atomic_uint session_count;
    atomic_uint result_count;
    atomic_uint_fast64_t lineage_counter;
};

struct apta_result {
    apta_context_t *context;
    atomic_uint reference_count;
    apta_result_info_t info;
};

struct apta_session {
    apta_context_t *context;
    apta_session_config_t config;

    atomic_uint state;
    atomic_uint cancel_requested;
    atomic_flag process_lock;
    atomic_flag result_lock;

    apta_result_t *current_result;
    apta_generation_t generation;
    uint64_t lineage_id_high;
    uint64_t lineage_id_low;

    uint32_t has_pull_source;
    apta_pcm_source_t pull_source;

    uint32_t has_focus;
    apta_focus_t focus;

    uint32_t end_of_input_signalled;
    apta_source_frame_t final_end_frame;

    uint32_t next_request_id;
    apta_internal_request_t requests[APTA_INTERNAL_MAX_REGION_REQUESTS];
};

int apta_internal_validate_struct(
    const void *structure,
    size_t minimum_size,
    uint32_t structure_size,
    uint32_t api_version);

int apta_internal_is_power_of_two(size_t value);

void *apta_internal_context_allocate(
    apta_context_t *context,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags);

void apta_internal_context_deallocate(
    apta_context_t *context,
    void *memory);

void apta_internal_log(
    apta_context_t *context,
    apta_log_level_t level,
    uint32_t diagnostic_code,
    const char *message);

apta_status_t apta_internal_publish_result(
    apta_session_t *session,
    apta_feature_mask_t changed_features);

void apta_internal_result_retain(apta_result_t *result);
void apta_internal_result_release(apta_result_t *result);

#endif /* APTA_INTERNAL_H */
