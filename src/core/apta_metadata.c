// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <stdalign.h>
#include <string.h>

#define APTA_METADATA_FLAG_MASK APTA_METADATA_FLAG_CREATION_TIME_PRESENT

static int apta_metadata_bytes_are_zero(
    const void *data,
    size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int apta_metadata_utf8_is_valid(
    const char *data,
    uint32_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t index = 0u;

    if (size != 0u && data == NULL) {
        return 0;
    }

    while (index < size) {
        uint8_t first = bytes[index];

        if (first <= 0x7Fu) {
            index += 1u;
            continue;
        }

        if (first >= 0xC2u && first <= 0xDFu) {
            if (index + 1u >= size ||
                bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0xBFu) {
                return 0;
            }
            index += 2u;
            continue;
        }

        if (first == 0xE0u) {
            if (index + 2u >= size ||
                bytes[index + 1u] < 0xA0u ||
                bytes[index + 1u] > 0xBFu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu) {
                return 0;
            }
            index += 3u;
            continue;
        }

        if ((first >= 0xE1u && first <= 0xECu) ||
            (first >= 0xEEu && first <= 0xEFu)) {
            if (index + 2u >= size ||
                bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0xBFu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu) {
                return 0;
            }
            index += 3u;
            continue;
        }

        if (first == 0xEDu) {
            if (index + 2u >= size ||
                bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0x9Fu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu) {
                return 0;
            }
            index += 3u;
            continue;
        }

        if (first == 0xF0u) {
            if (index + 3u >= size ||
                bytes[index + 1u] < 0x90u ||
                bytes[index + 1u] > 0xBFu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu ||
                bytes[index + 3u] < 0x80u ||
                bytes[index + 3u] > 0xBFu) {
                return 0;
            }
            index += 4u;
            continue;
        }

        if (first >= 0xF1u && first <= 0xF3u) {
            if (index + 3u >= size ||
                bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0xBFu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu ||
                bytes[index + 3u] < 0x80u ||
                bytes[index + 3u] > 0xBFu) {
                return 0;
            }
            index += 4u;
            continue;
        }

        if (first == 0xF4u) {
            if (index + 3u >= size ||
                bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0x8Fu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu ||
                bytes[index + 3u] < 0x80u ||
                bytes[index + 3u] > 0xBFu) {
                return 0;
            }
            index += 4u;
            continue;
        }

        return 0;
    }

    return 1;
}

static int apta_metadata_utf8_field_is_valid(
    const apta_utf8_view_t *field,
    uint32_t maximum_size)
{
    return field->size <= maximum_size &&
           apta_metadata_utf8_is_valid(field->data, field->size);
}

static int apta_metadata_view_is_valid(
    const apta_metadata_view_t *view,
    size_t *storage_size_out)
{
    uint64_t total;

    if (view == NULL || storage_size_out == NULL) {
        return 0;
    }

    if ((view->flags & ~APTA_METADATA_FLAG_MASK) != 0u ||
        !apta_metadata_bytes_are_zero(
            view->reserved32,
            sizeof(view->reserved32)) ||
        !apta_metadata_bytes_are_zero(
            view->reserved64,
            sizeof(view->reserved64))) {
        return 0;
    }

    if (!apta_metadata_utf8_field_is_valid(
            &view->producer_name,
            APTA_METADATA_MAX_PRODUCER_NAME_BYTES) ||
        !apta_metadata_utf8_field_is_valid(
            &view->producer_version_string,
            APTA_METADATA_MAX_VERSION_STRING_BYTES) ||
        !apta_metadata_utf8_field_is_valid(
            &view->backend_name,
            APTA_METADATA_MAX_BACKEND_NAME_BYTES) ||
        !apta_metadata_utf8_field_is_valid(
            &view->backend_version,
            APTA_METADATA_MAX_VERSION_STRING_BYTES) ||
        !apta_metadata_utf8_field_is_valid(
            &view->comments,
            APTA_METADATA_MAX_COMMENTS_BYTES)) {
        return 0;
    }

    if (view->application_source_id_kind == APTA_METADATA_SOURCE_ID_NONE) {
        if (view->application_source_id.size != 0u) {
            return 0;
        }
    } else if (
        view->application_source_id_kind == APTA_METADATA_SOURCE_ID_TEXT) {
        if (view->application_source_id.size == 0u ||
            view->application_source_id.size >
                APTA_METADATA_MAX_SOURCE_ID_BYTES ||
            !apta_metadata_utf8_is_valid(
                (const char *)view->application_source_id.data,
                view->application_source_id.size)) {
            return 0;
        }
    } else if (
        view->application_source_id_kind == APTA_METADATA_SOURCE_ID_BYTES) {
        if (view->application_source_id.size == 0u ||
            view->application_source_id.size >
                APTA_METADATA_MAX_SOURCE_ID_BYTES ||
            view->application_source_id.data == NULL) {
            return 0;
        }
    } else {
        return 0;
    }

    total = (uint64_t)view->producer_name.size +
            view->producer_version_string.size +
            view->backend_name.size +
            view->backend_version.size +
            view->application_source_id.size +
            view->comments.size;
    if (total > APTA_METADATA_MAX_TOTAL_BYTES || total > SIZE_MAX) {
        return 0;
    }

    *storage_size_out = (size_t)total;
    return 1;
}

static void apta_metadata_copy_utf8(
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

static void apta_metadata_copy_bytes(
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

static apta_status_t apta_metadata_copy_validated_view(
    apta_context_t *context,
    const apta_metadata_view_t *input,
    apta_internal_metadata_t *metadata_out)
{
    size_t storage_size;
    size_t offset;

    if (context == NULL || input == NULL || metadata_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    memset(metadata_out, 0, sizeof(*metadata_out));
    if (!apta_metadata_view_is_valid(input, &storage_size)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    metadata_out->view = *input;
    metadata_out->view.struct_size =
        (uint32_t)sizeof(metadata_out->view);
    metadata_out->view.api_version = APTA_API_VERSION;
    metadata_out->view.producer_name.data = NULL;
    metadata_out->view.producer_version_string.data = NULL;
    metadata_out->view.backend_name.data = NULL;
    metadata_out->view.backend_version.data = NULL;
    metadata_out->view.application_source_id.data = NULL;
    metadata_out->view.comments.data = NULL;

    if (storage_size != 0u) {
        metadata_out->storage =
            (uint8_t *)apta_internal_context_allocate(
                context,
                storage_size,
                alignof(uint8_t),
                APTA_MEMORY_PERSISTENT);
        if (metadata_out->storage == NULL) {
            memset(metadata_out, 0, sizeof(*metadata_out));
            return APTA_ERROR_OUT_OF_MEMORY;
        }
    }
    metadata_out->storage_size = storage_size;

    offset = 0u;
    apta_metadata_copy_utf8(
        &metadata_out->view.producer_name,
        &input->producer_name,
        metadata_out->storage,
        &offset);
    apta_metadata_copy_utf8(
        &metadata_out->view.producer_version_string,
        &input->producer_version_string,
        metadata_out->storage,
        &offset);
    apta_metadata_copy_utf8(
        &metadata_out->view.backend_name,
        &input->backend_name,
        metadata_out->storage,
        &offset);
    apta_metadata_copy_utf8(
        &metadata_out->view.backend_version,
        &input->backend_version,
        metadata_out->storage,
        &offset);
    apta_metadata_copy_bytes(
        &metadata_out->view.application_source_id,
        &input->application_source_id,
        metadata_out->storage,
        &offset);
    apta_metadata_copy_utf8(
        &metadata_out->view.comments,
        &input->comments,
        metadata_out->storage,
        &offset);

    return APTA_STATUS_OK;
}

void APTA_CALL apta_metadata_init(apta_metadata_t *metadata)
{
    if (metadata != NULL) {
        memset(metadata, 0, sizeof(*metadata));
        metadata->struct_size = (uint32_t)sizeof(*metadata);
        metadata->api_version = APTA_API_VERSION;
    }
}

void APTA_CALL apta_metadata_view_init(apta_metadata_view_t *view)
{
    if (view != NULL) {
        memset(view, 0, sizeof(*view));
        view->struct_size = (uint32_t)sizeof(*view);
        view->api_version = APTA_API_VERSION;
    }
}

apta_status_t apta_internal_metadata_copy_from_input(
    apta_context_t *context,
    const apta_metadata_t *input,
    apta_internal_metadata_t *metadata_out)
{
    apta_metadata_view_t view;

    if (input == NULL || metadata_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            input,
            sizeof(*input),
            input->struct_size,
            input->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    memcpy(&view, input, sizeof(view));
    view.struct_size = (uint32_t)sizeof(view);
    return apta_metadata_copy_validated_view(
        context,
        &view,
        metadata_out);
}

apta_status_t apta_internal_metadata_copy_from_view(
    apta_context_t *context,
    const apta_metadata_view_t *input,
    apta_internal_metadata_t *metadata_out)
{
    if (input == NULL || metadata_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            input,
            sizeof(*input),
            input->struct_size,
            input->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    return apta_metadata_copy_validated_view(
        context,
        input,
        metadata_out);
}

void apta_internal_metadata_cleanup(
    apta_context_t *context,
    apta_internal_metadata_t *metadata)
{
    if (metadata == NULL) {
        return;
    }
    if (context != NULL) {
        apta_internal_context_deallocate(context, metadata->storage);
    }
    memset(metadata, 0, sizeof(*metadata));
}

int apta_internal_metadata_is_present(
    const apta_internal_metadata_t *metadata)
{
    const apta_metadata_view_t *view;

    if (metadata == NULL) {
        return 0;
    }
    view = &metadata->view;
    return view->producer_name.size != 0u ||
           view->producer_version_string.size != 0u ||
           view->backend_name.size != 0u ||
           view->backend_version.size != 0u ||
           (view->flags &
            APTA_METADATA_FLAG_CREATION_TIME_PRESENT) != 0u ||
           view->application_source_id_kind !=
               APTA_METADATA_SOURCE_ID_NONE ||
           view->comments.size != 0u;
}

apta_status_t APTA_CALL apta_session_set_metadata(
    apta_session_t *session,
    const apta_metadata_t *metadata)
{
    apta_internal_metadata_t replacement;
    apta_internal_metadata_t previous;
    apta_status_t status;

    if (session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (atomic_flag_test_and_set_explicit(
            &session->process_lock,
            memory_order_acquire)) {
        return APTA_ERROR_BUSY;
    }
    if (atomic_load_explicit(&session->state, memory_order_acquire) !=
        APTA_SESSION_CREATED) {
        atomic_flag_clear_explicit(
            &session->process_lock,
            memory_order_release);
        return APTA_ERROR_INVALID_STATE;
    }

    memset(&replacement, 0, sizeof(replacement));
    if (metadata != NULL) {
        status = apta_internal_metadata_copy_from_input(
            session->context,
            metadata,
            &replacement);
        if (status < 0) {
            atomic_flag_clear_explicit(
                &session->process_lock,
                memory_order_release);
            return status;
        }
    }

    previous = session->metadata;
    session->metadata = replacement;
    status = apta_internal_publish_result(session, 0u);
    if (status < 0) {
        session->metadata = previous;
        apta_internal_metadata_cleanup(
            session->context,
            &replacement);
    } else {
        apta_internal_metadata_cleanup(
            session->context,
            &previous);
    }

    atomic_flag_clear_explicit(
        &session->process_lock,
        memory_order_release);
    return status;
}

apta_status_t APTA_CALL apta_result_get_metadata(
    const apta_result_t *result,
    apta_metadata_view_t *view_out)
{
    if (result == NULL || view_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            view_out,
            sizeof(*view_out),
            view_out->struct_size,
            view_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (!apta_internal_metadata_is_present(&result->metadata)) {
        apta_metadata_view_init(view_out);
        return APTA_STATUS_NOT_AVAILABLE;
    }

    *view_out = result->metadata.view;
    return APTA_STATUS_OK;
}
