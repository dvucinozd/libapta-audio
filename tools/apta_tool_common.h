// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_TOOL_COMMON_H
#define APTA_TOOL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include <apta/apta.h>

#define APTA_TOOL_DEFAULT_MAX_FILE_BYTES UINT64_C(268435456)

typedef struct {
    uint8_t *data;
    size_t size;
} apta_tool_buffer_t;

const char *apta_tool_status_name(apta_status_t status);
const char *apta_tool_feature_state_name(apta_feature_state_t state);
const char *apta_tool_session_state_name(apta_session_state_t state);

apta_status_t apta_tool_read_file(
    const char *path,
    uint64_t maximum_bytes,
    apta_tool_buffer_t *buffer_out);

void apta_tool_buffer_release(apta_tool_buffer_t *buffer);

apta_status_t apta_tool_write_file_atomic(
    const char *path,
    const void *data,
    size_t size);

apta_status_t apta_tool_parse_feature_list(
    const char *text,
    apta_feature_mask_t *features_out);

void apta_tool_print_feature_list(
    FILE *stream,
    apta_feature_mask_t features);

void apta_tool_json_string(
    FILE *stream,
    const char *data,
    size_t size);

#endif /* APTA_TOOL_COMMON_H */
