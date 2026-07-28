// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_METADATA_H
#define APTA_METADATA_H

#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APTA_METADATA_SOURCE_ID_NONE  0u
#define APTA_METADATA_SOURCE_ID_TEXT  1u
#define APTA_METADATA_SOURCE_ID_BYTES 2u

#define APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT    (1u << 0)
#define APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT (1u << 1)
#define APTA_METADATA_FLAG_BACKEND_NAME_PRESENT     (1u << 2)
#define APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT  (1u << 3)
#define APTA_METADATA_FLAG_CREATION_TIME_PRESENT    (1u << 4)
#define APTA_METADATA_FLAG_COMMENTS_PRESENT         (1u << 5)

#define APTA_METADATA_MAX_PRODUCER_NAME_BYTES    255u
#define APTA_METADATA_MAX_VERSION_STRING_BYTES   127u
#define APTA_METADATA_MAX_BACKEND_NAME_BYTES     255u
#define APTA_METADATA_MAX_SOURCE_ID_BYTES        1024u
#define APTA_METADATA_MAX_COMMENTS_BYTES         4096u
#define APTA_METADATA_MAX_TOTAL_BYTES            8192u

typedef struct {
    const char *data;
    uint32_t size;
} apta_utf8_view_t;

typedef struct {
    const uint8_t *data;
    uint32_t size;
} apta_bytes_view_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_utf8_view_t producer_name;
    apta_utf8_view_t producer_version_string;
    apta_utf8_view_t backend_name;
    apta_utf8_view_t backend_version;

    uint64_t creation_unix_time;
    apta_bytes_view_t application_source_id;
    apta_utf8_view_t comments;

    uint32_t application_source_id_kind;
    uint32_t flags;
    uint32_t reserved32[4];
    uint64_t reserved64[2];
} apta_metadata_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_utf8_view_t producer_name;
    apta_utf8_view_t producer_version_string;
    apta_utf8_view_t backend_name;
    apta_utf8_view_t backend_version;

    uint64_t creation_unix_time;
    apta_bytes_view_t application_source_id;
    apta_utf8_view_t comments;

    uint32_t application_source_id_kind;
    uint32_t flags;
    uint32_t reserved32[4];
    uint64_t reserved64[2];
} apta_metadata_view_t;

APTA_API void APTA_CALL
apta_metadata_init(apta_metadata_t *metadata);

APTA_API void APTA_CALL
apta_metadata_view_init(apta_metadata_view_t *view);

APTA_API apta_status_t APTA_CALL
apta_session_set_metadata(
    apta_session_t *session,
    const apta_metadata_t *metadata);

APTA_API apta_status_t APTA_CALL
apta_result_get_metadata(
    const apta_result_t *result,
    apta_metadata_view_t *view_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_METADATA_H */
