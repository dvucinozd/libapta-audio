// SPDX-License-Identifier: Apache-2.0
#include "apta_result_pool.h"

#include <string.h>

static void apta_pool_metadata_copy_text(
    apta_utf8_view_t *destination,
    const apta_utf8_view_t *source,
    uint8_t *storage,
    size_t *offset)
{
    destination->size = source->size;
    if (source->size == 0u) {
        destination->data = NULL;
        return;
    }

    memcpy(storage + *offset, source->data, source->size);
    destination->data = (const char *)(storage + *offset);
    *offset += source->size;
}

static void apta_pool_metadata_copy_bytes(
    apta_bytes_view_t *destination,
    const apta_bytes_view_t *source,
    uint8_t *storage,
    size_t *offset)
{
    destination->size = source->size;
    if (source->size == 0u) {
        destination->data = NULL;
        return;
    }

    memcpy(storage + *offset, source->data, source->size);
    destination->data = storage + *offset;
    *offset += source->size;
}

apta_status_t apta_internal_result_pool_create_metadata_result(
    apta_internal_result_pool_control_t *pool,
    const apta_session_config_t *config,
    const apta_internal_metadata_t *metadata,
    apta_generation_t generation,
    apta_session_state_t session_state,
    apta_feature_mask_t changed_features,
    uint64_t lineage_id_high,
    uint64_t lineage_id_low,
    apta_result_t **result_out)
{
    apta_result_t *result = NULL;
    uint8_t *slot_storage;
    uint8_t *metadata_storage;
    size_t offset = 0u;
    apta_status_t status;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;

    status = apta_internal_result_pool_create_empty_result(
        pool,
        config,
        generation,
        session_state,
        changed_features,
        lineage_id_high,
        lineage_id_low,
        &result);
    if (status < 0) {
        return status;
    }

    if (metadata == NULL ||
        !apta_internal_metadata_is_present(metadata)) {
        *result_out = result;
        return APTA_STATUS_OK;
    }

    if (metadata->storage_size > pool->layout.metadata_capacity) {
        apta_internal_result_release(result);
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    slot_storage = (uint8_t *)apta_internal_result_pool_get_slot_storage(
        pool,
        result->result_pool_slot_index);
    if (slot_storage == NULL ||
        pool->layout.metadata_offset > pool->layout.slot_bytes ||
        pool->layout.metadata_capacity >
            pool->layout.slot_bytes - pool->layout.metadata_offset) {
        apta_internal_result_release(result);
        return APTA_ERROR_INTERNAL;
    }

    metadata_storage = slot_storage + pool->layout.metadata_offset;
    result->metadata.view = metadata->view;
    result->metadata.view.struct_size =
        (uint32_t)sizeof(result->metadata.view);
    result->metadata.view.api_version = APTA_API_VERSION;
    result->metadata.view.producer_name.data = NULL;
    result->metadata.view.producer_version_string.data = NULL;
    result->metadata.view.backend_name.data = NULL;
    result->metadata.view.backend_version.data = NULL;
    result->metadata.view.application_source_id.data = NULL;
    result->metadata.view.comments.data = NULL;
    result->metadata.storage =
        metadata->storage_size != 0u ? metadata_storage : NULL;
    result->metadata.storage_size = metadata->storage_size;
    result->metadata.present = 1u;

    apta_pool_metadata_copy_text(
        &result->metadata.view.producer_name,
        &metadata->view.producer_name,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_text(
        &result->metadata.view.producer_version_string,
        &metadata->view.producer_version_string,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_text(
        &result->metadata.view.backend_name,
        &metadata->view.backend_name,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_text(
        &result->metadata.view.backend_version,
        &metadata->view.backend_version,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_bytes(
        &result->metadata.view.application_source_id,
        &metadata->view.application_source_id,
        metadata_storage,
        &offset);
    apta_pool_metadata_copy_text(
        &result->metadata.view.comments,
        &metadata->view.comments,
        metadata_storage,
        &offset);

    if (offset != metadata->storage_size) {
        apta_internal_result_release(result);
        return APTA_ERROR_INTERNAL;
    }

    *result_out = result;
    return APTA_STATUS_OK;
}
