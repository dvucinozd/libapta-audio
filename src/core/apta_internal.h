// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_INTERNAL_H
#define APTA_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include <apta/apta.h>

#define APTA_INTERNAL_MAX_REGION_REQUESTS 16u
#define APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN 1024u
#define APTA_INTERNAL_DETAIL_LEVEL_ID 1u
#define APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN 256u
#define APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE 64u
#define APTA_INTERNAL_DETAIL_TILE_FRAMES \
    (APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN * \
     APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE)
#define APTA_INTERNAL_MAX_DETAIL_TILES 4u
#define APTA_INTERNAL_PROCESS_CHUNK_FRAMES 256u
#define APTA_INTERNAL_MAX_PUSH_FRAMES 4096u

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

typedef struct {
    apta_source_frame_t first_frame;
    apta_source_frame_t end_frame;
} apta_internal_range_t;

typedef struct apta_internal_pcm_node {
    struct apta_internal_pcm_node *next;
    apta_source_frame_t first_frame;
    uint32_t frame_count;
    uint32_t processed_frames;
    float samples[];
} apta_internal_pcm_node_t;

typedef struct {
    uint32_t column_index;
    uint32_t sample_count;
    double sum_squares;
    float minimum;
    float maximum;
    uint8_t clipped;
    uint8_t complete;
    uint16_t reserved16;
} apta_internal_waveform_accumulator_t;

typedef struct {
    uint32_t tile_index;
    uint32_t complete_count;
    uint64_t access_serial;
    uint8_t occupied;
    uint8_t reserved8[7];
    apta_internal_waveform_accumulator_t
        accumulators[APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE];
} apta_internal_detail_tile_t;

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

    apta_source_frame_t total_source_frames;
    uint32_t source_sample_rate;
    uint32_t source_channel_count;
    apta_channel_layout_t source_channel_layout;

    apta_waveform_overview_view_t overview;
    apta_waveform_span_t *overview_spans;
    apta_waveform_column_t *overview_columns;

    uint32_t detail_tile_count;
    apta_waveform_tile_view_t *detail_tiles;
    apta_waveform_column_t *detail_columns;
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

    apta_internal_pcm_node_t *pcm_head;
    apta_internal_pcm_node_t *pcm_tail;
    uint64_t queued_pcm_frames;

    apta_internal_range_t *accepted_ranges;
    uint32_t accepted_range_count;
    uint32_t accepted_range_capacity;
    apta_source_frame_t greatest_accepted_end;
    apta_source_frame_t maximum_accepted_end;

    apta_internal_waveform_accumulator_t *overview_accumulators;
    uint32_t overview_accumulator_count;
    uint32_t overview_accumulator_capacity;
    uint32_t overview_complete_count;
    uint32_t overview_frames_per_column;

    apta_internal_detail_tile_t detail_tiles[APTA_INTERNAL_MAX_DETAIL_TILES];
    uint64_t detail_access_serial;
    uint64_t detail_mutation_serial;
    uint64_t detail_published_serial;
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

apta_status_t apta_internal_session_transition(
    apta_session_t *session,
    apta_session_state_t new_state);

apta_status_t apta_internal_waveform_accept_pcm(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

apta_status_t apta_internal_waveform_accept_pcm_base(
    apta_session_t *session,
    const apta_pcm_block_t *block,
    uint32_t *accepted_frames_out);

apta_status_t apta_internal_waveform_process(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out);

apta_status_t apta_internal_waveform_process_base(
    apta_session_t *session,
    const apta_work_budget_t *budget,
    apta_progress_t *progress_out,
    uint32_t *did_work_out,
    uint32_t *published_output_out);

apta_status_t apta_internal_waveform_next_pcm_request(
    apta_session_t *session,
    apta_pcm_request_t *request_out);

apta_status_t apta_internal_waveform_build_snapshot(
    apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_waveform_build_overview_snapshot(
    apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_detail_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
    float sample);

void apta_internal_detail_refresh_completed(apta_session_t *session);
void apta_internal_detail_update_request_states(apta_session_t *session);

int apta_internal_detail_range_complete(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame);

int apta_internal_detail_range_has_output(
    const apta_session_t *session,
    apta_source_frame_t first_frame,
    apta_source_frame_t end_frame);

apta_status_t apta_internal_detail_build_snapshot(
    apta_session_t *session,
    apta_result_t *result);

void apta_internal_waveform_cleanup_session(apta_session_t *session);
void apta_internal_waveform_cleanup_result(apta_result_t *result);
void apta_internal_waveform_cleanup_result_base(apta_result_t *result);

uint32_t apta_internal_crc32c(const uint8_t *data, size_t size);

#endif /* APTA_INTERNAL_H */
