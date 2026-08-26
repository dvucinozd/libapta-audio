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
#include "../../src/tempo/apta_s4_internal.h"
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

static void write_json_string(FILE *stream, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;

    fputc('"', stream);
    while (*cursor != 0u) {
        switch (*cursor) {
        case '"': fputs("\\\"", stream); break;
        case '\\': fputs("\\\\", stream); break;
        case '\b': fputs("\\b", stream); break;
        case '\f': fputs("\\f", stream); break;
        case '\n': fputs("\\n", stream); break;
        case '\r': fputs("\\r", stream); break;
        case '\t': fputs("\\t", stream); break;
        default:
            if (*cursor < 0x20u) {
                fprintf(stream, "\\u%04x", (unsigned int)*cursor);
            } else {
                fputc((int)*cursor, stream);
            }
            break;
        }
        ++cursor;
    }
    fputc('"', stream);
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    const apta_feature_mask_t features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_3BAND |
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
    const float *flux = NULL;
    const uint32_t *candidate_lags = NULL;
    const float *candidate_lag_offsets = NULL;
    uint32_t beat_count = 0u;
    uint32_t lag = 0u;
    uint32_t flux_count = 0u;
    uint32_t candidate_count = 0u;
    uint32_t selected_phase = 0u;
    uint64_t first_beat_bin = 0u;
    uint64_t evidence_first_bin = 0u;
    int meter_view_available = 0;
    int overview_available = 0;
    apta_meter_view_t meter_view;
    apta_waveform_overview_view_t overview;
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
    apta_internal_s4_trace_get(
        session,
        &flux,
        &flux_count,
        &evidence_first_bin,
        &candidate_lags,
        &candidate_lag_offsets,
        &candidate_count,
        &selected_phase);

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
        overview_available = 0u;
        apta_waveform_overview_view_init(&overview);
        if (apta_result_get_waveform_overview(result, 0u, &overview) ==
                APTA_STATUS_OK &&
            overview.span_count > 0u) {
            overview_available = 1u;
        }

        trace = fopen(output_path, "w");
        if (trace == NULL) {
            fprintf(stderr, "apta-meter-trace: cannot open trace output\n");
            goto cleanup;
        }
        fputs("{\"track\":", trace);
        write_json_string(trace, input_path);
        fprintf(trace,
                ",\"frames\":%llu,\"sample_rate\":%u,"
                "\"lattice\":{\"anchor_frame\":%llu,"
                "\"period_whole_frames\":%llu,"
                "\"period_fraction_q32\":%llu},"
                "\"meter\":{\"numerator\":%u,\"denominator\":%u,"
                "\"downbeat_ordinal\":%lld,\"confidence\":%u},"
                "\"lag_bins\":%u,\"first_beat_bin\":%llu,"
                "\"onset_evidence_first_bin\":%llu,"
                "\"selected_phase_bin\":%u,\"tempo_candidates\":[",
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
                (unsigned long long)first_beat_bin,
                (unsigned long long)evidence_first_bin,
                selected_phase);
        for (index = 0u; index < candidate_count; ++index) {
            fprintf(trace,
                    "%s{\"lag_bins\":%u,\"lag_offset_bins\":%.9g}",
                    index > 0u ? "," : "",
                    candidate_lags != NULL ? candidate_lags[index] : 0u,
                    candidate_lag_offsets != NULL
                        ? (double)candidate_lag_offsets[index]
                        : 0.0);
        }
        fputs("],\"onset_flux\":[", trace);
        for (index = 0u; index < flux_count; ++index) {
            fprintf(trace, "%s%.9g", index > 0u ? "," : "", (double)flux[index]);
        }
        fprintf(trace,
                "],\"overview_3band\":{\"frames_per_column\":%u,"
                "\"origin_frame\":%llu,\"spans\":[",
                overview_available ? overview.level.frames_per_column : 0u,
                (unsigned long long)(overview_available
                                         ? overview.level.origin_frame
                                         : 0u));
        if (overview_available) {
            uint32_t span_index;

            for (span_index = 0u;
                 span_index < overview.span_count;
                 ++span_index) {
                const apta_waveform_span_t *span = &overview.spans[span_index];
                uint32_t column_index;

                fprintf(trace,
                        "%s{\"first_column_index\":%u,\"bands\":[",
                        span_index > 0u ? "," : "",
                        span->first_column_index);
                for (column_index = 0u;
                     column_index < span->column_count;
                     ++column_index) {
                    const apta_waveform_column_t *column =
                        &span->columns[column_index];

                    fprintf(trace,
                            "%s%u,%u,%u",
                            column_index > 0u ? "," : "",
                            (unsigned int)column->low,
                            (unsigned int)column->mid,
                            (unsigned int)column->high);
                }
                fputs("]}", trace);
            }
        }
        fputs("]},\"beats\":[", trace);
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
