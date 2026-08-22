// SPDX-License-Identifier: Apache-2.0
#include "apta_meter_internal.h"

#include <math.h>
#include <stdalign.h>
#include <string.h>

#define APTA_METER_EPSILON 1e-12f

static float apta_meter_phase_score(
    const float *beat_strengths,
    uint32_t beat_count,
    uint32_t meter,
    uint32_t phase)
{
    float downbeat_sum = 0.0f;
    float other_sum = 0.0f;
    uint32_t downbeat_count = 0u;
    uint32_t other_count = 0u;
    uint32_t index;

    for (index = 0u; index < beat_count; ++index) {
        if ((index % meter) == phase) {
            downbeat_sum += beat_strengths[index];
            downbeat_count += 1u;
        } else {
            other_sum += beat_strengths[index];
            other_count += 1u;
        }
    }
    if (downbeat_count == 0u || other_count == 0u) {
        return 0.0f;
    }
    {
        const float downbeat_mean = downbeat_sum / (float)downbeat_count;
        const float other_mean = other_sum / (float)other_count;
        const float total = downbeat_mean + other_mean;

        return total > APTA_METER_EPSILON
                   ? (downbeat_mean - other_mean) / total
                   : 0.0f;
    }
}

static void apta_meter_best_phase(
    const float *beat_strengths,
    uint32_t beat_count,
    uint32_t meter,
    uint32_t *phase_out,
    float *score_out)
{
    uint32_t phase;
    uint32_t best_phase = 0u;
    float best_score = -1.0f;

    for (phase = 0u; phase < meter; ++phase) {
        const float score = apta_meter_phase_score(
            beat_strengths, beat_count, meter, phase);
        if (score > best_score) {
            best_score = score;
            best_phase = phase;
        }
    }
    *phase_out = best_phase;
    *score_out = best_score > 0.0f ? best_score : 0.0f;
}

