// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_session_workspace.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

static int apta_s4_enabled(const apta_session_t *session)
{
    return session != NULL &&
           (session->config.requested_features & APTA_INTERNAL_S4_FEATURES) != 0u;
}

static apta_source_frame_t apta_s4_min_frame(
    apta_source_frame_t left,
    apta_source_frame_t right)
{
    return left < right ? left : right;
}

static apta_source_frame_t apta_s4_max_frame(
    apta_source_frame_t left,
    apta_source_frame_t right)
{
    return left > right ? left : right;
}

static void apta_s4_init_range(
    apta_frame_range_t *range,
    apta_source_frame_t first,
    apta_source_frame_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

static const apta_internal_onset_bin_t *apta_s4_const_bin(
    const apta_session_t *session,
    uint64_t bin_index)
{
    const apta_internal_onset_bin_t *bin;

    if (session->onset_bins == NULL || session->onset_bin_capacity == 0u) {
        return NULL;
    }

    bin = &session->onset_bins[
        (uint32_t)(bin_index % session->onset_bin_capacity)];
    return bin->occupied && bin->bin_index == bin_index ? bin : NULL;
}

static uint32_t apta_s4_expected_bin_samples(
    const apta_session_t *session,
    uint64_t bin_index)
{
    apta_source_frame_t first;
    apta_source_frame_t end;

    if (bin_index > UINT64_MAX / APTA_INTERNAL_ONSET_FRAMES_PER_BIN) {
        return 0u;
    }

    first = bin_index * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    end = first + APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    if (!session->end_of_input_signalled || end <= session->final_end_frame) {
        return APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    }
    if (first >= session->final_end_frame) {
        return 0u;
    }
    return (uint32_t)(session->final_end_frame - first);
}

static int apta_s4_bin_complete(
    const apta_session_t *session,
    uint64_t bin_index)
{
    const apta_internal_onset_bin_t *bin =
        apta_s4_const_bin(session, bin_index);
    uint32_t expected = apta_s4_expected_bin_samples(session, bin_index);

    return bin != NULL && expected != 0u && bin->sample_count == expected;
}

static double apta_s4_energy(
    const apta_session_t *session,
    uint64_t bin_index)
{
    const apta_internal_onset_bin_t *bin =
        apta_s4_const_bin(session, bin_index);

    return bin != NULL && bin->sample_count != 0u
               ? bin->sum_absolute / (double)bin->sample_count
               : 0.0;
}

/* A1: the single definition of the flux computation. Called once per bin by
 * the fill loop in apta_internal_s4_refresh(); the lag and phase loops read
 * session->onset_flux instead of calling this. */
static double apta_s4_flux_uncached(
    const apta_session_t *session,
    uint64_t bin_index,
    uint64_t evidence_first)
{
    double current = apta_s4_energy(session, bin_index);
    double previous = bin_index > evidence_first
                          ? apta_s4_energy(session, bin_index - 1u)
                          : 0.0;
    return current > previous ? current - previous : 0.0;
}

static int apta_s4_find_evidence(
    const apta_session_t *session,
    uint64_t *first_out,
    uint64_t *end_out)
{
    uint64_t maximum = 0u;
    uint64_t minimum;
    uint64_t current_first = 0u;
    uint64_t best_first = 0u;
    uint64_t best_end = 0u;
    uint64_t index;
    uint32_t slot;
    int have_bin = 0;
    int in_run = 0;

    for (slot = 0u; slot < session->onset_bin_capacity; ++slot) {
        const apta_internal_onset_bin_t *bin = &session->onset_bins[slot];
        if (bin->occupied && apta_s4_bin_complete(session, bin->bin_index)) {
            if (!have_bin || bin->bin_index > maximum) {
                maximum = bin->bin_index;
            }
            have_bin = 1;
        }
    }
    if (!have_bin) {
        return 0;
    }

    minimum = maximum >= session->onset_bin_capacity - 1u
                  ? maximum - (session->onset_bin_capacity - 1u)
                  : 0u;

    for (index = minimum; index <= maximum; ++index) {
        if (apta_s4_bin_complete(session, index)) {
            if (!in_run) {
                current_first = index;
                in_run = 1;
            }
        } else if (in_run) {
            if (index - current_first > best_end - best_first) {
                best_first = current_first;
                best_end = index;
            }
            in_run = 0;
        }
    }
    if (in_run && maximum + 1u - current_first > best_end - best_first) {
        best_first = current_first;
        best_end = maximum + 1u;
    }

    if (best_end <= best_first) {
        return 0;
    }
    *first_out = best_first;
    *end_out = best_end;
    return 1;
}

static uint32_t apta_s4_tempo_from_lag(
    const apta_session_t *session,
    uint32_t lag)
{
    uint64_t numerator;
    uint64_t denominator;

    if (lag == 0u) {
        return 0u;
    }
    numerator = (uint64_t)session->config.source_sample_rate * UINT64_C(60000);
    denominator = (uint64_t)lag * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    return denominator != 0u
               ? (uint32_t)((numerator + denominator / 2u) / denominator)
               : 0u;
}

static apta_tempo_relation_t apta_s4_relation(
    uint32_t selected,
    uint32_t candidate)
{
    uint64_t difference;
    uint64_t tolerance;

    if (selected == 0u || candidate == 0u || selected == candidate) {
        return APTA_TEMPO_RELATION_INDEPENDENT;
    }

    tolerance = selected / 50u + 1u;
    difference = candidate > selected / 2u
                     ? candidate - selected / 2u
                     : selected / 2u - candidate;
    if (difference <= tolerance) {
        return APTA_TEMPO_RELATION_HALF;
    }

    tolerance = selected / 25u + 1u;
    difference = candidate > selected * 2u
                     ? candidate - selected * 2u
                     : selected * 2u - candidate;
    if (difference <= tolerance) {
        return APTA_TEMPO_RELATION_DOUBLE;
    }

    return APTA_TEMPO_RELATION_INDEPENDENT;
}

static void apta_s4_period_from_tempo(
    const apta_session_t *session,
    uint32_t tempo,
    apta_frame_period_t *period)
{
    uint64_t numerator;
    uint64_t remainder;

    memset(period, 0, sizeof(*period));
    if (tempo == 0u) {
        return;
    }

    numerator = (uint64_t)session->config.source_sample_rate * UINT64_C(60000);
    period->whole_frames = numerator / tempo;
    remainder = numerator % tempo;
    period->fraction_q32 = (uint32_t)((remainder << 32) / tempo);
}

static uint32_t apta_s4_beat_count(
    const apta_grid_segment_t *segment)
{
    uint64_t period;
    uint64_t first;
    uint64_t end;
    uint64_t anchor;
    uint64_t count;

    if (segment->applicability_range.end_frame <=
            segment->applicability_range.first_frame ||
        segment->frames_per_beat.whole_frames == 0u) {
        return 0u;
    }

    period = segment->frames_per_beat.whole_frames +
             (segment->frames_per_beat.fraction_q32 != 0u ? 1u : 0u);
    first = segment->applicability_range.first_frame;
    end = segment->applicability_range.end_frame;
    anchor = segment->anchor_position.whole_frame;

    if (anchor < first) {
        uint64_t delta = first - anchor;
        uint64_t steps = (delta + period - 1u) / period;
        if (steps > (UINT64_MAX - anchor) / period) {
            return 0u;
        }
        anchor += steps * period;
    }
    if (anchor >= end) {
        return 0u;
    }

    count = 1u + (end - 1u - anchor) / period;
    return count > UINT32_MAX ? UINT32_MAX : (uint32_t)count;
}

apta_status_t apta_internal_s4_prepare(apta_session_t *session)
{
    size_t bytes;

    if (!apta_s4_enabled(session) || session->onset_bins != NULL) {
        return APTA_STATUS_OK;
    }

    bytes = (size_t)APTA_INTERNAL_ONSET_BIN_CAPACITY *
            sizeof(apta_internal_onset_bin_t);
    session->onset_bins =
        (apta_internal_onset_bin_t *)apta_internal_session_allocate(
            session,
            bytes,
            alignof(apta_internal_onset_bin_t),
            APTA_MEMORY_PERSISTENT);
    if (session->onset_bins == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(session->onset_bins, 0, bytes);
    session->onset_bin_capacity = APTA_INTERNAL_ONSET_BIN_CAPACITY;

    bytes = (size_t)APTA_INTERNAL_ONSET_BIN_CAPACITY * sizeof(float);
    session->onset_flux = (float *)apta_internal_session_allocate(
        session,
        bytes,
        alignof(float),
        APTA_MEMORY_PERSISTENT);
    if (session->onset_flux == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(session->onset_flux, 0, bytes);
    session->onset_flux_capacity = APTA_INTERNAL_ONSET_BIN_CAPACITY;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_s4_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
    float sample)
{
    uint64_t bin_index;
    uint32_t slot;
    apta_internal_onset_bin_t *bin;

    if (!apta_s4_enabled(session)) {
        return APTA_STATUS_OK;
    }
    if (session->onset_bins == NULL) {
        return APTA_ERROR_INTERNAL;
    }

    bin_index = source_frame / APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    slot = (uint32_t)(bin_index % session->onset_bin_capacity);
    bin = &session->onset_bins[slot];
    if (!bin->occupied || bin->bin_index != bin_index) {
        memset(bin, 0, sizeof(*bin));
        bin->occupied = 1u;
        bin->bin_index = bin_index;
    }
    if (bin->sample_count == UINT32_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    bin->sum_absolute += fabs((double)sample);
    bin->sample_count += 1u;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_s4_refresh(apta_session_t *session)
{
    double best_scores[APTA_INTERNAL_MAX_TEMPO_CANDIDATES] = {0.0, 0.0, 0.0};
    uint32_t best_lags[APTA_INTERNAL_MAX_TEMPO_CANDIDATES] = {0u, 0u, 0u};
    uint64_t evidence_first;
    uint64_t evidence_end;
    uint64_t run_count;
    uint64_t index;
    uint32_t flux_capacity;
    uint32_t minimum_lag;
    uint32_t maximum_lag;
    uint32_t lag;
    uint32_t candidate_count = 0u;
    uint32_t selected_tempo;
    uint32_t phase = 0u;
    double phase_score = -1.0;
    uint32_t confidence;
    apta_feature_state_t state;
    apta_source_frame_t evidence_first_frame;
    apta_source_frame_t evidence_end_frame;
    apta_source_frame_t requested_first;
    apta_source_frame_t requested_end;
    apta_source_frame_t applicability_first;
    apta_source_frame_t applicability_end;
    uint32_t old_tempo;
    apta_feature_state_t old_state;
    apta_frame_range_t old_evidence;
    uint32_t flags = 0u;

    if (!apta_s4_enabled(session) || session->onset_bins == NULL ||
        session->onset_flux == NULL) {
        return APTA_STATUS_OK;
    }

    if (session->local_grid_locked) {
        if (atomic_load_explicit(&session->state, memory_order_acquire) ==
                APTA_SESSION_COMPLETED &&
            session->tempo_value.state != APTA_FEATURE_FINAL) {
            session->tempo_value.state = APTA_FEATURE_FINAL;
            session->local_grid_segment.state = APTA_FEATURE_FINAL;
            session->s4_mutation_serial += 1u;
        }
        return APTA_STATUS_OK;
    }

    if (!apta_s4_find_evidence(session, &evidence_first, &evidence_end)) {
        return APTA_STATUS_OK;
    }
    run_count = evidence_end - evidence_first;
    if (run_count < APTA_INTERNAL_MIN_TEMPO_BINS) {
        return APTA_STATUS_OK;
    }

    minimum_lag = (uint32_t)(
        ((uint64_t)session->config.source_sample_rate * 60u +
         (uint64_t)300u * APTA_INTERNAL_ONSET_FRAMES_PER_BIN - 1u) /
        ((uint64_t)300u * APTA_INTERNAL_ONSET_FRAMES_PER_BIN));
    maximum_lag = (uint32_t)(
        ((uint64_t)session->config.source_sample_rate * 60u) /
        ((uint64_t)40u * APTA_INTERNAL_ONSET_FRAMES_PER_BIN));
    if (minimum_lag == 0u) {
        minimum_lag = 1u;
    }
    if ((uint64_t)maximum_lag * 2u >= run_count) {
        maximum_lag = (uint32_t)(run_count / 2u);
    }
    if (maximum_lag < minimum_lag) {
        return APTA_STATUS_OK;
    }

    /* A1: flux[index] depends only on index and evidence_first, both invariant
     * across the lag loop. Compute it once here; the loops below are pure
     * array reads. Storing flux as float rounds each term once, but the
     * accumulators stay double so that the only arithmetic difference from the
     * previous implementation is that single rounding. A3 converts the
     * accumulators.
     *
     * The array is indexed linearly by (index - evidence_first), not by the
     * onset_bins ring mapping. apta_s4_find_evidence() returns a contiguous
     * run bounded by onset_bin_capacity, so the offsets fit the allocation,
     * and dropping the ring keeps an integer division out of the inner loop —
     * with a runtime capacity, index % capacity compiles to a hardware divide
     * and costs more than the correlation arithmetic it feeds. */
    flux_capacity = session->onset_flux_capacity;
    if (run_count > (uint64_t)flux_capacity) {
        return APTA_STATUS_OK;
    }
    for (index = evidence_first; index < evidence_end; ++index) {
        session->onset_flux[(uint32_t)(index - evidence_first)] =
            (float)apta_s4_flux_uncached(session, index, evidence_first);
    }

    for (lag = minimum_lag; lag <= maximum_lag; ++lag) {
        double numerator = 0.0;
        double left_square = 0.0;
        double right_square = 0.0;
        double score;
        uint32_t position;

        const float *flux = session->onset_flux;

        for (index = evidence_first + lag; index < evidence_end; ++index) {
            const uint32_t offset = (uint32_t)(index - evidence_first);
            const double left = (double)flux[offset];
            const double right = (double)flux[offset - lag];
            numerator += left * right;
            left_square += left * left;
            right_square += right * right;
        }
        if (left_square <= 1e-18 || right_square <= 1e-18) {
            continue;
        }
        score = numerator / sqrt(left_square * right_square);
        if (score <= 0.0) {
            continue;
        }

        for (position = 0u;
             position < APTA_INTERNAL_MAX_TEMPO_CANDIDATES;
             ++position) {
            if (score > best_scores[position]) {
                uint32_t move;
                for (move = APTA_INTERNAL_MAX_TEMPO_CANDIDATES - 1u;
                     move > position;
                     --move) {
                    best_scores[move] = best_scores[move - 1u];
                    best_lags[move] = best_lags[move - 1u];
                }
                best_scores[position] = score;
                best_lags[position] = lag;
                break;
            }
        }
    }

    if (best_lags[0] == 0u || best_scores[0] < 0.05) {
        return APTA_STATUS_OK;
    }

    for (lag = 0u; lag < APTA_INTERNAL_MAX_TEMPO_CANDIDATES; ++lag) {
        uint32_t tempo;
        uint32_t score;
        uint32_t previous;
        int duplicate = 0;

        if (best_lags[lag] == 0u) {
            continue;
        }
        tempo = apta_s4_tempo_from_lag(session, best_lags[lag]);
        if (tempo < APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
            tempo > APTA_REFERENCE_TEMPO_MAX_MILLIBPM) {
            continue;
        }
        for (previous = 0u; previous < candidate_count; ++previous) {
            uint32_t existing = session->tempo_candidates[previous].tempo_millibpm;
            uint32_t difference = tempo > existing ? tempo - existing : existing - tempo;
            if (difference <= 500u) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        score = (uint32_t)(best_scores[lag] / best_scores[0] * 65535.0 + 0.5);
        if (score > 65535u) {
            score = 65535u;
        }
        session->tempo_candidates[candidate_count].tempo_millibpm = tempo;
        session->tempo_candidates[candidate_count].score = (uint16_t)score;
        session->tempo_candidates[candidate_count].confidence =
            (apta_confidence_value_t)(25u + (score * 70u) / 65535u);
        session->tempo_candidates[candidate_count].reserved8 = 0u;
        session->tempo_candidates[candidate_count].flags = 0u;
        candidate_count += 1u;
    }
    if (candidate_count == 0u) {
        return APTA_STATUS_OK;
    }

    selected_tempo = session->tempo_candidates[0].tempo_millibpm;
    for (lag = 0u; lag < candidate_count; ++lag) {
        session->tempo_candidates[lag].relation_to_selected =
            apta_s4_relation(selected_tempo,
                             session->tempo_candidates[lag].tempo_millibpm);
        if (lag != 0u &&
            session->tempo_candidates[lag].score >=
                (uint16_t)((uint32_t)session->tempo_candidates[0].score * 7u / 10u)) {
            if (session->tempo_candidates[lag].relation_to_selected ==
                APTA_TEMPO_RELATION_HALF) {
                flags |= APTA_TEMPO_FLAG_HALF_TIME_AMBIGUITY;
            }
            if (session->tempo_candidates[lag].relation_to_selected ==
                APTA_TEMPO_RELATION_DOUBLE) {
                flags |= APTA_TEMPO_FLAG_DOUBLE_TIME_AMBIGUITY;
            }
        }
    }

    confidence = 35u + (uint32_t)(best_scores[0] * 50.0);
    if (candidate_count > 1u && best_scores[0] > 0.0) {
        double separation = 1.0 - best_scores[1] / best_scores[0];
        if (separation > 0.0) {
            confidence += (uint32_t)(separation * 15.0);
        }
    }
    if (run_count >= APTA_INTERNAL_STABLE_TEMPO_BINS) {
        confidence += 5u;
    }
    if (confidence > APTA_CONFIDENCE_MAX) {
        confidence = APTA_CONFIDENCE_MAX;
    }

    for (lag = 0u; lag < best_lags[0]; ++lag) {
        double score = 0.0;
        for (index = evidence_first + lag;
             index < evidence_end;
             index += best_lags[0]) {
            score += (double)session->onset_flux[
                (uint32_t)(index - evidence_first)];
        }
        if (score > phase_score) {
            phase_score = score;
            phase = lag;
        }
    }

    evidence_first_frame = evidence_first * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    evidence_end_frame = evidence_end * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    if (session->end_of_input_signalled) {
        evidence_end_frame = apta_s4_min_frame(
            evidence_end_frame,
            session->final_end_frame);
    }

    requested_first = evidence_first_frame;
    requested_end = evidence_end_frame;
    if (session->has_focus &&
        (session->focus.feature_mask & APTA_INTERNAL_S4_FEATURES) != 0u) {
        requested_first = session->focus.playhead_frame >
                                  session->focus.lookbehind_frames
                              ? session->focus.playhead_frame -
                                    session->focus.lookbehind_frames
                              : 0u;
        requested_end = session->focus.playhead_frame;
        if (UINT64_MAX - requested_end < session->focus.lookahead_frames) {
            requested_end = UINT64_MAX;
        } else {
            requested_end += session->focus.lookahead_frames;
        }
    }

    applicability_first = apta_s4_max_frame(requested_first, evidence_first_frame);
    applicability_end = apta_s4_min_frame(requested_end, evidence_end_frame);
    if (applicability_end <= applicability_first) {
        applicability_first = evidence_first_frame;
        applicability_end = evidence_end_frame;
    }

    state = run_count >= APTA_INTERNAL_STABLE_TEMPO_BINS && confidence >= 50u
                ? APTA_FEATURE_STABLE
                : APTA_FEATURE_PROVISIONAL;
    if (state == APTA_FEATURE_STABLE && session->end_of_input_signalled &&
        evidence_first_frame == 0u &&
        evidence_end_frame == session->final_end_frame &&
        atomic_load_explicit(&session->state, memory_order_acquire) ==
            APTA_SESSION_COMPLETED) {
        state = APTA_FEATURE_FINAL;
    }

    old_tempo = session->has_tempo ? session->tempo_value.tempo_millibpm : 0u;
    old_state = session->has_tempo ? session->tempo_value.state : APTA_FEATURE_ABSENT;
    old_evidence = session->tempo_value.evidence_range;

    if (session->tempo_candidate_set_id == 0u) {
        session->tempo_candidate_set_id = 1u;
    } else if (old_tempo != 0u && old_tempo != selected_tempo) {
        session->tempo_candidate_set_id += 1u;
        if (session->tempo_candidate_set_id == 0u) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
    }
    if (session->local_grid_segment_id == 0u) {
        session->local_grid_segment_id = 1u;
    }

    memset(&session->tempo_value, 0, sizeof(session->tempo_value));
    session->tempo_value.struct_size = (uint32_t)sizeof(session->tempo_value);
    session->tempo_value.api_version = APTA_API_VERSION;
    apta_s4_init_range(&session->tempo_value.evidence_range,
                       evidence_first_frame,
                       evidence_end_frame);
    apta_s4_init_range(&session->tempo_value.applicability_range,
                       applicability_first,
                       applicability_end);
    session->tempo_value.tempo_millibpm = selected_tempo;
    session->tempo_value.confidence = (apta_confidence_value_t)confidence;
    session->tempo_value.state = state;
    session->tempo_value.flags = flags;
    session->tempo_value.candidate_set_id = session->tempo_candidate_set_id;
    session->tempo_candidate_count = candidate_count;

    memset(&session->local_grid_segment, 0, sizeof(session->local_grid_segment));
    session->local_grid_segment.struct_size =
        (uint32_t)sizeof(session->local_grid_segment);
    session->local_grid_segment.api_version = APTA_API_VERSION;
    apta_s4_init_range(&session->local_grid_segment.applicability_range,
                       applicability_first,
                       applicability_end);
    session->local_grid_segment.anchor_position.whole_frame =
        (evidence_first + phase) * APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    session->local_grid_segment.anchor_ordinal = 0;
    apta_s4_period_from_tempo(
        session,
        selected_tempo,
        &session->local_grid_segment.frames_per_beat);
    session->local_grid_segment.nominal_tempo_millibpm = selected_tempo;
    session->local_grid_segment.confidence =
        (apta_confidence_value_t)confidence;
    session->local_grid_segment.state = state;
    session->local_grid_segment.flags = flags;
    session->local_grid_segment.segment_id = session->local_grid_segment_id;
    session->local_grid_segment.revision = session->tempo_candidate_set_id;
    session->local_grid_segment.beat_count =
        apta_s4_beat_count(&session->local_grid_segment);

    apta_s4_init_range(&session->local_grid_requested_range,
                       requested_first,
                       requested_end);
    apta_s4_init_range(&session->local_grid_evidence_range,
                       evidence_first_frame,
                       evidence_end_frame);
    apta_s4_init_range(&session->local_grid_applicability_range,
                       applicability_first,
                       applicability_end);
    apta_s4_init_range(&session->local_grid_coverage_range,
                       applicability_first,
                       applicability_end);

    session->has_tempo = 1u;
    session->has_local_grid = 1u;

    if (old_tempo != selected_tempo || old_state != state ||
        old_evidence.first_frame != evidence_first_frame ||
        old_evidence.end_frame != evidence_end_frame) {
        session->s4_mutation_serial += 1u;
    }
    return APTA_STATUS_OK;
}

apta_feature_mask_t apta_internal_s4_pending_features(
    const apta_session_t *session)
{
    apta_feature_mask_t features = 0u;

    if (session == NULL ||
        session->s4_mutation_serial == session->s4_published_serial) {
        return 0u;
    }
    if (session->has_tempo) {
        features |= APTA_FEATURE_BPM;
        if ((session->config.requested_features & APTA_FEATURE_CONFIDENCE) != 0u) {
            features |= APTA_FEATURE_CONFIDENCE;
        }
    }
    if (session->has_local_grid) {
        features |= APTA_FEATURE_LOCAL_BEATGRID;
        if ((session->config.requested_features & APTA_FEATURE_GRID_LOCKING) != 0u) {
            features |= APTA_FEATURE_GRID_LOCKING;
        }
    }
    return features;
}

void apta_internal_s4_mark_published(apta_session_t *session)
{
    if (session != NULL) {
        session->s4_published_serial = session->s4_mutation_serial;
    }
}

apta_status_t apta_internal_s4_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    if (session == NULL || result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (session->has_tempo) {
        size_t bytes = (size_t)session->tempo_candidate_count *
                       sizeof(apta_tempo_candidate_t);
        if (session->tempo_candidate_count > APTA_INTERNAL_MAX_TEMPO_CANDIDATES) {
            return APTA_ERROR_INTERNAL;
        }
        if (bytes != 0u) {
            result->tempo_candidates =
                (apta_tempo_candidate_t *)apta_internal_context_allocate(
                    session->context,
                    bytes,
                    alignof(apta_tempo_candidate_t),
                    APTA_MEMORY_PERSISTENT);
            if (result->tempo_candidates == NULL) {
                return APTA_ERROR_OUT_OF_MEMORY;
            }
            memcpy(result->tempo_candidates, session->tempo_candidates, bytes);
        }

        apta_tempo_view_init(&result->tempo);
        result->tempo.selected = session->tempo_value;
        result->tempo.candidate_count = session->tempo_candidate_count;
        result->tempo.candidates = result->tempo_candidates;
        result->info.available_features |= APTA_FEATURE_BPM;
        if ((session->config.requested_features & APTA_FEATURE_CONFIDENCE) != 0u) {
            result->info.available_features |= APTA_FEATURE_CONFIDENCE;
        }
    }

    if (session->has_local_grid) {
        result->local_grid_coverage =
            (apta_frame_range_t *)apta_internal_context_allocate(
                session->context,
                sizeof(apta_frame_range_t),
                alignof(apta_frame_range_t),
                APTA_MEMORY_PERSISTENT);
        result->local_grid_segments =
            (apta_grid_segment_t *)apta_internal_context_allocate(
                session->context,
                sizeof(apta_grid_segment_t),
                alignof(apta_grid_segment_t),
                APTA_MEMORY_PERSISTENT);
        if (result->local_grid_coverage == NULL ||
            result->local_grid_segments == NULL) {
            apta_internal_s4_cleanup_result(result);
            return APTA_ERROR_OUT_OF_MEMORY;
        }
        *result->local_grid_coverage = session->local_grid_coverage_range;
        *result->local_grid_segments = session->local_grid_segment;

        apta_grid_view_init(&result->local_grid);
        result->local_grid.requested_range = session->local_grid_requested_range;
        result->local_grid.evidence_range = session->local_grid_evidence_range;
        result->local_grid.applicability_range =
            session->local_grid_applicability_range;
        result->local_grid.representation = APTA_GRID_REPRESENTATION_SEGMENTS;
        result->local_grid.state = session->local_grid_segment.state;
        result->local_grid.confidence = session->local_grid_segment.confidence;
        result->local_grid.coverage_range_count = 1u;
        result->local_grid.coverage_ranges = result->local_grid_coverage;
        result->local_grid.segment_count = 1u;
        result->local_grid.segments = result->local_grid_segments;
        result->local_grid.flags = session->local_grid_segment.flags;
        result->info.available_features |= APTA_FEATURE_LOCAL_BEATGRID;
        if ((session->config.requested_features & APTA_FEATURE_GRID_LOCKING) != 0u) {
            result->info.available_features |= APTA_FEATURE_GRID_LOCKING;
        }
    }

    return APTA_STATUS_OK;
}

void apta_internal_s4_cleanup_session(apta_session_t *session)
{
    if (session == NULL) {
        return;
    }
    apta_internal_context_deallocate(session->context, session->onset_bins);
    session->onset_bins = NULL;
    session->onset_bin_capacity = 0u;
    apta_internal_context_deallocate(session->context, session->onset_flux);
    session->onset_flux = NULL;
    session->onset_flux_capacity = 0u;
}

void apta_internal_s4_cleanup_result(apta_result_t *result)
{
    if (result == NULL) {
        return;
    }
    apta_internal_context_deallocate(result->context, result->tempo_candidates);
    apta_internal_context_deallocate(result->context, result->local_grid_coverage);
    apta_internal_context_deallocate(result->context, result->local_grid_segments);
    result->tempo_candidates = NULL;
    result->local_grid_coverage = NULL;
    result->local_grid_segments = NULL;
    result->tempo.candidates = NULL;
    result->tempo.candidate_count = 0u;
    result->local_grid.coverage_ranges = NULL;
    result->local_grid.coverage_range_count = 0u;
    result->local_grid.segments = NULL;
    result->local_grid.segment_count = 0u;
}

apta_status_t APTA_CALL apta_session_lock_grid_range(
    apta_session_t *session,
    const apta_frame_range_t *range)
{
    apta_frame_range_t old_applicability;
    apta_frame_range_t old_coverage;
    apta_grid_segment_t old_segment;
    uint32_t old_locked;
    uint64_t old_serial;
    apta_status_t status;

    if (session == NULL || range == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            range,
            sizeof(*range),
            range->struct_size,
            range->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (range->first_frame >= range->end_frame) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((session->config.requested_features &
         (APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GRID_LOCKING)) !=
        (APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GRID_LOCKING)) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (!session->has_local_grid ||
        (session->local_grid_segment.state != APTA_FEATURE_STABLE &&
         session->local_grid_segment.state != APTA_FEATURE_FINAL)) {
        return APTA_ERROR_INVALID_STATE;
    }
    if (range->first_frame <
            session->local_grid_applicability_range.first_frame ||
        range->end_frame >
            session->local_grid_applicability_range.end_frame) {
        return APTA_ERROR_CONFLICT;
    }
    if (session->local_grid_locked) {
        return session->local_grid_applicability_range.first_frame ==
                       range->first_frame &&
                   session->local_grid_applicability_range.end_frame ==
                       range->end_frame
                   ? APTA_STATUS_OK
                   : APTA_ERROR_CONFLICT;
    }

    old_applicability = session->local_grid_applicability_range;
    old_coverage = session->local_grid_coverage_range;
    old_segment = session->local_grid_segment;
    old_locked = session->local_grid_locked;
    old_serial = session->s4_mutation_serial;

    session->local_grid_locked = 1u;
    session->local_grid_applicability_range = *range;
    session->local_grid_coverage_range = *range;
    session->local_grid_segment.applicability_range = *range;
    session->local_grid_segment.flags |= APTA_GRID_FLAG_LOCKED;
    session->local_grid_segment.beat_count =
        apta_s4_beat_count(&session->local_grid_segment);
    session->s4_mutation_serial += 1u;

    status = apta_internal_publish_result(
        session,
        APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GRID_LOCKING);
    if (status < 0) {
        session->local_grid_applicability_range = old_applicability;
        session->local_grid_coverage_range = old_coverage;
        session->local_grid_segment = old_segment;
        session->local_grid_locked = old_locked;
        session->s4_mutation_serial = old_serial;
        return status;
    }
    apta_internal_s4_mark_published(session);
    return APTA_STATUS_OK;
}
