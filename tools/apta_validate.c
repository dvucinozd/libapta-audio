// SPDX-License-Identifier: Apache-2.0
#include "apta_tool_common.h"

#include <stdio.h>
#include <string.h>

static void print_usage(FILE *stream)
{
    fputs(
        "Usage: apta-validate INPUT.apta [--strict] [--quiet]\n"
        "\n"
        "Options:\n"
        "  --strict   Enforce strict reserved-field validation\n"
        "  --quiet    Produce no success output\n"
        "  --help\n"
        "  --version\n",
        stream);
}

int main(int argc, char **argv)
{
    const char *path;
    int strict = 0;
    int quiet = 0;
    int index;
    apta_tool_buffer_t file = {NULL, 0u};
    apta_context_config_t context_config;
    apta_parse_options_t parse_options;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_status_t status;
    int exit_code = 1;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("apta-validate %u.%u.%u\n",
               APTA_VERSION_MAJOR, APTA_VERSION_MINOR, APTA_VERSION_PATCH);
        return 0;
    }
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }
    path = argv[1];
    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--strict") == 0) {
            strict = 1;
        } else if (strcmp(argv[index], "--quiet") == 0) {
            quiet = 1;
        } else {
            fprintf(stderr, "apta-validate: unknown option: %s\n", argv[index]);
            return 2;
        }
    }

    status = apta_tool_read_file(
        path, APTA_TOOL_DEFAULT_MAX_FILE_BYTES, &file);
    if (status < 0) {
        if (!quiet) {
            fprintf(stderr, "apta-validate: cannot read %s: %s\n",
                    path, apta_tool_status_name(status));
        }
        goto cleanup;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_DETAIL |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE |
        APTA_FEATURE_GRID_LOCKING;
    status = apta_context_create(&context_config, &context);
    if (status < 0) {
        if (!quiet) {
            fprintf(stderr, "apta-validate: context creation failed: %s\n",
                    apta_tool_status_name(status));
        }
        goto cleanup;
    }

    apta_parse_options_init(&parse_options);
    parse_options.maximum_file_bytes = APTA_TOOL_DEFAULT_MAX_FILE_BYTES;
    if (strict) {
        parse_options.flags = APTA_PARSE_STRICT;
    }
    status = apta_result_parse(
        context, &parse_options, file.data, file.size, &result);
    if (status < 0) {
        if (!quiet) {
            fprintf(stderr, "apta-validate: invalid %s: %s (%d)\n",
                    path, apta_tool_status_name(status), (int)status);
        }
        goto cleanup;
    }

    if (!quiet) {
        printf("valid: %s%s\n", path, strict ? " (strict)" : "");
    }
    exit_code = 0;

cleanup:
    if (result != NULL) {
        apta_result_release(result);
    }
    if (context != NULL) {
        (void)apta_context_destroy(context);
    }
    apta_tool_buffer_release(&file);
    return exit_code;
}