apta_status_t apta_internal_meter_select(
    const float *beat_strengths,
    uint32_t beat_count,
    apta_internal_meter_selection_t *selection_out)
{
    uint32_t phase3;
    uint32_t phase4;
    float score3;
    float score4;
    float best;
    float runner_up;
    float separation;
    uint32_t confidence;

    if (beat_strengths == NULL || selection_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    memset(selection_out, 0, sizeof(*selection_out));
    if (beat_count < APTA_INTERNAL_METER_MIN_BEATS) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    apta_meter_best_phase(beat_strengths, beat_count, 3u, &phase3, &score3);
    apta_meter_best_phase(beat_strengths, beat_count, 4u, &phase4, &score4);

    if (score4 >= score3) {
        selection_out->numerator = 4u;
        selection_out->downbeat_phase = phase4;
        best = score4;
        runner_up = score3;
    } else {
        selection_out->numerator = 3u;
        selection_out->downbeat_phase = phase3;
        best = score3;
        runner_up = score4;
    }
    selection_out->denominator = 4u;
    selection_out->score = best;
    selection_out->runner_up_score = runner_up;

    separation = best > runner_up ? best - runner_up : 0.0f;
    confidence = 25u;
    confidence += (uint32_t)(best * 45.0f + 0.5f);
    confidence += (uint32_t)(separation * 30.0f + 0.5f);
    if (beat_count >= APTA_INTERNAL_METER_STABLE_BEATS) {
        confidence += 10u;
    }
    if (confidence > APTA_CONFIDENCE_MAX) {
        confidence = APTA_CONFIDENCE_MAX;
    }
    selection_out->confidence = (apta_confidence_value_t)confidence;
    return APTA_STATUS_OK;
}

static uint32_t apta_meter_lag_from_tempo(
    const apta_session_t *session,
    uint32_t tempo_millibpm)
{
    uint64_t numerator;
    uint64_t denominator;

    if (session == NULL || tempo_millibpm == 0u) {
        return 0u;
    }
    numerator = (uint64_t)session->config.source_sample_rate * UINT64_C(60000);
    denominator = (uint64_t)tempo_millibpm * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    return denominator != 0u
               ? (uint32_t)((numerator + denominator / 2u) / denominator)
               : 0u;
}

static float apta_meter_flux_near(
    const float *flux,
    uint32_t span,
    uint32_t offset)
{
    float value;

    if (flux == NULL || offset >= span) {
        return 0.0f;
    }
    value = flux[offset];
    if (offset > 0u && flux[offset - 1u] > value) {
        value = flux[offset - 1u];
    }
    if (offset + 1u < span && flux[offset + 1u] > value) {
        value = flux[offset + 1u];
    }
    return value;
}

static uint32_t apta_meter_collect_beats(
    const apta_session_t *session,
    uint32_t lag,
    float *strengths,
    uint32_t capacity,
    uint64_t *first_beat_bin_out)
{
    uint64_t evidence_first;
    uint64_t evidence_end;
    uint64_t anchor_bin;
    uint64_t first_beat_bin;
    uint64_t beat_bin;
    uint32_t count = 0u;
    uint32_t span;

    if (session == NULL || lag == 0u || strengths == NULL || capacity == 0u ||
        session->onset_flux == NULL ||
        session->s4_refresh_evidence_end <= session->s4_refresh_evidence_first) {
        return 0u;
    }
    evidence_first = session->s4_refresh_evidence_first;
    evidence_end = session->s4_refresh_evidence_end;
    span = (uint32_t)(evidence_end - evidence_first);
    anchor_bin = session->local_grid_segment.anchor_position.whole_frame /
                 APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    first_beat_bin = anchor_bin;
    if (first_beat_bin < evidence_first) {
        const uint64_t delta = evidence_first - first_beat_bin;
        const uint64_t steps = (delta + lag - 1u) / lag;
        first_beat_bin += steps * lag;
    }
    if (first_beat_bin >= evidence_end) {
        return 0u;
    }

    for (beat_bin = first_beat_bin;
         beat_bin < evidence_end && count < capacity;
         beat_bin += lag) {
        strengths[count++] = apta_meter_flux_near(
            session->onset_flux,
            span,
            (uint32_t)(beat_bin - evidence_first));
        if (UINT64_MAX - beat_bin < lag) {
            break;
        }
    }
    if (first_beat_bin_out != NULL) {
        *first_beat_bin_out = first_beat_bin;
    }
    return count;
}

static apta_beat_ordinal_t apta_meter_ordinal_at_bin(
    const apta_session_t *session,
    uint64_t beat_bin,
    uint32_t lag)
{
    const uint64_t anchor_bin =
        session->local_grid_segment.anchor_position.whole_frame /
        APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    int64_t delta;

    if (lag == 0u) {
        return session->local_grid_segment.anchor_ordinal;
    }
    if (beat_bin >= anchor_bin) {
        delta = (int64_t)((beat_bin - anchor_bin) / lag);
    } else {
        delta = -(int64_t)((anchor_bin - beat_bin) / lag);
    }
    return session->local_grid_segment.anchor_ordinal + delta;
}

int apta_internal_meter_refresh_pending(const apta_session_t *session)
{
    return session != NULL &&
           (session->config.requested_features &
            APTA_FEATURE_METER_DOWNBEAT) != 0u &&
           session->has_local_grid &&
           session->meter_source_s4_serial != session->s4_mutation_serial;
}

apta_status_t apta_internal_meter_refresh(
    apta_session_t *session,
    uint32_t step_limit,
    uint32_t *completed_steps_out)
{
    float strengths[APTA_INTERNAL_ONSET_BIN_CAPACITY /
                    APTA_INTERNAL_MIN_TEMPO_BINS * 256u];
    apta_internal_meter_selection_t selection;
    apta_meter_segment_t next;
    uint64_t first_beat_bin = 0u;
    uint64_t downbeat_bin;
    uint32_t lag;
    uint32_t beat_count;
    uint32_t index;
    apta_beat_ordinal_t first_ordinal;
    apta_beat_ordinal_t downbeat_ordinal;
    int changed;
    apta_status_t status;

    if (completed_steps_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *completed_steps_out = 0u;
    if (session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_meter_refresh_pending(session)) {
        return APTA_STATUS_OK;
    }
    if (step_limit == 0u) {
        return APTA_STATUS_MORE_WORK;
    }
    *completed_steps_out = 1u;

    lag = apta_meter_lag_from_tempo(
        session, session->tempo_value.tempo_millibpm);
    if (lag == 0u) {
        session->meter_source_s4_serial = session->s4_mutation_serial;
        return APTA_STATUS_OK;
    }
    beat_count = apta_meter_collect_beats(
        session,
        lag,
        strengths,
        (uint32_t)(sizeof(strengths) / sizeof(strengths[0])),
        &first_beat_bin);
    status = apta_internal_meter_select(strengths, beat_count, &selection);
    session->meter_source_s4_serial = session->s4_mutation_serial;
    if (status == APTA_STATUS_NOT_AVAILABLE) {
        return APTA_STATUS_OK;
    }
    if (status < 0) {
        return status;
    }

    first_ordinal = apta_meter_ordinal_at_bin(session, first_beat_bin, lag);
    index = selection.downbeat_phase;
    downbeat_bin = first_beat_bin + (uint64_t)index * lag;
    downbeat_ordinal = first_ordinal + (apta_beat_ordinal_t)index;

    memset(&next, 0, sizeof(next));
    apta_meter_segment_init(&next);
    next.applicability_range = session->local_grid_applicability_range;
    next.downbeat_frame = downbeat_bin * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    next.downbeat_ordinal = downbeat_ordinal;
    next.numerator = selection.numerator;
    next.denominator = selection.denominator;
    next.state = beat_count >= APTA_INTERNAL_METER_STABLE_BEATS
                     ? APTA_FEATURE_STABLE
                     : APTA_FEATURE_PROVISIONAL;
    next.confidence = selection.confidence;
    next.segment_id = session->meter_segment.segment_id;

    changed = !session->has_meter ||
              session->meter_segment.numerator != next.numerator ||
              session->meter_segment.denominator != next.denominator ||
              session->meter_segment.downbeat_ordinal != next.downbeat_ordinal ||
              session->meter_segment.downbeat_frame != next.downbeat_frame ||
              session->meter_segment.state != next.state ||
              session->meter_segment.confidence != next.confidence ||
              session->meter_segment.applicability_range.first_frame !=
                  next.applicability_range.first_frame ||
              session->meter_segment.applicability_range.end_frame !=
                  next.applicability_range.end_frame;
    if (!changed) {
        return APTA_STATUS_OK;
    }
    next.segment_id += 1u;
    if (next.segment_id == 0u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    session->meter_segment = next;
    session->has_meter = 1u;
    session->meter_mutation_serial += 1u;
    return APTA_STATUS_OK;
}

apta_feature_mask_t apta_internal_meter_pending_features(
    const apta_session_t *session)
{
    if (session == NULL || !session->has_meter ||
        session->meter_mutation_serial == session->meter_published_serial) {
        return 0u;
    }
    return APTA_FEATURE_METER_DOWNBEAT;
}

void apta_internal_meter_mark_published(apta_session_t *session)
{
    if (session != NULL) {
        session->meter_published_serial = session->meter_mutation_serial;
    }
}

apta_status_t apta_internal_meter_build_snapshot(
    const apta_session_t *session,
    apta_result_t *result)
{
    const int completed =
        atomic_load_explicit(&session->state, memory_order_acquire) ==
        APTA_SESSION_COMPLETED;

    if (session == NULL || result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!session->has_meter ||
        (session->config.requested_features & APTA_FEATURE_METER_DOWNBEAT) == 0u) {
        return APTA_STATUS_OK;
    }

    result->meter_segments =
        (apta_meter_segment_t *)apta_internal_context_allocate(
            session->context,
            sizeof(apta_meter_segment_t),
            alignof(apta_meter_segment_t),
            APTA_MEMORY_PERSISTENT);
    if (result->meter_segments == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    *result->meter_segments = session->meter_segment;
    if (completed) {
        result->meter_segments[0].state = APTA_FEATURE_FINAL;
    }

    apta_meter_view_init(&result->meter);
    result->meter.downbeat_frame = result->meter_segments[0].downbeat_frame;
    result->meter.downbeat_ordinal = result->meter_segments[0].downbeat_ordinal;
    result->meter.numerator = result->meter_segments[0].numerator;
    result->meter.denominator = result->meter_segments[0].denominator;
    result->meter.state = completed
                              ? APTA_FEATURE_FINAL
                              : result->meter_segments[0].state;
    result->meter.confidence = result->meter_segments[0].confidence;
    result->meter.segment_count = 1u;
    result->meter.segments = result->meter_segments;
    result->meter.flags = result->meter_segments[0].flags;
    result->info.available_features |= APTA_FEATURE_METER_DOWNBEAT;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_meter_pool_build(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result)
{
    const apta_internal_result_pool_layout_t *layout;
    uint8_t *storage;
    const int completed =
        atomic_load_explicit(&session->state, memory_order_acquire) ==
        APTA_SESSION_COMPLETED;

    if (pool == NULL || session == NULL || result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!session->has_meter ||
        (session->config.requested_features & APTA_FEATURE_METER_DOWNBEAT) == 0u) {
        return APTA_STATUS_OK;
    }
    layout = apta_internal_result_pool_get_layout(pool);
    storage = (uint8_t *)apta_internal_result_pool_get_slot_storage(
        pool, result->result_pool_slot_index);
    if (layout == NULL || storage == NULL || layout->meter_segment_capacity < 1u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    result->meter_segments = (apta_meter_segment_t *)(void *)(
        storage + layout->meter_segments_offset);
    *result->meter_segments = session->meter_segment;
    if (completed) {
        result->meter_segments[0].state = APTA_FEATURE_FINAL;
    }
    apta_meter_view_init(&result->meter);
    result->meter.downbeat_frame = result->meter_segments[0].downbeat_frame;
    result->meter.downbeat_ordinal = result->meter_segments[0].downbeat_ordinal;
    result->meter.numerator = result->meter_segments[0].numerator;
    result->meter.denominator = result->meter_segments[0].denominator;
    result->meter.state = completed
                              ? APTA_FEATURE_FINAL
                              : result->meter_segments[0].state;
    result->meter.confidence = result->meter_segments[0].confidence;
    result->meter.segment_count = 1u;
    result->meter.segments = result->meter_segments;
    result->meter.flags = result->meter_segments[0].flags;
    result->info.available_features |= APTA_FEATURE_METER_DOWNBEAT;
    return APTA_STATUS_OK;
}
