// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_RESULT_POOL_H
#define APTA_RESULT_POOL_H

#include "apta_result_pool_layout.h"

apta_status_t apta_internal_result_pool_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_internal_result_pool_control_t **pool_out);

void apta_internal_result_pool_retain(
    apta_internal_result_pool_control_t *pool);

void apta_internal_result_pool_release(
    apta_internal_result_pool_control_t *pool);

apta_status_t apta_internal_result_pool_create_empty_result(
    apta_internal_result_pool_control_t *pool,
    const apta_session_config_t *config,
    apta_generation_t generation,
    apta_session_state_t session_state,
    apta_feature_mask_t changed_features,
    uint64_t lineage_id_high,
    uint64_t lineage_id_low,
    apta_result_t **result_out);

void apta_internal_result_pool_release_result_slot(
    apta_internal_result_pool_control_t *pool,
    uint32_t slot_index);

size_t apta_internal_result_pool_get_allocation_size(
    const apta_internal_result_pool_control_t *pool);

uint32_t apta_internal_result_pool_get_slot_count(
    const apta_internal_result_pool_control_t *pool);

const apta_internal_result_pool_layout_t *
apta_internal_result_pool_get_layout(
    const apta_internal_result_pool_control_t *pool);

void *apta_internal_result_pool_get_slot_storage(
    apta_internal_result_pool_control_t *pool,
    uint32_t slot_index);

#endif /* APTA_RESULT_POOL_H */
