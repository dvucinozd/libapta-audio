// SPDX-License-Identifier: Apache-2.0
/* apta-key-trace: development-only octave-resolved key evidence dump. */
#include "apta_tool_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/key/apta_key_internal.h"
#include <apta/desktop/apta_decoder.h>

#define APTA_KEY_TRACE_MAX_ITERATIONS 1000000u

static void print_usage(FILE *stream)
{
    fputs(
        "Usage: apta-key-trace INPUT.wav --output TRACE.json\n"
        "\n"
        "Development diagnostic only: requires a key-trace-enabled build.\n",
        stream);
}

static int fail_status(const char *operation, apta_status_t status)
{
    fprintf(stderr, "apta-key-trace: %s: %s (%d)\n",
            operation, apta_tool_status_name(status), (int)status);
    return 1;
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_MUSICAL_KEY;
    apta_decoder_t decoder;
    apta_decoder_info_t decoder_info;
    apta_pcm_source_t source;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    const float *spectral_profile = NULL;
    const float *chroma = NULL;
    apta_key_view_t key_view;
    apta_work_budget_t budget;
    apta_status_t status;
    FILE *trace = NULL;
    uint32_t bin_count = 0u;
    uint32_t completed_windows = 0u;
    uint32_t would_block_count = 0u;
    uint32_t iteration;
    uint32_t index;
    int exit_code = 1;
    int argument;

    for (argument = 1; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--output") == 0) {
            if (++argument >= argc || output_path != NULL) {
                print_usage(stderr);
                return 2;
            }
            output_path = argv[argument];
        } else if (strcmp(argv[argument], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (input_path == NULL) {
            input_path = argv[argument];
        } else {
            print_usage(stderr);
            return 2;
        }
    }
    if (input_path == NULL || output_path == NULL || output_path[0] == '\0') {
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
    status = apta_session_set_source(session, &source);
    if (status < 0) {
        exit_code = fail_status("cannot attach decoder source", status);
        goto cleanup;
    }

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 4096u;
    budget.maximum_steps = 64u;
    status = APTA_STATUS_MORE_WORK;
    for (iteration = 0u;
         iteration < APTA_KEY_TRACE_MAX_ITERATIONS;
         ++iteration) {
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
        fprintf(stderr, "apta-key-trace: analysis iteration limit exceeded\n");
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        fprintf(stderr, "apta-key-trace: no final result available\n");
        goto cleanup;
    }
    apta_key_view_init(&key_view);
    status = apta_result_get_key(result, NULL, &key_view);
    if (status < 0) {
        exit_code = fail_status("no key result", status);
        goto cleanup;
    }
    apta_internal_key_trace_get(
        session,
        &spectral_profile,
        &bin_count,
        &chroma,
        &completed_windows);
    if (spectral_profile == NULL || chroma == NULL || bin_count == 0u) {
        fprintf(stderr, "apta-key-trace: no internal key evidence\n");
        goto cleanup;
    }

    trace = fopen(output_path, "w");
    if (trace == NULL) {
        fprintf(stderr, "apta-key-trace: cannot open trace output\n");
        goto cleanup;
    }
    fprintf(trace,
            "{\"frames\":%llu,\"sample_rate\":%u,"
            "\"completed_windows\":%u,"
            "\"key\":{\"tonic\":%u,\"mode\":%u,\"confidence\":%u},"
            "\"spectral_profile\":[",
            (unsigned long long)decoder_info.total_frames,
            decoder_info.sample_rate,
            completed_windows,
            (unsigned int)key_view.tonic,
            (unsigned int)key_view.mode,
            (unsigned int)key_view.confidence);
    for (index = 0u; index < bin_count; ++index) {
        fprintf(trace,
                "%s%.9g",
                index > 0u ? "," : "",
                (double)spectral_profile[index]);
    }
    fputs("],\"chroma\":[", trace);
    for (index = 0u; index < APTA_INTERNAL_KEY_PITCH_CLASSES; ++index) {
        fprintf(trace,
                "%s%.9g",
                index > 0u ? "," : "",
                (double)chroma[index]);
    }
    fputs("]}\n", trace);
    if (fclose(trace) != 0) {
        trace = NULL;
        exit_code =
            fail_status("cannot write trace output", APTA_ERROR_INTERNAL);
        goto cleanup;
    }
    trace = NULL;
    exit_code = 0;

cleanup:
    if (trace != NULL) {
        (void)fclose(trace);
    }
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
