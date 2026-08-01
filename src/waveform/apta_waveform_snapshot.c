// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

static apta_source_frame_t apta_min_frame(
    apta_source_frame_t left,
    apta_source_frame_t right)
{
    return left < right ? left : right;
}

static double apta_round_ties_even(double value)
{
    double lower;
    double fraction;

    lower = floor(value);
    fraction = value - lower;

    if (fraction < 0.5) {
        return lower;
    }
    if (fraction > 0.5) {
        return lower + 1.0;
    }

    return fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0;
}

static int16_t apta_quantize_peak(float value)
{
    double rounded;

    if (value <= -1.0f) {
        return INT16_MIN;
    }
    if (value >= 1.0f) {
        return INT16_MAX;
    }

    rounded = apta_round_ties_even((double)value * 32767.0);
    if (rounded < (double)INT16_MIN) {
        rounded = (double)INT16_MIN;
    }
    if (rounded > (double)INT16_MAX) {
        rounded = (double)INT16_MAX;
    }
    return (int16_t)rounded;
}

/* A3: sum_squares is an integer accumulator scaled by
 * APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE squared. The arithmetic here stays
 * double: it runs once per column rather than per sample, and the
 * round-half-to-even step feeds canonical serialization. */
/* A4: the overview's own confidence, independent of the tempo engine. It
 * reports coverage completeness: how much of the expected column range has
 * actually been measured. APTA_CONFIDENCE_UNKNOWN when the track length is not
 * yet known, or when the host did not ask for confidence. */
static apta_confidence_value_t apta_overview_confidence(
    const apta_session_t *session,
    uint32_t complete_columns)
{
    uint64_t total_frames;
    uint64_t expected;

    if ((session->config.requested_features & APTA_FEATURE_CONFIDENCE) == 0u) {
        return APTA_CONFIDENCE_UNKNOWN;
    }

    total_frames = session->end_of_input_signalled
                       ? session->final_end_frame
                       : session->config.total_frames;
    if (total_frames == 0u || session->overview_frames_per_column == 0u) {
        return APTA_CONFIDENCE_UNKNOWN;
    }

    expected = (total_frames +
                (uint64_t)session->overview_frames_per_column - 1u) /
               (uint64_t)session->overview_frames_per_column;
    if (expected == 0u) {
        return APTA_CONFIDENCE_UNKNOWN;
    }
    if ((uint64_t)complete_columns >= expected) {
        return (apta_confidence_value_t)APTA_CONFIDENCE_MAX;
    }
    return (apta_confidence_value_t)(
        ((uint64_t)complete_columns * (uint64_t)APTA_CONFIDENCE_MAX) /
        expected);
}

static uint16_t apta_quantize_rms(uint64_t sum_squares, uint32_t sample_count)
{
    double rms;
    double rounded;

    if (sample_count == 0u) {
        return 0u;
    }

    rms = sqrt((double)sum_squares / (double)sample_count) /
          (double)APTA_INTERNAL_SQUARE_MAGNITUDE_SCALE;
    if (rms < 0.0) {
        rms = 0.0;
    }
    if (rms > 1.0) {
        rms = 1.0;
    }

    rounded = apta_round_ties_even(rms * 65535.0);
    if (rounded < 0.0) {
        rounded = 0.0;
    }
    if (rounded > 65535.0) {
        rounded = 65535.0;
    }
    return (uint16_t)rounded;
}

