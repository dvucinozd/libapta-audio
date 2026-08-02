// SPDX-License-Identifier: Apache-2.0
#include <apta/desktop/apta_posix_file.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct apta_file {
    FILE *stream;
    uint64_t size;
};

apta_status_t APTA_CALL apta_file_open_read(
    const char *path,
    apta_file_t **file_out)
{
    apta_file_t *file;
    off_t end_offset;

    if (file_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *file_out = NULL;
    if (path == NULL || path[0] == '\0') {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    file = (apta_file_t *)calloc(1u, sizeof(*file));
    if (file == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    file->stream = fopen(path, "rb");
    if (file->stream == NULL) {
        free(file);
        return APTA_ERROR_SOURCE;
    }
    if (fseeko(file->stream, (off_t)0, SEEK_END) != 0) {
        fclose(file->stream);
        free(file);
        return APTA_ERROR_SOURCE;
    }
    end_offset = ftello(file->stream);
    if (end_offset < 0 || fseeko(file->stream, (off_t)0, SEEK_SET) != 0) {
        fclose(file->stream);
        free(file);
        return APTA_ERROR_SOURCE;
    }
    file->size = (uint64_t)end_offset;
    *file_out = file;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_file_get_size(
    const apta_file_t *file,
    uint64_t *size_out)
{
    if (file == NULL || size_out == NULL) {
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
    size_t remaining;
    size_t to_read;
    size_t read_bytes;

    if (read_bytes_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *read_bytes_out = 0u;
    if (file == NULL || file->stream == NULL ||
        (requested_bytes != 0u && buffer == NULL)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (offset > file->size || offset > (uint64_t)INT64_MAX) {
        return APTA_ERROR_SOURCE;
    }
    if (requested_bytes == 0u || offset == file->size) {
        return APTA_STATUS_OK;
    }

    remaining = file->size - offset > (uint64_t)SIZE_MAX
                    ? SIZE_MAX
                    : (size_t)(file->size - offset);
    to_read = requested_bytes < remaining ? requested_bytes : remaining;
    if (fseeko(file->stream, (off_t)offset, SEEK_SET) != 0) {
        return APTA_ERROR_SOURCE;
    }

    clearerr(file->stream);
    read_bytes = fread(buffer, 1u, to_read, file->stream);
    if (read_bytes != to_read && ferror(file->stream)) {
        return APTA_ERROR_SOURCE;
    }
    *read_bytes_out = read_bytes;
    return APTA_STATUS_OK;
}

void APTA_CALL apta_file_close(apta_file_t *file)
{
    if (file == NULL) {
        return;
    }
    if (file->stream != NULL) {
        (void)fclose(file->stream);
    }
    free(file);
}

apta_status_t APTA_CALL apta_file_write_atomic(
    const char *path,
    const void *data,
    size_t size)
{
    static const char suffix[] = ".tmp.XXXXXX";
    size_t path_size;
    char *temporary_path;
    int descriptor;
    FILE *stream;
    size_t written;
    int failed = 0;

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
    stream = fdopen(descriptor, "wb");
    if (stream == NULL) {
        (void)close(descriptor);
        (void)unlink(temporary_path);
        free(temporary_path);
        return APTA_ERROR_SOURCE;
    }

    written = size == 0u ? 0u : fwrite(data, 1u, size, stream);
    if (written != size || fflush(stream) != 0 || fsync(descriptor) != 0) {
        failed = 1;
    }
    if (fclose(stream) != 0) {
        failed = 1;
    }
    if (failed != 0) {
        (void)unlink(temporary_path);
        free(temporary_path);
        return APTA_ERROR_SOURCE;
    }
    if (rename(temporary_path, path) != 0) {
        (void)unlink(temporary_path);
        free(temporary_path);
        return APTA_ERROR_SOURCE;
    }
    free(temporary_path);
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_posix_file_open_read(
    const char *path,
    apta_posix_file_t **file_out)
{
    apta_file_t *file = NULL;
    apta_status_t status;

    if (file_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *file_out = NULL;
    status = apta_file_open_read(path, &file);
    if (status >= 0) {
        *file_out = (apta_posix_file_t *)file;
    }
    return status;
}

apta_status_t APTA_CALL apta_posix_file_get_size(
    const apta_posix_file_t *file,
    uint64_t *size_out)
{
    return apta_file_get_size((const apta_file_t *)file, size_out);
}

apta_status_t APTA_CALL apta_posix_file_read_at(
    apta_posix_file_t *file,
    uint64_t offset,
    void *buffer,
    size_t requested_bytes,
    size_t *read_bytes_out)
{
    return apta_file_read_at(
        (apta_file_t *)file,
        offset,
        buffer,
        requested_bytes,
        read_bytes_out);
}

void APTA_CALL apta_posix_file_close(apta_posix_file_t *file)
{
    apta_file_close((apta_file_t *)file);
}
