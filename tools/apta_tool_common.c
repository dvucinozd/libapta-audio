// SPDX-License-Identifier: Apache-2.0
#include "apta_tool_common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <apta/desktop/apta_posix_file.h>

const char *apta_tool_status_name(apta_status_t status)
{
    switch (status) {
    case APTA_STATUS_OK: return "ok";
    case APTA_STATUS_MORE_WORK: return "more-work";
    case APTA_STATUS_WOULD_BLOCK: return "would-block";
    case APTA_STATUS_END_OF_INPUT: return "end-of-input";
    case APTA_STATUS_NOT_AVAILABLE: return "not-available";
    case APTA_ERROR_INVALID_ARGUMENT: return "invalid-argument";
    case APTA_ERROR_OUT_OF_MEMORY: return "out-of-memory";
    case APTA_ERROR_UNSUPPORTED: return "unsupported";
    case APTA_ERROR_INCOMPATIBLE_VERSION: return "incompatible-version";
    case APTA_ERROR_SOURCE: return "source-error";
    case APTA_ERROR_CORRUPT_DATA: return "corrupt-data";
    case APTA_ERROR_CANCELLED: return "cancelled";
    case APTA_ERROR_INTERNAL: return "internal-error";
    case APTA_ERROR_BUSY: return "busy";
    case APTA_ERROR_CONFLICT: return "conflict";
    case APTA_ERROR_LIMIT_EXCEEDED: return "limit-exceeded";
    case APTA_ERROR_INVALID_STATE: return "invalid-state";
    case APTA_ERROR_BUFFER_TOO_SMALL: return "buffer-too-small";
    case APTA_ERROR_RESULT_SLOTS_EXHAUSTED: return "result-slots-exhausted";
    default: return "unknown-status";
    }
}

const char *apta_tool_feature_state_name(apta_feature_state_t state)
{
    switch (state) {
    case APTA_FEATURE_ABSENT: return "absent";
    case APTA_FEATURE_PARTIAL: return "partial";
    case APTA_FEATURE_PROVISIONAL: return "provisional";
    case APTA_FEATURE_STABLE: return "stable";
    case APTA_FEATURE_FINAL: return "final";
    case APTA_FEATURE_FAILED: return "failed";
    default: return "unknown";
    }
}

const char *apta_tool_session_state_name(apta_session_state_t state)
{
    switch (state) {
    case APTA_SESSION_CREATED: return "created";
    case APTA_SESSION_ACTIVE: return "active";
    case APTA_SESSION_DRAINING: return "draining";
    case APTA_SESSION_COMPLETED: return "completed";
    case APTA_SESSION_CANCELLED: return "cancelled";
    case APTA_SESSION_FAILED: return "failed";
    default: return "unknown";
    }
}

