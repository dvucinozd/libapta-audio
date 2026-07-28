// SPDX-License-Identifier: Apache-2.0
#include "apta_session_workspace.h"

#include <stdalign.h>
#include <string.h>

#define APTA_WORKSPACE_METADATA_FLAG_MASK                            \
    (APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |                     \
     APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT |                  \
     APTA_METADATA_FLAG_BACKEND_NAME_PRESENT |                      \
     APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT |                   \
     APTA_METADATA_FLAG_CREATION_TIME_PRESENT |                     \
     APTA_METADATA_FLAG_COMMENTS_PRESENT)

APTA_API apta_status_t APTA_CALL apta_session_set_metadata_base(
    apta_session_t *session,
    const apta_metadata_t *metadata);

static int apta_workspace_metadata_bytes_are_zero(
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

static int apta_workspace_metadata_utf8_is_valid(
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
        } else if (first >= 0xC2u && first <= 0xDFu) {
            if (index + 1u >= size || bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0xBFu) {
                return 0;
            }
            index += 2u;
        } else if (first == 0xE0u) {
            if (index + 2u >= size || bytes[index + 1u] < 0xA0u ||
                bytes[index + 1u] > 0xBFu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu) {
                return 0;
            }
            index += 3u;
        } else if ((first >= 0xE1u && first <= 0xECu) ||
                   (first >= 0xEEu && first <= 0xEFu)) {
            if (index + 2u >= size || bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0xBFu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu) {
                return 0;
            }
            index += 3u;
        } else if (first == 0xEDu) {
            if (index + 2u >= size || bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0x9Fu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu) {
                return 0;
            }
            index += 3u;
        } else if (first == 0xF0u) {
            if (index + 3u >= size || bytes[index + 1u] < 0x90u ||
                bytes[index + 1u] > 0xBFu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu ||
                bytes[index + 3u] < 0x80u ||
                bytes[index + 3u] > 0xBFu) {
                return 0;
            }
            index += 4u;
        } else if (first >= 0xF1u && first <= 0xF3u) {
            if (index + 3u >= size || bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0xBFu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu ||
                bytes[index + 3u] < 0x80u ||
                bytes[index + 3u] > 0xBFu) {
                return 0;
            }
            index += 4u;
        } else if (first == 0xF4u) {
            if (index + 3u >= size || bytes[index + 1u] < 0x80u ||
                bytes[index + 1u] > 0x8Fu ||
                bytes[index + 2u] < 0x80u ||
                bytes[index + 2u] > 0xBFu ||
                bytes[index + 3u] < 0x80u ||
                bytes[index + 3u] > 0xBFu) {
                return 0;
            }
            index += 4u;
        } else {
            return 0;
        }
    }

    return 1;
}

static int apta_workspace_metadata_text_is_valid(
    const apta_utf8_view_t *field,
    uint32_t maximum_size,
    int present)
{
    if (!present) {
        return field->size == 0u;
    }
    return field->size <= maximum_size &&
           apta_workspace_metadata_utf8_is_valid(
               field->data,
               field->size);
}

