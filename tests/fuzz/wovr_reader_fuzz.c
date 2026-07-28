// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>

#include <apta/apta.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    apta_context_config_t context_config;
    apta_parse_options_t parse_options;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    context_config.memory_limit_bytes = UINT64_C(2097152);

    if (apta_context_create(&context_config, &context) != APTA_STATUS_OK) {
        return 0;
    }

    apta_parse_options_init(&parse_options);
    parse_options.maximum_file_bytes = UINT64_C(1048576);
    parse_options.maximum_section_count = 64u;
    parse_options.maximum_overview_spans = 4096u;
    parse_options.maximum_waveform_columns = 65536u;
    parse_options.maximum_allocation_bytes = UINT64_C(1048576);

    (void)apta_result_parse(
        context,
        &parse_options,
        data,
        size,
        &result);

    if (result != NULL) {
        apta_result_release(result);
    }

    (void)apta_context_destroy(context);
    return 0;
}
