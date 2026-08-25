// SPDX-License-Identifier: Apache-2.0
/* apta-meter-trace: development diagnostic tool.
 *
 * Runs a real analyzer session (pull mode, WAV input) and emits one NDJSON
 * line describing the FINAL meter refresh: APTA's published lattice, the
 * exact sampling lag, and the actual internal per-beat broadband strength
 * series the meter stage scored. Built only with
 * -DAPTA_ENABLE_EXPERIMENTAL_METER_TRACE=ON; never shipped.
 */
#include "apta_tool_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../src/beatgrid/apta_meter_internal.h"
#include <apta/desktop/apta_decoder.h>

#define APTA_TRACE_MAX_ITERATIONS 1000000u

static void print_usage(FILE *stream)
{
    fputs(
        "Usage: apta-meter-trace INPUT.wav --output TRACE.ndjson\n"
        "\n"
        "Development diagnostic only: requires a trace-enabled library build.\n",
        stream);
}

static int fail_status(const char *operation, apta_status_t status)
{
    fprintf(stderr, "apta-meter-trace: %s: %s (%d)\n",
            operation, apta_tool_status_name(status), (int)status);
    return 1;
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_BPM |
        APTA_FEATURE_LOCAL_BEATGRID |
        APTA_FEATURE_METER_DOWNBEAT;
    apta_decoder_t decoder;
    apta_decoder_info_t decoder_info;
    apta_pcm_source_t source;
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_work_budget_t budget;
    FILE *trace = NULL;
    const float *broad = NULL;
    const float *accent = NULL;
    uint32_t beat_count = 0u;
    uint32_t lag = 0u;
    uint64_t first_beat_bin = 0u;
    int meter_view_available = 0;
    apta_meter_view_t meter_view;
    apta_status_t status;
    uint32_t iteration;
    uint32_t would_block_count = 0u;
    uint32_t index;
    int exit_code = 1;
    int index_arg;

    for (index_arg = 1; index_arg < argc; ++index_arg) {
        if (strcmp(argv[index_arg], "--output") == 0) {
            if (++index_arg >= argc || output_path != NULL) {
                print_usage(stderr);
                return 2;
            }
            output_path = argv[index_arg];
        } else if (strcmp(argv[index_arg], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (input_path == NULL) {
            input_path = argv[index_arg];
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
    for (iteration = 0u; iteration < APTA_TRACE_MAX_ITERATIONS; ++iteration) {
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
        fprintf(stderr, "apta-meter-trace: analysis iteration limit exceeded\n");
        goto cleanup;
    }

    result = apta_session_acquire_result(session);
    if (result == NULL) {
        fprintf(stderr, "apta-meter-trace: no final result available\n");
        goto cleanup;
    }

    apta_internal_meter_trace_get(
        session, &broad, &accent, &beat_count, &lag, &first_beat_bin);

    {
        const apta_frame_range_t full_range = {
            (uint32_t)sizeof(apta_frame_range_t),
            APTA_API_VERSION,
            0u,
            decoder_info.total_frames};
        apta_grid_view_t grid_view;
        apta_grid_view_init(&grid_view);
        status = apta_result_get_beatgrid(
            result, APTA_FEATURE_LOCAL_BEATGRID, &full_range, &grid_view);
        if (status < 0) {
            exit_code = fail_status("no local grid", status);
            goto cleanup;
        }
        meter_view_available = 0u;
        apta_meter_view_init(&meter_view);
        if (apta_result_get_meter(result, &full_range, &meter_view) ==
                APTA_STATUS_OK &&
            meter_view.segment_count > 0u) {
            meter_view_available = 1u;
        }

        trace = fopen(output_path, "w");
        if (trace == NULL) {
            fprintf(stderr, "apta-meter-trace: cannot open trace output\n");
            goto cleanup;
        }
        fprintf(trace,
                "{\"track\":\"%s\",\"frames\":%llu,\"sample_rate\":%u,"
                "\"lattice\":{\"anchor_frame\":%llu,"
                "\"period_whole_frames\":%llu,"
                "\"period_fraction_q32\":%llu},"
                "\"meter\":{\"numerator\":%u,\"denominator\":%u,"
                "\"downbeat_ordinal\":%lld,\"confidence\":%u},"
                "\"lag_bins\":%u,\"first_beat_bin\":%llu,\"beats\":[",
                input_path,
                (unsigned long long)decoder_info.total_frames,
                decoder_info.sample_rate,
                (unsigned long long)(grid_view.segment_count > 0u
                                         ? grid_view.segments[0]
                                               .anchor_position.whole_frame
                                         : 0u),
                (unsigned long long)(grid_view.segment_count > 0u
                                         ? grid_view.segments[0]
                                               .frames_per_beat.whole_frames
                                         : 0u),
                (unsigned long long)(grid_view.segment_count > 0u
                                         ? (unsigned long long)grid_view
                                               .segments[0].frames_per_beat
                                               .fraction_q32
                                         : 0u),
                (unsigned int)(meter_view_available
                                   ? meter_view.segments[0].numerator
                                   : 0u),
                (unsigned int)(meter_view_available
                                   ? meter_view.segments[0].denominator
                                   : 0u),
                (long long)(meter_view_available
                                ? (long long)meter_view.segments[0]
                                      .downbeat_ordinal
                                : 0ll),
                (unsigned int)(meter_view_available
                                   ? meter_view.segments[0].confidence
                                   : 0u),
                lag,
                (unsigned long long)first_beat_bin);
        for (index = 0u; index < beat_count; ++index) {
            fprintf(trace, "%s%.9g", index > 0u ? "," : "", (double)broad[index]);
        }
        fputs("]}\n", trace);
        if (fclose(trace) != 0) {
            trace = NULL;
            exit_code =
                fail_status("cannot write trace output", APTA_ERROR_INTERNAL);
            goto cleanup;
        }
        trace = NULL;
        printf("traced %s -> %s (%u beats)\n",
               input_path, output_path, beat_count);
        exit_code = 0;
    }

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