static int apta_workspace_metadata_input_is_valid(
    const apta_metadata_t *metadata,
    size_t *storage_size_out)
{
    uint64_t total;

    if ((metadata->flags & ~APTA_WORKSPACE_METADATA_FLAG_MASK) != 0u ||
        !apta_workspace_metadata_bytes_are_zero(
            metadata->reserved32,
            sizeof(metadata->reserved32)) ||
        !apta_workspace_metadata_bytes_are_zero(
            metadata->reserved64,
            sizeof(metadata->reserved64))) {
        return 0;
    }

    if (!apta_workspace_metadata_text_is_valid(
            &metadata->producer_name,
            APTA_METADATA_MAX_PRODUCER_NAME_BYTES,
            (metadata->flags &
             APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT) != 0u) ||
        !apta_workspace_metadata_text_is_valid(
            &metadata->producer_version_string,
            APTA_METADATA_MAX_VERSION_STRING_BYTES,
            (metadata->flags &
             APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT) != 0u) ||
        !apta_workspace_metadata_text_is_valid(
            &metadata->backend_name,
            APTA_METADATA_MAX_BACKEND_NAME_BYTES,
            (metadata->flags &
             APTA_METADATA_FLAG_BACKEND_NAME_PRESENT) != 0u) ||
        !apta_workspace_metadata_text_is_valid(
            &metadata->backend_version,
            APTA_METADATA_MAX_VERSION_STRING_BYTES,
            (metadata->flags &
             APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT) != 0u) ||
        !apta_workspace_metadata_text_is_valid(
            &metadata->comments,
            APTA_METADATA_MAX_COMMENTS_BYTES,
            (metadata->flags &
             APTA_METADATA_FLAG_COMMENTS_PRESENT) != 0u)) {
        return 0;
    }

    if (metadata->application_source_id_kind ==
        APTA_METADATA_SOURCE_ID_NONE) {
        if (metadata->application_source_id.size != 0u) {
            return 0;
        }
    } else if (metadata->application_source_id_kind ==
               APTA_METADATA_SOURCE_ID_TEXT) {
        if (metadata->application_source_id.size >
                APTA_METADATA_MAX_SOURCE_ID_BYTES ||
            !apta_workspace_metadata_utf8_is_valid(
                (const char *)metadata->application_source_id.data,
                metadata->application_source_id.size)) {
            return 0;
        }
    } else if (metadata->application_source_id_kind ==
               APTA_METADATA_SOURCE_ID_BYTES) {
        if (metadata->application_source_id.size >
                APTA_METADATA_MAX_SOURCE_ID_BYTES ||
            (metadata->application_source_id.size != 0u &&
             metadata->application_source_id.data == NULL)) {
            return 0;
        }
    } else {
        return 0;
    }

    total = (uint64_t)metadata->producer_name.size +
            metadata->producer_version_string.size +
            metadata->backend_name.size +
            metadata->backend_version.size +
            metadata->application_source_id.size +
            metadata->comments.size;
    if (total > APTA_METADATA_MAX_TOTAL_BYTES || total > SIZE_MAX) {
        return 0;
    }

    *storage_size_out = (size_t)total;
    return 1;
}

static void apta_workspace_metadata_copy_text(
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

static void apta_workspace_metadata_copy_bytes(
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

static apta_status_t apta_workspace_metadata_copy_input(
    apta_session_t *session,
    const apta_metadata_t *input,
    apta_internal_metadata_t *metadata_out)
{
    apta_metadata_view_t view;
    size_t storage_size;
    size_t offset = 0u;

    if (!apta_internal_validate_struct(
            input,
            sizeof(*input),
            input->struct_size,
            input->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (!apta_workspace_metadata_input_is_valid(input, &storage_size)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    memset(metadata_out, 0, sizeof(*metadata_out));
    memcpy(&view, input, sizeof(view));
    view.struct_size = (uint32_t)sizeof(view);
    view.api_version = APTA_API_VERSION;
    view.producer_name.data = NULL;
    view.producer_version_string.data = NULL;
    view.backend_name.data = NULL;
    view.backend_version.data = NULL;
    view.application_source_id.data = NULL;
    view.comments.data = NULL;

    metadata_out->view = view;
    metadata_out->storage_size = storage_size;
    metadata_out->present = 1u;
    if (storage_size != 0u) {
        metadata_out->storage =
            (uint8_t *)apta_internal_session_allocate(
                session,
                storage_size,
                alignof(uint8_t),
                APTA_MEMORY_PERSISTENT);
        if (metadata_out->storage == NULL) {
            memset(metadata_out, 0, sizeof(*metadata_out));
            return APTA_ERROR_OUT_OF_MEMORY;
        }
    }

    apta_workspace_metadata_copy_text(
        &metadata_out->view.producer_name,
        &input->producer_name,
        metadata_out->storage,
        &offset);
    apta_workspace_metadata_copy_text(
        &metadata_out->view.producer_version_string,
        &input->producer_version_string,
        metadata_out->storage,
        &offset);
    apta_workspace_metadata_copy_text(
        &metadata_out->view.backend_name,
        &input->backend_name,
        metadata_out->storage,
        &offset);
    apta_workspace_metadata_copy_text(
        &metadata_out->view.backend_version,
        &input->backend_version,
        metadata_out->storage,
        &offset);
    apta_workspace_metadata_copy_bytes(
        &metadata_out->view.application_source_id,
        &input->application_source_id,
        metadata_out->storage,
        &offset);
    apta_workspace_metadata_copy_text(
        &metadata_out->view.comments,
        &input->comments,
        metadata_out->storage,
        &offset);

    return APTA_STATUS_OK;
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
    if (!apta_internal_session_uses_workspace(session)) {
        return apta_session_set_metadata_base(session, metadata);
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
    apta_metadata_view_init(&replacement.view);
    if (metadata != NULL) {
        status = apta_workspace_metadata_copy_input(
            session,
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
