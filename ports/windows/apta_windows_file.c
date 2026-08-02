// SPDX-License-Identifier: Apache-2.0
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <apta/desktop/apta_file.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct apta_file {
    HANDLE handle;
    uint64_t size;
};

static wchar_t *apta_windows_utf8_to_wide(const char *path)
{
    int count;
    wchar_t *wide;

    if (path == NULL || path[0] == '\0' ||
        strlen(path) > (size_t)INT_MAX) {
        return NULL;
    }
    count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(*wide)) {
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count) != count) {
        free(wide);
        return NULL;
    }
    return wide;
}

apta_status_t APTA_CALL apta_file_open_read(
    const char *path,
    apta_file_t **file_out)
{
    apta_file_t *file;
    wchar_t *wide_path;
    LARGE_INTEGER size;

    if (file_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *file_out = NULL;
    if (path == NULL || path[0] == '\0') {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    wide_path = apta_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return APTA_ERROR_SOURCE;
    }
    file = (apta_file_t *)calloc(1u, sizeof(*file));
    if (file == NULL) {
        free(wide_path);
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    file->handle = CreateFileW(
        wide_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
        NULL);
    free(wide_path);
    if (file->handle == INVALID_HANDLE_VALUE) {
        free(file);
        return APTA_ERROR_SOURCE;
    }
    if (!GetFileSizeEx(file->handle, &size) || size.QuadPart < 0) {
        (void)CloseHandle(file->handle);
        free(file);
        return APTA_ERROR_SOURCE;
    }
    file->size = (uint64_t)size.QuadPart;
    *file_out = file;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_file_get_size(
    const apta_file_t *file,
    uint64_t *size_out)
{
    if (file == NULL || file->handle == INVALID_HANDLE_VALUE ||
        size_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *size_out = file->size;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_file_read_at(
    apta_file_t *file,
    uint64_t offset,
    void *buffer,
    size_t requested_bytes,
    size_t *read_bytes_out)
{
    uint64_t remaining64;
    size_t remaining;
    size_t total = 0u;
    LARGE_INTEGER position;

    if (read_bytes_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *read_bytes_out = 0u;
    if (file == NULL || file->handle == INVALID_HANDLE_VALUE ||
        (requested_bytes != 0u && buffer == NULL)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (offset > file->size || offset > INT64_MAX) {
        return APTA_ERROR_SOURCE;
    }
    if (requested_bytes == 0u || offset == file->size) {
        return APTA_STATUS_OK;
    }
    remaining64 = file->size - offset;
    remaining = remaining64 > SIZE_MAX ? SIZE_MAX : (size_t)remaining64;
    if (remaining > requested_bytes) {
        remaining = requested_bytes;
    }
    position.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(file->handle, position, NULL, FILE_BEGIN)) {
        return APTA_ERROR_SOURCE;
    }
    while (total < remaining) {
        size_t pending = remaining - total;
        DWORD chunk = pending > MAXDWORD ? MAXDWORD : (DWORD)pending;
        DWORD received = 0u;
        if (!ReadFile(
                file->handle,
                (uint8_t *)buffer + total,
                chunk,
                &received,
                NULL)) {
            return APTA_ERROR_SOURCE;
        }
        total += received;
        if (received != chunk) {
            break;
        }
    }
    *read_bytes_out = total;
    return APTA_STATUS_OK;
}

void APTA_CALL apta_file_close(apta_file_t *file)
{
    if (file == NULL) {
        return;
    }
    if (file->handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(file->handle);
    }
    free(file);
}

static apta_status_t apta_windows_make_temporary_path(
    const wchar_t *path,
    wchar_t **temporary_out,
    HANDLE *handle_out)
{
    static volatile LONG sequence = 0;
    size_t path_size = wcslen(path);
    size_t capacity;
    wchar_t *temporary;
    unsigned int attempt;

    if (path_size > (SIZE_MAX / sizeof(*temporary)) - 48u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    capacity = path_size + 48u;
    temporary = (wchar_t *)malloc(capacity * sizeof(*temporary));
    if (temporary == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    for (attempt = 0u; attempt < 32u; ++attempt) {
        LONG serial = InterlockedIncrement(&sequence);
        DWORD error;
        int count = swprintf_s(
            temporary,
            capacity,
            L"%ls.tmp.%lu.%ld",
            path,
            (unsigned long)GetCurrentProcessId(),
            (long)serial);
        HANDLE handle;
        if (count < 0) {
            free(temporary);
            return APTA_ERROR_SOURCE;
        }
        handle = CreateFileW(
            temporary,
            GENERIC_WRITE,
            0u,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            *temporary_out = temporary;
            *handle_out = handle;
            return APTA_STATUS_OK;
        }
        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            break;
        }
    }
    free(temporary);
    return APTA_ERROR_SOURCE;
}

apta_status_t APTA_CALL apta_file_write_atomic(
    const char *path,
    const void *data,
    size_t size)
{
    wchar_t *wide_path;
    wchar_t *temporary = NULL;
    HANDLE handle;
    size_t total = 0u;
    apta_status_t status;
    int failed = 0;

    if (path == NULL || path[0] == '\0' || (size != 0u && data == NULL)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    wide_path = apta_windows_utf8_to_wide(path);
    if (wide_path == NULL) {
        return APTA_ERROR_SOURCE;
    }
    status = apta_windows_make_temporary_path(
        wide_path, &temporary, &handle);
    if (status < 0) {
        free(wide_path);
        return status;
    }
    while (total < size) {
        size_t pending = size - total;
        DWORD chunk = pending > MAXDWORD ? MAXDWORD : (DWORD)pending;
        DWORD written = 0u;
        if (!WriteFile(
                handle,
                (const uint8_t *)data + total,
                chunk,
                &written,
                NULL) || written != chunk) {
            failed = 1;
            break;
        }
        total += written;
    }
    if (failed == 0 && !FlushFileBuffers(handle)) {
        failed = 1;
    }
    if (!CloseHandle(handle)) {
        failed = 1;
    }
    if (failed == 0 && !MoveFileExW(
            temporary,
            wide_path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        failed = 1;
    }
    if (failed != 0) {
        (void)DeleteFileW(temporary);
    }
    free(temporary);
    free(wide_path);
    return failed == 0 ? APTA_STATUS_OK : APTA_ERROR_SOURCE;
}
