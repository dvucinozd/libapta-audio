// SPDX-License-Identifier: Apache-2.0
#include "apta_tool_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <apta/desktop/apta_decoder.h>

#define APTA_ANALYZE_MAX_ITERATIONS 1000000u
#define APTA_ANALYZE_MAX_OUTPUT_BYTES UINT64_C(1073741824)

static void print_usage(FILE *stream)
{
    fputs(
        "Usage: apta-analyze INPUT.wav --output OUTPUT.apta [options]\n"
        "\n"
        "Options:\n"
        "  --profile waveform|performance\n"
        "  --features waveform,detail,3band,bpm,beatgrid,global,dynamic,locking,key,meter,all\n"
        "  --help\n"
        "  --version\n",
        stream);
}

static int fail_status(const char *operation, apta_status_t status)
{
    fprintf(stderr, "apta-analyze: %s: %s (%d)\n",
            operation, apta_tool_status_name(status), (int)status);
    return 1;
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_CONFIDENCE;
    apta_decoder_t decoder;
    apta_decoder_info_t decoder_info;
    apta_pcm_source_t source;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_metadata_t metadata;
    apta_work_budget_t budget;
    apta_serialize_options_t serialize_options;
    apta_status_t status;
    uint32_t iteration;
    uint32_t would_block_count = 0u;
    uint64_t required_size = 0u;
    uint8_t *serialized = NULL;
    size_t written = 0u;
    size_t input_path_size;
    int index;
    int exit_code = 1;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("apta-analyze %u.%u.%u\n",
               APTA_VERSION_MAJOR, APTA_VERSION_MINOR, APTA_VERSION_PATCH);
        return 0;
    }
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }

    input_path = argv[1];
    for (index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--output") == 0) {
            if (++index >= argc || output_path != NULL) {
                print_usage(stderr);
                return 2;
            }
            output_path = argv[index];
        } else if (strcmp(argv[index], "--profile") == 0) {
            if (++index >= argc) {
                print_usage(stderr);
                return 2;
            }
            if (strcmp(argv[index], "waveform") == 0) {
                features = APTA_FEATURE_WAVEFORM_OVERVIEW;
            } else if (strcmp(argv[index], "performance") == 0) {
                features = APTA_FEATURE_WAVEFORM_OVERVIEW |
                           APTA_FEATURE_BPM |
                           APTA_FEATURE_LOCAL_BEATGRID |
                           APTA_FEATURE_CONFIDENCE;
            } else {
                fprintf(stderr, "apta-analyze: unknown profile: %s\n", argv[index]);
                return 2;
            }
        } else if (strcmp(argv[index], "--features") == 0) {
            if (++index >= argc ||
                apta_tool_parse_feature_list(argv[index], &features) < 0) {
                fprintf(stderr, "apta-analyze: invalid feature list\n");
                return 2;
            }
        } else if (strcmp(argv[index], "--help") == 0 ||
                   strcmp(argv[index], "--version") == 0) {
            fprintf(stderr, "apta-analyze: --help/--version must be used alone\n");
            return 2;
        } else {
            fprintf(stderr, "apta-analyze: unknown option: %s\n", argv[index]);
            return 2;
        }
    }
    if (output_path == NULL || output_path[0] == '\0' || input_path[0] == '\0') {
        print_usage(stderr);
        return 2;
    }

    apta_decoder_init(&decoder);
    apta_decoder_info_init(&decoder_info);
    status = apta_wav_decoder_open_path(input_path, &decoder, &decoder_info);
    if (status < 0) {
        return fail_status("cannot open input WAV", status);
    }
    apta_pcm_source_init(&source);
    status = apta_decoder_make_pcm_source(&decoder, &source);
    if (status < 0) {
        exit_code = fail_status("cannot create PCM source", status);
        goto cleanup;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = features;
    status = apta_context_create(&context_config, &context);
    if (status < 0) {
        exit_code = fail_status("cannot create context", status);
        goto cleanup;
    }

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = decoder_info.sample_rate;
    session_config.channel_count = decoder_info.channel_count;
    session_config.sample_format = decoder_info.sample_format;
    session_config.channel_layout = decoder_info.channel_layout;
    session_config.total_frames = decoder_info.total_frames;
    session_config.requested_features = features;
    status = apta_session_create(context, &session_config, &session);
    if (status < 0) {
        exit_code = fail_status("cannot create session", status);
        goto cleanup;
    }

    input_path_size = strlen(input_path);
    apta_metadata_init(&metadata);
    metadata.flags =
        APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT |
        APTA_METADATA_FLAG_PRODUCER_VERSION_PRESENT |
        APTA_METADATA_FLAG_BACKEND_NAME_PRESENT |
        APTA_METADATA_FLAG_BACKEND_VERSION_PRESENT |
        APTA_METADATA_FLAG_CREATION_TIME_PRESENT;
    metadata.producer_name.data = "apta-analyze";
    metadata.producer_name.size = 12u;
    metadata.producer_version_string.data = "0.1.0";
    metadata.producer_version_string.size = 5u;
    metadata.backend_name.data = "wav-reference";
    metadata.backend_name.size = 13u;
    metadata.backend_version.data = "1";
    metadata.backend_version.size = 1u;
    metadata.creation_unix_time = (uint64_t)time(NULL);
    if (input_path_size <= APTA_METADATA_MAX_SOURCE_ID_BYTES) {
        metadata.application_source_id_kind = APTA_METADATA_SOURCE_ID_TEXT;
        metadata.application_source_id.data = (const uint8_t *)input_path;
        metadata.application_source_id.size = (uint32_t)input_path_size;
    }
    status = apta_session_set_metadata(session, &metadata);
    if (status < 0) {
        exit_code = fail_status("cannot set metadata", status);
        goto cleanup;
    }
    status = apta_session_set_source(session, &source);
    if (status < 0) {
        exit_code = fail_status("cannot attach decoder source", status);
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 4096u;
    budget.maximum_steps = 64u;
    status = APTA_STATUS_MORE_WORK;
    for (iteration = 0u; iteration < APTA_ANALYZE_MAX_ITERATIONS; ++iteration) {
        status = apta_session_process(session, &budget, NULL);
        if (status == APTA_STATUS_END_OF_INPUT) {
            break;
        }
        if (status == APTA_STATUS_WOULD_BLOCK) {
            if (++would_block_count > 4u) {
                exit_code = fail_status("decoder unexpectedly blocked", status);
                goto cleanup;
            }
            continue;
        }
        would_block_count = 0u;
        if (status < 0) {
            exit_code = fail_status("analysis failed", status);
            goto cleanup;
        }
    }
    if (status != APTA_STATUS_END_OF_INPUT) {
        fprintf(stderr, "apta-analyze: analysis iteration limit exceeded\n");
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        fprintf(stderr, "apta-analyze: no final result available\n");
        goto cleanup;
    }
    apta_serialize_options_init(&serialize_options);
    serialize_options.flags = APTA_SERIALIZE_CANONICAL;
    serialize_options.maximum_output_bytes = APTA_ANALYZE_MAX_OUTPUT_BYTES;
    status = apta_result_query_serialized_size(
        result, &serialize_options, &required_size);
    if (status < 0 || required_size == 0u || required_size > SIZE_MAX) {
        exit_code = fail_status("cannot measure output", status < 0 ? status : APTA_ERROR_LIMIT_EXCEEDED);
        goto cleanup;
    }
    serialized = (uint8_t *)malloc((size_t)required_size);
    if (serialized == NULL) {
        exit_code = fail_status("cannot allocate output", APTA_ERROR_OUT_OF_MEMORY);
        goto cleanup;
    }
    status = apta_result_serialize(
        result,
        &serialize_options,
        serialized,
        (size_t)required_size,
        &written);
    if (status < 0 || written != (size_t)required_size) {
        exit_code = fail_status("cannot serialize output", status < 0 ? status : APTA_ERROR_INTERNAL);
        goto cleanup;
    }
    status = apta_tool_write_file_atomic(output_path, serialized, written);
    if (status < 0) {
        exit_code = fail_status("cannot write output", status);
        goto cleanup;
    }

    printf("analyzed %s -> %s\nfeatures: ", input_path, output_path);
    apta_tool_print_feature_list(stdout, features);
    printf("\nframes: %llu\nsample-rate: %u\n",
           (unsigned long long)decoder_info.total_frames,
           decoder_info.sample_rate);
    exit_code = 0;

cleanup:
    free(serialized);
    if (result != NULL) {
        apta_result_release(result);
    }
    if (session != NULL) {
        (void)apta_session_destroy(session);
    }
    apta_decoder_close(&decoder);
    if (context != NULL && apta_context_destroy(context) < 0 && exit_code == 0) {
        exit_code = 1;
    }
    return exit_code;
}
