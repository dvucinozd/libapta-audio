// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_DESKTOP_FILE_H
#define APTA_DESKTOP_FILE_H

#include <stddef.h>
#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native desktop file adapter. Paths are UTF-8 on every supported platform.
 * The opaque object is read-only; writes use a sibling temporary file and an
 * atomic replacement so a consumer never observes a partial `.apta` file.
 */
typedef struct apta_file apta_file_t;

APTA_API apta_status_t APTA_CALL
apta_file_open_read(
    const char *path,
    apta_file_t **file_out);

APTA_API apta_status_t APTA_CALL
apta_file_get_size(
    const apta_file_t *file,
    uint64_t *size_out);

APTA_API apta_status_t APTA_CALL
apta_file_read_at(
    apta_file_t *file,
    uint64_t offset,
    void *buffer,
    size_t requested_bytes,
    size_t *read_bytes_out);

APTA_API void APTA_CALL
apta_file_close(apta_file_t *file);

APTA_API apta_status_t APTA_CALL
apta_file_write_atomic(
    const char *path,
    const void *data,
    size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_DESKTOP_FILE_H */
