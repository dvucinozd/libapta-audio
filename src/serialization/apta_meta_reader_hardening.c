// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <string.h>

APTA_API apta_status_t APTA_CALL apta_result_parse_meta_base(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out);

static int apta_meta_parse_options_are_valid(
    const apta_parse_options_t *options)
{
    uint32_t index;

    if (options == NULL) {
        return 1;
    }
    if (!apta_internal_validate_struct(
            options,
            sizeof(*options),
            options->struct_size,
            options->api_version) ||
        (options->flags & ~APTA_PARSE_STRICT) != 0u) {
        return 0;
    }
    for (index = 0u; index < 4u; ++index) {
        if (options->reserved64[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

apta_status_t APTA_CALL apta_result_parse(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out)
{
    apta_status_t status;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;

    if (context == NULL || buffer == NULL ||
        !apta_meta_parse_options_are_valid(options)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_result_parse_meta_base(
        context,
        options,
        buffer,
        buffer_size,
        result_out);
    if (status == APTA_ERROR_INVALID_ARGUMENT) {
        *result_out = NULL;
        return APTA_ERROR_CORRUPT_DATA;
    }
    return status;
}
