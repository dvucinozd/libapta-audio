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
#define APTA_INTERNAL_SCHEDULER_AGE_STEP 8u
#define APTA_INTERNAL_SCHEDULER_MAX_SKIPS 32u
#define APTA_INTERNAL_RESULT_FLAG_POOLED (1u << 0)

#define APTA_INTERNAL_ONSET_FRAMES_PER_BIN 256u
#define APTA_INTERNAL_ONSET_BIN_CAPACITY 4096u
#define APTA_INTERNAL_MIN_TEMPO_BINS 512u
#define APTA_INTERNAL_STABLE_TEMPO_BINS 1024u
#define APTA_INTERNAL_MAX_TEMPO_CANDIDATES 3u

#define APTA_INTERNAL_S4_FEATURES \
    (APTA_FEATURE_BPM | APTA_FEATURE_LOCAL_BEATGRID | \
     APTA_FEATURE_CONFIDENCE | APTA_FEATURE_GRID_LOCKING)

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
    uint32_t scheduler_skip_count;
    uint32_t reserved32;
    uint64_t scheduler_enqueue_serial;
} apta_internal_request_t;

typedef struct {
    uint32_t effective_priority;
    uint32_t request_id;
    uint64_t soft_deadline_monotonic_ns;
    uint64_t enqueue_serial;
} apta_internal_schedule_score_t;

typedef struct {
    apta_metadata_view_t view;
    uint8_t *storage;
    size_t storage_size;
    uint32_t present;
    uint32_t reserved32;
} apta_internal_metadata_t;

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

typedef struct {
    uint64_t bin_index;
    double sum_absolute;
    uint32_t sample_count;
    uint8_t occupied;
    uint8_t reserved8[3];
} apta_internal_onset_bin_t;

typedef struct apta_internal_result_pool_control
    apta_internal_result_pool_control_t;

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
    apta_internal_result_pool_control_t *result_pool;
    uint32_t result_pool_slot_index;
    uint32_t result_flags;
    apta_result_info_t info;

    apta_source_frame_t total_source_frames;
    uint32_t source_sample_rate;
    uint32_t source_channel_count;
    apta_channel_layout_t source_channel_layout;

    apta_internal_metadata_t metadata;

    apta_waveform_overview_view_t overview;
    apta_waveform_span_t *overview_spans;
    apta_waveform_column_t *overview_columns;

    uint32_t detail_tile_count;
    apta_waveform_tile_view_t *detail_tiles;
    apta_waveform_column_t *detail_columns;

    apta_tempo_view_t tempo;
    apta_tempo_candidate_t *tempo_candidates;

    apta_grid_view_t local_grid;
    apta_frame_range_t *local_grid_coverage;
    apta_grid_segment_t *local_grid_segments;
};

struct apta_session {
    apta_context_t *context;
    apta_session_config_t config;

    atomic_uint state;
    atomic_uint cancel_requested;
    atomic_flag process_lock;
    atomic_flag result_lock;

    apta_result_t *current_result;
    apta_internal_result_pool_control_t *result_pool;
    apta_generation_t generation;
    uint64_t lineage_id_high;
    uint64_t lineage_id_low;

    apta_internal_metadata_t metadata;

    uint32_t has_pull_source;
    apta_pcm_source_t pull_source;

    uint32_t has_focus;
    apta_focus_t focus;

    uint32_t end_of_input_signalled;
    apta_source_frame_t final_end_frame;

    uint32_t next_request_id;
    uint64_t next_scheduler_enqueue_serial;
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

    apta_internal_onset_bin_t *onset_bins;
    uint32_t onset_bin_capacity;
    uint32_t tempo_candidate_count;
    apta_tempo_value_t tempo_value;
    apta_tempo_candidate_t tempo_candidates[APTA_INTERNAL_MAX_TEMPO_CANDIDATES];
    apta_grid_segment_t local_grid_segment;
    apta_frame_range_t local_grid_requested_range;
    apta_frame_range_t local_grid_evidence_range;
    apta_frame_range_t local_grid_applicability_range;
    apta_frame_range_t local_grid_coverage_range;
    uint32_t has_tempo;
    uint32_t has_local_grid;
    uint32_t local_grid_locked;
    uint32_t tempo_candidate_set_id;
    uint32_t local_grid_segment_id;
    uint64_t s4_mutation_serial;
    uint64_t s4_published_serial;
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

apta_status_t apta_internal_metadata_copy_from_input(
    apta_context_t *context,
    const apta_metadata_t *input,
    apta_internal_metadata_t *metadata_out);

apta_status_t apta_internal_metadata_copy_from_view(
    apta_context_t *context,
    const apta_metadata_view_t *input,
    apta_internal_metadata_t *metadata_out);

void apta_internal_metadata_cleanup(
    apta_context_t *context,
    apta_internal_metadata_t *metadata);

int apta_internal_metadata_is_present(
    const apta_internal_metadata_t *metadata);

apta_status_t apta_internal_scheduler_register_request(
    apta_session_t *session,
    apta_internal_request_t *request);

void apta_internal_scheduler_score_request(
    const apta_internal_request_t *request,
    apta_internal_schedule_score_t *score_out);

int apta_internal_scheduler_score_better(
    const apta_internal_schedule_score_t *candidate,
    const apta_internal_schedule_score_t *current);

void apta_internal_scheduler_note_choice(
    apta_session_t *session,
    uint32_t selected_request_id);

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

apta_status_t apta_internal_s4_prepare(apta_session_t *session);
apta_status_t apta_internal_s4_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
    float sample);
apta_status_t apta_internal_s4_refresh(apta_session_t *session);
apta_feature_mask_t apta_internal_s4_pending_features(
    const apta_session_t *session);
void apta_internal_s4_mark_published(apta_session_t *session);
apta_status_t apta_internal_s4_build_snapshot(
    apta_session_t *session,
    apta_result_t *result);
void apta_internal_s4_cleanup_session(apta_session_t *session);
void apta_internal_s4_cleanup_result(apta_result_t *result);

void apta_internal_waveform_cleanup_session(apta_session_t *session);
void apta_internal_waveform_cleanup_result(apta_result_t *result);
void apta_internal_waveform_cleanup_result_base(apta_result_t *result);

uint32_t apta_internal_crc32c(const uint8_t *data, size_t size);

#endif /* APTA_INTERNAL_H */
