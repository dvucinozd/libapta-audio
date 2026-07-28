// SPDX-License-Identifier: Apache-2.0
#include <apta/desktop/apta_posix_file.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

struct apta_posix_file {
    FILE *stream;
    uint64_t size;
};

apta_status_t APTA_CALL apta_posix_file_open_read(
    const char *path,
    apta_posix_file_t **file_out)
{
    apta_posix_file_t *file;
    off_t end_offset;

    if (file_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *file_out = NULL;
    if (path == NULL || path[0] == '\0') {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    file = (apta_posix_file_t *)calloc(1u, sizeof(*file));
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

apta_status_t APTA_CALL apta_posix_file_get_size(
    const apta_posix_file_t *file,
    uint64_t *size_out)
{
    if (file == NULL || size_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *size_out = file->size;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_posix_file_read_at(
    apta_posix_file_t *file,
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

void APTA_CALL apta_posix_file_close(apta_posix_file_t *file)
{
    if (file == NULL) {
        return;
    }
    if (file->stream != NULL) {
        (void)fclose(file->stream);
    }
    free(file);
}
