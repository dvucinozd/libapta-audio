// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_TOOL_COMMON_H
#define APTA_TOOL_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define APTA_TOOL_DEFAULT_MAX_FILE_BYTES UINT64_C(268435456)
#define APTA_VERSION_MAJOR APTA_API_VERSION_MAJOR
#define APTA_VERSION_MINOR APTA_API_VERSION_MINOR
#define APTA_VERSION_PATCH APTA_API_VERSION_PATCH

typedef struct {
    uint8_t *data;
    size_t size;
} apta_tool_buffer_t;

const char *apta_tool_status_name(apta_status_t status);
const char *apta_tool_feature_state_name(apta_feature_state_t state);
const char *apta_tool_session_state_name(apta_session_state_t state);
const char *apta_tool_grid_revision_state_name(
    apta_grid_revision_state_t state);
const char *apta_tool_diagnostic_severity_name(
    apta_diagnostic_severity_t severity);

apta_status_t apta_tool_read_file(
    const char *path,
    uint64_t maximum_bytes,
    apta_tool_buffer_t *buffer_out);

void apta_tool_buffer_release(apta_tool_buffer_t *buffer);

apta_status_t apta_tool_write_file_atomic(
    const char *path,
    const void *data,
    size_t size);

/*
 * Every feature a context can be asked for, which is what `--features all`
 * expands to. Exposed so a test can assert the two agree: spelling the set out
 * in the parser is how the global grid, dynamic tempo, the three-band overview
 * and grid locking each came to have no CLI path at one time or another.
 */
apta_feature_mask_t apta_tool_all_features(void);

/*
 * Every `.apta` section code the tools can name, NULL-terminated.
 *
 * Exposed for the same reason as the feature set: `apta-inspect` kept a
 * hand-written list of sections it would accept and display, and a section the
 * writer could emit but the list did not mention was invisible. GGRD and REVN
 * were both in that position. `apta.tools.sections_all` builds a container that
 * carries every section and requires each one to appear here.
 */
extern const char *const apta_tool_section_codes[];

/* Whether `code` is a section the tools know how to name. */
int apta_tool_section_is_known(const char *code);

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
