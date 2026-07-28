// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_RESULT_POOL_LAYOUT_H
#define APTA_RESULT_POOL_LAYOUT_H

#include "apta_internal.h"

#define APTA_INTERNAL_RESULT_SLOT_COUNT 2u

typedef struct {
    size_t total_bytes;
    size_t slot_bytes;
    size_t slot_offsets[APTA_INTERNAL_RESULT_SLOT_COUNT];

    size_t result_offset;
    size_t overview_spans_offset;
    size_t overview_columns_offset;
    size_t detail_tiles_offset;
    size_t detail_columns_offset;
    size_t metadata_offset;

    uint32_t overview_span_capacity;
    uint32_t overview_column_capacity;
    uint32_t detail_tile_capacity;
    uint32_t detail_column_capacity;
    uint32_t metadata_capacity;
    uint32_t slot_count;
} apta_internal_result_pool_layout_t;

typedef struct {
    atomic_uint active;
    uint32_t reserved32;
    size_t storage_offset;
    size_t storage_size;
} apta_internal_result_slot_control_t;

typedef struct apta_internal_result_pool_control {
    apta_context_t *context;
    atomic_uint reference_count;
    size_t allocation_size;
    uint32_t slot_count;
    uint32_t reserved32;
    apta_internal_result_pool_layout_t layout;
    apta_internal_result_slot_control_t
        slots[APTA_INTERNAL_RESULT_SLOT_COUNT];
} apta_internal_result_pool_control_t;

apta_status_t apta_internal_result_pool_calculate_layout(
    const apta_session_config_t *config,
    apta_internal_result_pool_layout_t *layout_out);

#endif /* APTA_RESULT_POOL_LAYOUT_H */