apta_status_t apta_internal_waveform_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    uint32_t complete_count;
    uint32_t span_count;
    uint32_t index;
    uint32_t output_index;
    uint32_t span_index;
    uint32_t previous_column;
    uint32_t span_output_start;
    int have_previous;
    int full_coverage;
    size_t column_bytes;
    size_t span_bytes;

    if ((session->config.requested_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u ||
        session->overview_complete_count == 0u) {
        return APTA_STATUS_OK;
    }

    complete_count = session->overview_complete_count;
    span_count = 0u;
    have_previous = 0;
    previous_column = 0u;

    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        const apta_internal_waveform_accumulator_t *accumulator =
            &session->overview_accumulators[index];

        if (!accumulator->complete) {
            continue;
        }
        if (!have_previous ||
            accumulator->column_index != previous_column + 1u) {
            span_count += 1u;
        }
        previous_column = accumulator->column_index;
        have_previous = 1;
    }

    if (!apta_internal_size_array_fits(
            0u,
            complete_count,
            sizeof(*result->overview_columns)) ||
        !apta_internal_size_array_fits(
            0u,
            span_count,
            sizeof(*result->overview_spans))) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    column_bytes = (size_t)complete_count * sizeof(*result->overview_columns);
    span_bytes = (size_t)span_count * sizeof(*result->overview_spans);

    result->overview_columns =
        (apta_waveform_column_t *)apta_internal_context_allocate(
            session->context,
            column_bytes,
            alignof(apta_waveform_column_t),
            APTA_MEMORY_PERSISTENT);
    if (result->overview_columns == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    result->overview_spans =
        (apta_waveform_span_t *)apta_internal_context_allocate(
            session->context,
            span_bytes,
            alignof(apta_waveform_span_t),
            APTA_MEMORY_PERSISTENT);
    if (result->overview_spans == NULL) {
        apta_internal_context_deallocate(
            session->context,
            result->overview_columns);
        result->overview_columns = NULL;
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(result->overview_columns, 0, column_bytes);
    memset(result->overview_spans, 0, span_bytes);

    output_index = 0u;
    span_index = 0u;
    have_previous = 0;
    previous_column = 0u;
    span_output_start = 0u;

    for (index = 0u; index < session->overview_accumulator_count; ++index) {
        const apta_internal_waveform_accumulator_t *accumulator =
            &session->overview_accumulators[index];
        apta_waveform_column_t *column;

        if (!accumulator->complete) {
            continue;
        }

        if (!have_previous ||
            accumulator->column_index != previous_column + 1u) {
            if (have_previous) {
                apta_waveform_span_t *previous_span =
                    &result->overview_spans[span_index - 1u];
                apta_source_frame_t end_frame =
                    ((apta_source_frame_t)previous_column + 1u) *
                    session->overview_frames_per_column;
                if (session->end_of_input_signalled) {
                    end_frame = apta_min_frame(end_frame, session->final_end_frame);
                }
                previous_span->source_range.end_frame = end_frame;
                previous_span->column_count = output_index - span_output_start;
            }

            span_output_start = output_index;
            result->overview_spans[span_index].source_range.struct_size =
                (uint32_t)sizeof(result->overview_spans[span_index].source_range);
            result->overview_spans[span_index].source_range.api_version =
                APTA_API_VERSION;
            result->overview_spans[span_index].source_range.first_frame =
                (apta_source_frame_t)accumulator->column_index *
                session->overview_frames_per_column;
            result->overview_spans[span_index].first_column_index =
                accumulator->column_index;
            result->overview_spans[span_index].columns =
                &result->overview_columns[output_index];
            span_index += 1u;
        }

        column = &result->overview_columns[output_index];
        column->minimum = apta_quantize_peak(accumulator->minimum);
        column->maximum = apta_quantize_peak(accumulator->maximum);
        column->rms = apta_quantize_rms(
            accumulator->sum_squares,
            accumulator->sample_count);
        column->flags = APTA_WAVEFORM_COLUMN_VALID;
        if (accumulator->clipped) {
            column->flags |= APTA_WAVEFORM_COLUMN_CLIPPED;
        }

        output_index += 1u;
        previous_column = accumulator->column_index;
        have_previous = 1;
    }

    if (have_previous) {
        apta_waveform_span_t *last_span =
            &result->overview_spans[span_index - 1u];
        apta_source_frame_t end_frame =
            ((apta_source_frame_t)previous_column + 1u) *
            session->overview_frames_per_column;
        if (session->end_of_input_signalled) {
            end_frame = apta_min_frame(end_frame, session->final_end_frame);
        }
        last_span->source_range.end_frame = end_frame;
        last_span->column_count = output_index - span_output_start;
    }

    memset(&result->overview, 0, sizeof(result->overview));
    result->overview.struct_size = (uint32_t)sizeof(result->overview);
    result->overview.api_version = APTA_API_VERSION;
    result->overview.level.struct_size = (uint32_t)sizeof(result->overview.level);
    result->overview.level.api_version = APTA_API_VERSION;
    result->overview.level.level_id = 0u;
    result->overview.level.frames_per_column =
        session->overview_frames_per_column;
    result->overview.level.origin_frame = 0u;
    result->overview.confidence =
        apta_overview_confidence(session, output_index);
    result->overview.span_count = span_count;
    result->overview.spans = result->overview_spans;

    full_coverage = session->end_of_input_signalled &&
                    span_count == 1u &&
                    result->overview_spans[0].source_range.first_frame == 0u &&
                    result->overview_spans[0].source_range.end_frame ==
                        session->final_end_frame;

    result->overview.state =
        full_coverage &&
                atomic_load_explicit(&session->state, memory_order_acquire) ==
                    APTA_SESSION_COMPLETED
            ? APTA_FEATURE_FINAL
            : (full_coverage ? APTA_FEATURE_STABLE : APTA_FEATURE_PARTIAL);

    result->info.available_features |= APTA_FEATURE_WAVEFORM_OVERVIEW;
    /* A4: the overview reports confidence on its own, without S4. */
    if ((session->config.requested_features & APTA_FEATURE_CONFIDENCE) != 0u) {
        result->info.available_features |= APTA_FEATURE_CONFIDENCE;
    }
    return APTA_STATUS_OK;
}

void apta_internal_waveform_cleanup_session(apta_session_t *session)
{
    apta_internal_pcm_node_t *node;

    if (session == NULL) {
        return;
    }

    node = session->pcm_head;
    while (node != NULL) {
        apta_internal_pcm_node_t *next = node->next;
        apta_internal_context_deallocate(session->context, node);
        node = next;
    }

    apta_internal_context_deallocate(session->context, session->accepted_ranges);
    apta_internal_context_deallocate(
        session->context,
        session->overview_accumulators);

    session->pcm_head = NULL;
    session->pcm_tail = NULL;
    session->accepted_ranges = NULL;
    session->overview_accumulators = NULL;
}

void apta_internal_waveform_cleanup_result(apta_result_t *result)
{
    if (result == NULL) {
        return;
    }

    apta_internal_context_deallocate(result->context, result->overview_spans);
    apta_internal_context_deallocate(result->context, result->overview_columns);
    result->overview_spans = NULL;
    result->overview_columns = NULL;
}