apta_status_t apta_tool_read_file(
    const char *path,
    uint64_t maximum_bytes,
    apta_tool_buffer_t *buffer_out)
{
    apta_posix_file_t *file = NULL;
    uint64_t size64;
    size_t size;
    size_t read_bytes = 0u;
    uint8_t *data = NULL;
    apta_status_t status;

    if (buffer_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    buffer_out->data = NULL;
    buffer_out->size = 0u;
    if (path == NULL || path[0] == '\0' || maximum_bytes == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_posix_file_open_read(path, &file);
    if (status < 0) {
        return status;
    }
    status = apta_posix_file_get_size(file, &size64);
    if (status < 0) {
        apta_posix_file_close(file);
        return status;
    }
    if (size64 == 0u || size64 > maximum_bytes || size64 > SIZE_MAX) {
        apta_posix_file_close(file);
        return size64 == 0u ? APTA_ERROR_CORRUPT_DATA
                            : APTA_ERROR_LIMIT_EXCEEDED;
    }
    size = (size_t)size64;
    data = (uint8_t *)malloc(size);
    if (data == NULL) {
        apta_posix_file_close(file);
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    status = apta_posix_file_read_at(file, 0u, data, size, &read_bytes);
    apta_posix_file_close(file);
    if (status < 0 || read_bytes != size) {
        free(data);
        return status < 0 ? status : APTA_ERROR_SOURCE;
    }
    buffer_out->data = data;
    buffer_out->size = size;
    return APTA_STATUS_OK;
}

void apta_tool_buffer_release(apta_tool_buffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0u;
}

apta_status_t apta_tool_write_file_atomic(
    const char *path,
    const void *data,
    size_t size)
{
    static const char suffix[] = ".tmp.XXXXXX";
    size_t path_size;
    char *temporary_path;
    int descriptor;
    FILE *file;
    size_t written;
    int failed = 0;
    int saved_errno = 0;

    if (path == NULL || path[0] == '\0' || (size != 0u && data == NULL)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    path_size = strlen(path);
    if (path_size > SIZE_MAX - sizeof(suffix)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    temporary_path = (char *)malloc(path_size + sizeof(suffix));
    if (temporary_path == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memcpy(temporary_path, path, path_size);
    memcpy(temporary_path + path_size, suffix, sizeof(suffix));

    descriptor = mkstemp(temporary_path);
    if (descriptor < 0) {
        free(temporary_path);
        return APTA_ERROR_SOURCE;
    }
    file = fdopen(descriptor, "wb");
    if (file == NULL) {
        saved_errno = errno;
        (void)close(descriptor);
        (void)unlink(temporary_path);
        free(temporary_path);
        errno = saved_errno;
        return APTA_ERROR_SOURCE;
    }

    written = size == 0u ? 0u : fwrite(data, 1u, size, file);
    if (written != size) {
        failed = 1;
        saved_errno = errno != 0 ? errno : EIO;
    }
    if (!failed && fflush(file) != 0) {
        failed = 1;
        saved_errno = errno != 0 ? errno : EIO;
    }
    if (!failed && fsync(descriptor) != 0) {
        failed = 1;
        saved_errno = errno != 0 ? errno : EIO;
    }
    if (fclose(file) != 0 && !failed) {
        failed = 1;
        saved_errno = errno != 0 ? errno : EIO;
    }
    if (failed) {
        (void)unlink(temporary_path);
        free(temporary_path);
        errno = saved_errno;
        return APTA_ERROR_SOURCE;
    }
    if (rename(temporary_path, path) != 0) {
        saved_errno = errno;
        (void)unlink(temporary_path);
        free(temporary_path);
        errno = saved_errno;
        return APTA_ERROR_SOURCE;
    }
    free(temporary_path);
    return APTA_STATUS_OK;
}

static int apta_tool_token_equals(
    const char *token,
    size_t token_size,
    const char *expected)
{
    size_t expected_size = strlen(expected);
    return token_size == expected_size &&
           memcmp(token, expected, token_size) == 0;
}

apta_status_t apta_tool_parse_feature_list(
    const char *text,
    apta_feature_mask_t *features_out)
{
    const char *cursor;
    apta_feature_mask_t features = 0u;

    if (text == NULL || features_out == NULL || text[0] == '\0') {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    cursor = text;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, ',');
        size_t token_size = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (token_size == 0u) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        if (apta_tool_token_equals(cursor, token_size, "waveform")) {
            features |= APTA_FEATURE_WAVEFORM_OVERVIEW;
        } else if (apta_tool_token_equals(cursor, token_size, "detail")) {
            features |= APTA_FEATURE_WAVEFORM_OVERVIEW |
                        APTA_FEATURE_WAVEFORM_DETAIL;
        } else if (apta_tool_token_equals(cursor, token_size, "bpm")) {
            features |= APTA_FEATURE_WAVEFORM_OVERVIEW |
                        APTA_FEATURE_BPM |
                        APTA_FEATURE_CONFIDENCE;
        } else if (apta_tool_token_equals(cursor, token_size, "beatgrid")) {
            features |= APTA_FEATURE_WAVEFORM_OVERVIEW |
                        APTA_FEATURE_BPM |
                        APTA_FEATURE_LOCAL_BEATGRID |
                        APTA_FEATURE_CONFIDENCE;
        } else if (apta_tool_token_equals(cursor, token_size, "global")) {
            features |= APTA_FEATURE_WAVEFORM_OVERVIEW |
                        APTA_FEATURE_BPM |
                        APTA_FEATURE_LOCAL_BEATGRID |
                        APTA_FEATURE_GLOBAL_BEATGRID |
                        APTA_FEATURE_CONFIDENCE;
        } else if (apta_tool_token_equals(cursor, token_size, "dynamic")) {
            features |= APTA_FEATURE_WAVEFORM_OVERVIEW |
                        APTA_FEATURE_BPM |
                        APTA_FEATURE_LOCAL_BEATGRID |
                        APTA_FEATURE_GLOBAL_BEATGRID |
                        APTA_FEATURE_DYNAMIC_TEMPO |
                        APTA_FEATURE_CONFIDENCE;
        } else if (apta_tool_token_equals(cursor, token_size, "all")) {
            /* "all" has to mean all. It previously stopped at the local grid,
             * which left the global grid and dynamic tempo with no CLI path at
             * all: every S6 figure on record came from synthetic harnesses,
             * and no shipped tool could run that code over real audio. */
            features |= APTA_FEATURE_WAVEFORM_OVERVIEW |
                        APTA_FEATURE_WAVEFORM_DETAIL |
                        APTA_FEATURE_BPM |
                        APTA_FEATURE_LOCAL_BEATGRID |
                        APTA_FEATURE_GLOBAL_BEATGRID |
                        APTA_FEATURE_DYNAMIC_TEMPO |
                        APTA_FEATURE_CONFIDENCE;
        } else {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        if (end == NULL) {
            break;
        }
        cursor = end + 1u;
    }
    *features_out = features;
    return APTA_STATUS_OK;
}

void apta_tool_print_feature_list(
    FILE *stream,
    apta_feature_mask_t features)
{
    int first = 1;
    struct feature_name {
        apta_feature_mask_t feature;
        const char *name;
    } names[] = {
        {APTA_FEATURE_WAVEFORM_OVERVIEW, "waveform-overview"},
        {APTA_FEATURE_WAVEFORM_DETAIL, "waveform-detail"},
        {APTA_FEATURE_BPM, "bpm"},
        {APTA_FEATURE_LOCAL_BEATGRID, "local-beatgrid"},
        {APTA_FEATURE_CONFIDENCE, "confidence"},
        {APTA_FEATURE_GRID_LOCKING, "grid-locking"}
    };
    size_t index;

    if (stream == NULL) {
        return;
    }
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
        if ((features & names[index].feature) != 0u) {
            fprintf(stream, "%s%s", first ? "" : ",", names[index].name);
            first = 0;
        }
    }
    if (first) {
        fputs("none", stream);
    }
}

void apta_tool_json_string(
    FILE *stream,
    const char *data,
    size_t size)
{
    size_t index;

    fputc('"', stream);
    for (index = 0u; index < size; ++index) {
        unsigned char value = (unsigned char)data[index];
        switch (value) {
        case '"': fputs("\\\"", stream); break;
        case '\\': fputs("\\\\", stream); break;
        case '\b': fputs("\\b", stream); break;
        case '\f': fputs("\\f", stream); break;
        case '\n': fputs("\\n", stream); break;
        case '\r': fputs("\\r", stream); break;
        case '\t': fputs("\\t", stream); break;
        default:
            if (value < 0x20u) {
                fprintf(stream, "\\u%04x", (unsigned)value);
            } else {
                fputc(value, stream);
            }
            break;
        }
    }
    fputc('"', stream);
}
