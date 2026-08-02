// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_DESKTOP_POSIX_FILE_H
#define APTA_DESKTOP_POSIX_FILE_H

#include <apta/desktop/apta_file.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compatibility API for applications written against the Stage S5 adapter. */
typedef struct apta_posix_file apta_posix_file_t;

APTA_API apta_status_t APTA_CALL
apta_posix_file_open_read(
    const char *path,
    apta_posix_file_t **file_out);

APTA_API apta_status_t APTA_CALL
apta_posix_file_get_size(
    const apta_posix_file_t *file,
    uint64_t *size_out);

APTA_API apta_status_t APTA_CALL
apta_posix_file_read_at(
    apta_posix_file_t *file,
    uint64_t offset,
    void *buffer,
    size_t requested_bytes,
    size_t *read_bytes_out);

APTA_API void APTA_CALL
apta_posix_file_close(apta_posix_file_t *file);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_DESKTOP_POSIX_FILE_H */
