// SPDX-License-Identifier: Apache-2.0
#include "apta_s6_internal.h"
#include "../core/apta_period_refine.h"
#include "../core/apta_session_workspace.h"
#include "../core/apta_tempo_prior.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t first_bin;
    uint64_t end_bin;
    uint32_t tempo_millibpm;
    uint32_t phase_bins;
    apta_confidence_value_t confidence;
    float score;
} apta_s6_window_t;

static int apta_s6_enabled(const apta_session_t *session)
{
    return session != NULL &&
           (session->config.requested_features & APTA_INTERNAL_S6_FEATURES) != 0u;
}

static void apta_s6_init_range(
    apta_frame_range_t *range,
    apta_source_frame_t first,
    apta_source_frame_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

static apta_source_frame_t apta_s6_min_frame(
    apta_source_frame_t left,
    apta_source_frame_t right)
{
    return left < right ? left : right;
}

static const apta_internal_onset_bin_t *apta_s6_const_bin(
    const apta_internal_s6_session_state_t *state,
    uint64_t bin_index)
{
    const apta_internal_onset_bin_t *bin;

    if (state == NULL || state->global_bins == NULL ||
        state->global_bin_capacity == 0u) {
        return NULL;
    }
    bin = &state->global_bins[
        (uint32_t)(bin_index % state->global_bin_capacity)];
    return bin->occupied && bin->bin_index == bin_index ? bin : NULL;
}

static uint32_t apta_s6_expected_bin_samples(
    const apta_session_t *session,
    uint64_t bin_index)
{
    apta_source_frame_t first;
    apta_source_frame_t end;

    if (bin_index > UINT64_MAX / APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN) {
        return 0u;
    }
    first = bin_index * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN;
    end = first + APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN;
    if (!session->end_of_input_signalled || end <= session->final_end_frame) {
        return APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN;
    }
    if (first >= session->final_end_frame) {
        return 0u;
    }
    return (uint32_t)(session->final_end_frame - first);
}

static int apta_s6_bin_complete(
    const apta_session_t *session,
    uint64_t bin_index)
{
    const apta_internal_onset_bin_t *bin =
        apta_s6_const_bin(session->s6, bin_index);
    const uint32_t expected = apta_s6_expected_bin_samples(session, bin_index);

    return bin != NULL && expected != 0u && bin->sample_count == expected;
}

/* A3: see apta_s4_energy(). */
static float apta_s6_energy(
    const apta_internal_s6_session_state_t *state,
    uint64_t bin_index)
{
    const apta_internal_onset_bin_t *bin = apta_s6_const_bin(state, bin_index);
    return bin != NULL && bin->sample_count != 0u
               ? (float)bin->sum_absolute /
                     ((float)bin->sample_count *
                      APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE)
               : 0.0f;
}

/* A1: the single definition of the flux computation. Called once per bin by
 * the fill loop in apta_internal_s6_refresh(); the per-window lag and phase
 * loops read state->global_flux instead of calling this. */
static float apta_s6_flux_uncached(
    const apta_internal_s6_session_state_t *state,
    uint64_t bin_index,
    uint64_t evidence_first)
{
    const float current = apta_s6_energy(state, bin_index);
    const float previous = bin_index > evidence_first
                               ? apta_s6_energy(state, bin_index - 1u)
                               : 0.0f;
    return current > previous ? current - previous : 0.0f;
}

static int apta_s6_find_evidence(
    const apta_session_t *session,
    uint64_t *first_out,
    uint64_t *end_out)
{
    const apta_internal_s6_session_state_t *state = session->s6;
    uint64_t minimum = UINT64_MAX;
    uint64_t maximum = 0u;
    uint64_t current_first = 0u;
    uint64_t best_first = 0u;
    uint64_t best_end = 0u;
    uint64_t index;
    uint32_t slot;
    int have = 0;
    int in_run = 0;

    for (slot = 0u; slot < state->global_bin_capacity; ++slot) {
        const apta_internal_onset_bin_t *bin = &state->global_bins[slot];
        if (!bin->occupied || !apta_s6_bin_complete(session, bin->bin_index)) {
            continue;
        }
        if (bin->bin_index < minimum) {
            minimum = bin->bin_index;
        }
        if (!have || bin->bin_index > maximum) {
            maximum = bin->bin_index;
        }
        have = 1;
    }
    if (!have) {
        return 0;
    }

    for (index = minimum; index <= maximum; ++index) {
        if (apta_s6_bin_complete(session, index)) {
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

static uint32_t apta_s6_tempo_from_lag(
    const apta_session_t *session,
    uint32_t lag)
{
    const uint64_t numerator =
        (uint64_t)session->config.source_sample_rate * UINT64_C(60000);
    const uint64_t denominator =
        (uint64_t)lag * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN;

    return lag != 0u && denominator != 0u
               ? (uint32_t)((numerator + denominator / 2u) / denominator)
               : 0u;
}

static void apta_s6_period_from_tempo(
    const apta_session_t *session,
    uint32_t tempo,
    apta_frame_period_t *period)
{
    const uint64_t numerator =
        (uint64_t)session->config.source_sample_rate * UINT64_C(60000);
    uint64_t remainder;

    memset(period, 0, sizeof(*period));
    if (tempo == 0u) {
        return;
    }
    period->whole_frames = numerator / tempo;
    remainder = numerator % tempo;
    period->fraction_q32 = (uint32_t)((remainder << 32) / tempo);
}

static uint32_t apta_s6_beat_count(const apta_grid_segment_t *segment)
{
    uint64_t period;
    uint64_t anchor;
    uint64_t first;
    uint64_t end;
    uint64_t steps;
    uint64_t count;

    if (segment->frames_per_beat.whole_frames == 0u ||
        segment->applicability_range.first_frame >=
            segment->applicability_range.end_frame) {
        return 0u;
    }
    period = segment->frames_per_beat.whole_frames +
             (segment->frames_per_beat.fraction_q32 != 0u ? 1u : 0u);
    anchor = segment->anchor_position.whole_frame;
    first = segment->applicability_range.first_frame;
    end = segment->applicability_range.end_frame;
    if (anchor < first) {
        steps = (first - anchor + period - 1u) / period;
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

static int apta_s6_estimate_window(
    const apta_session_t *session,
    uint64_t first,
    uint64_t end,
    apta_s6_window_t *window)
{
    uint32_t minimum_lag;
    uint32_t maximum_lag;
    uint32_t best_lag = 0u;
    uint32_t lag;
    uint32_t phase = 0u;
    float best_score = 0.0f;
    /* The acceptance gate below measures the evidence, so it has to see the
     * correlation itself rather than the prior-weighted score the argmax ranks
     * on. Weighting the gated value would reject correct answers at the edges
     * of the prior for having an unfashionable tempo. */
    float best_correlation = 0.0f;
    float best_phase_score = -1.0f;
    uint64_t index;
    const float *flux;
    uint64_t flux_base;

    if (window == NULL || end <= first ||
        end - first < APTA_INTERNAL_GLOBAL_MIN_BINS) {
        return 0;
    }
    if (session->s6->global_flux == NULL) {
        return 0;
    }
    /* A1: flux is indexed linearly from the refresh's evidence start, shared
     * by every window of that refresh. See apta_internal_s6_refresh(). */
    flux = session->s6->global_flux;
    flux_base = session->s6->flux_base_bin;

    minimum_lag = (uint32_t)(
        ((uint64_t)session->config.source_sample_rate * 60u +
         (uint64_t)300u * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN - 1u) /
        ((uint64_t)300u * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN));
    maximum_lag = (uint32_t)(
        ((uint64_t)session->config.source_sample_rate * 60u) /
        ((uint64_t)40u * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN));
    if (minimum_lag == 0u) {
        minimum_lag = 1u;
    }
    if ((uint64_t)maximum_lag * 2u >= end - first) {
        maximum_lag = (uint32_t)((end - first) / 2u);
    }
    if (maximum_lag < minimum_lag) {
        return 0;
    }

    for (lag = minimum_lag; lag <= maximum_lag; ++lag) {
        float numerator = 0.0f;
        float left_square = 0.0f;
        float right_square = 0.0f;
        float correlation;
        float score;

        for (index = first + lag; index < end; ++index) {
            const uint32_t offset = (uint32_t)(index - flux_base);
            const float left = flux[offset];
            const float right = flux[offset - lag];
            numerator += left * right;
            left_square += left * left;
            right_square += right * right;
        }
        /* A3: float guard, sized for normalized flux. See apta_s4.c. */
        if (left_square <= 1e-12f || right_square <= 1e-12f) {
            continue;
        }
        /* B1's prior, which S4 has had since the octave work and S6 never got.
         * Autocorrelation peaks at every multiple of the true period, so a bare
         * argmax picks whichever family member happens to score highest. On 68
         * real tracks that was the wrong one 38 times: 25 landed on half the
         * tempo and 11 on a third. */
        correlation = numerator / sqrtf(left_square * right_square);
        score = correlation *
                apta_internal_tempo_prior(
                    apta_s6_tempo_from_lag(session, lag));
        if (score > best_score) {
            best_score = score;
            best_correlation = correlation;
            best_lag = lag;
        }
    }
    if (best_lag == 0u || best_correlation < 0.04f) {
        return 0;
    }

    for (lag = 0u; lag < best_lag; ++lag) {
        float score = 0.0f;
        for (index = first + lag; index < end; index += best_lag) {
            score += flux[(uint32_t)(index - flux_base)];
        }
        if (score > best_phase_score) {
            best_phase_score = score;
            phase = lag;
        }
    }

    memset(window, 0, sizeof(*window));
    window->first_bin = first;
    window->end_bin = end;
    /* A global bin is 46 ms, so integer lags near 128 BPM are 13 BPM apart and
     * the whole 110-150 range holds three of them. Refining matters more here
     * than in S4, and helps less: the window bounds how far across the track
     * the measurement can reach. */
    window->tempo_millibpm = apta_internal_tempo_with_offset(
        apta_s6_tempo_from_lag(session, best_lag),
        best_lag,
        apta_internal_refine_lag(
            &flux[(uint32_t)(first - flux_base)],
            (uint32_t)(end - first),
            best_lag,
            APTA_INTERNAL_TEMPO_REFINE_MAX_BEATS));
    if (window->tempo_millibpm < APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
        window->tempo_millibpm > APTA_REFERENCE_TEMPO_MAX_MILLIBPM) {
        return 0;
    }
    window->phase_bins = phase;
    window->score = best_score;
    window->confidence = (apta_confidence_value_t)(40u +
        (best_score >= 1.0 ? 50u : (uint32_t)(best_score * 50.0)));
    if (window->confidence > APTA_CONFIDENCE_MAX) {
        window->confidence = APTA_CONFIDENCE_MAX;
    }
    return 1;
}

static uint64_t apta_s6_signature(
    const apta_internal_s6_session_state_t *state)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t index;

#define APTA_S6_HASH_VALUE(value)                         \
    do {                                                  \
        uint64_t apta_s6_hash_v = (uint64_t)(value);      \
        uint32_t apta_s6_hash_i;                          \
        for (apta_s6_hash_i = 0u; apta_s6_hash_i < 8u; ++apta_s6_hash_i) { \
            hash ^= (uint8_t)(apta_s6_hash_v >> (apta_s6_hash_i * 8u));     \
            hash *= UINT64_C(1099511628211);               \
        }                                                  \
    } while (0)

    APTA_S6_HASH_VALUE(state->segment_count);
    APTA_S6_HASH_VALUE(state->beat_count);
    APTA_S6_HASH_VALUE(state->representation);
    for (index = 0u; index < state->segment_count; ++index) {
        const apta_grid_segment_t *segment = &state->segments[index];
        APTA_S6_HASH_VALUE(segment->applicability_range.first_frame);
        APTA_S6_HASH_VALUE(segment->applicability_range.end_frame);
        APTA_S6_HASH_VALUE(segment->anchor_position.whole_frame);
        APTA_S6_HASH_VALUE(segment->anchor_position.fraction_q32);
        APTA_S6_HASH_VALUE(segment->frames_per_beat.whole_frames);
        APTA_S6_HASH_VALUE(segment->frames_per_beat.fraction_q32);
        APTA_S6_HASH_VALUE(segment->nominal_tempo_millibpm);
    }
#undef APTA_S6_HASH_VALUE
    return hash;
}

static int apta_s6_locked_conflict(const apta_session_t *session)
{
    uint32_t index;

    if (!session->local_grid_locked || !session->has_local_grid ||
        session->s6 == NULL) {
        return 0;
    }
    for (index = 0u; index < session->s6->segment_count; ++index) {
        const apta_grid_segment_t *global = &session->s6->segments[index];
        const apta_grid_segment_t *local = &session->local_grid_segment;
        uint32_t difference;

        if (global->applicability_range.first_frame >=
                local->applicability_range.end_frame ||
            local->applicability_range.first_frame >=
                global->applicability_range.end_frame) {
            continue;
        }
        difference = global->nominal_tempo_millibpm >
                             local->nominal_tempo_millibpm
                         ? global->nominal_tempo_millibpm -
                               local->nominal_tempo_millibpm
                         : local->nominal_tempo_millibpm -
                               global->nominal_tempo_millibpm;
        if (difference > 500u) {
            return 1;
        }
        if (global->anchor_position.whole_frame >
                local->anchor_position.whole_frame +
                    APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN ||
            local->anchor_position.whole_frame >
                global->anchor_position.whole_frame +
                    APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN) {
            return 1;
        }
    }
    return 0;
}

static apta_status_t apta_s6_generate_beats(
    apta_internal_s6_session_state_t *state)
{
    uint32_t segment_index;
    uint32_t beat_count = 0u;
    apta_beat_ordinal_t ordinal = 0;

    for (segment_index = 0u;
         segment_index < state->segment_count;
         ++segment_index) {
        apta_grid_segment_t *segment = &state->segments[segment_index];
        uint64_t period_q32;
        uint64_t position_q32;
        uint64_t first_q32;
        uint64_t end_q32;
        uint64_t steps;
        uint32_t segment_beats = 0u;

        if (segment->frames_per_beat.whole_frames == 0u ||
            segment->frames_per_beat.whole_frames > UINT32_MAX ||
            segment->anchor_position.whole_frame > UINT32_MAX ||
            segment->applicability_range.first_frame > UINT32_MAX ||
            segment->applicability_range.end_frame > UINT32_MAX) {
            state->flags |= APTA_GRID_FLAG_DEGRADED;
            continue;
        }
        period_q32 = (segment->frames_per_beat.whole_frames << 32u) |
                     segment->frames_per_beat.fraction_q32;
        position_q32 = (segment->anchor_position.whole_frame << 32u) |
                       segment->anchor_position.fraction_q32;
        first_q32 = segment->applicability_range.first_frame << 32u;
        end_q32 = segment->applicability_range.end_frame << 32u;
        if (period_q32 == 0u) {
            continue;
        }
        if (position_q32 < first_q32) {
            steps = (first_q32 - position_q32 + period_q32 - 1u) / period_q32;
            if (steps > (UINT64_MAX - position_q32) / period_q32) {
                return APTA_ERROR_LIMIT_EXCEEDED;
            }
            position_q32 += steps * period_q32;
        }
        segment->anchor_ordinal = ordinal;
        while (position_q32 < end_q32) {
            apta_beat_t *beat;
            if (beat_count >= state->beat_capacity) {
                state->flags |= APTA_GRID_FLAG_DEGRADED;
                break;
            }
            beat = &state->beats[beat_count];
            memset(beat, 0, sizeof(*beat));
            beat->position.whole_frame = position_q32 >> 32u;
            beat->position.fraction_q32 = (uint32_t)position_q32;
            beat->ordinal = ordinal;
            beat->confidence = segment->confidence;
            beat->flags = segment->flags;
            beat->revision = segment->revision;
            beat_count += 1u;
            segment_beats += 1u;
            ordinal += 1;
            if (position_q32 > UINT64_MAX - period_q32) {
                break;
            }
            position_q32 += period_q32;
        }
        segment->beat_count = segment_beats;
    }
    state->beat_count = beat_count;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_s6_prepare(apta_session_t *session)
{
    apta_internal_s6_session_state_t *state;
    size_t bins_bytes;
    size_t beats_bytes;
    size_t flux_bytes;

    if (!apta_s6_enabled(session) || session->s6 != NULL) {
        return APTA_STATUS_OK;
    }
    state = (apta_internal_s6_session_state_t *)apta_internal_session_allocate(
        session,
        sizeof(*state),
        alignof(apta_internal_s6_session_state_t),
        APTA_MEMORY_PERSISTENT);
    if (state == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(state, 0, sizeof(*state));

    bins_bytes = (size_t)APTA_INTERNAL_GLOBAL_BIN_CAPACITY *
                 sizeof(apta_internal_onset_bin_t);
    state->global_bins =
        (apta_internal_onset_bin_t *)apta_internal_session_allocate(
            session,
            bins_bytes,
            alignof(apta_internal_onset_bin_t),
            APTA_MEMORY_PERSISTENT);
    flux_bytes = (size_t)APTA_INTERNAL_GLOBAL_BIN_CAPACITY * sizeof(float);
    state->global_flux = (float *)apta_internal_session_allocate(
        session,
        flux_bytes,
        alignof(float),
        APTA_MEMORY_PERSISTENT);
    beats_bytes = (size_t)APTA_INTERNAL_GLOBAL_MAX_BEATS * sizeof(apta_beat_t);
    state->beats = (apta_beat_t *)apta_internal_session_allocate(
        session,
        beats_bytes,
        alignof(apta_beat_t),
        APTA_MEMORY_PERSISTENT);
    if (state->global_bins == NULL || state->beats == NULL ||
        state->global_flux == NULL) {
        apta_internal_session_deallocate(session, state->beats);
        apta_internal_session_deallocate(session, state->global_flux);
        apta_internal_session_deallocate(session, state->global_bins);
        apta_internal_session_deallocate(session, state);
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(state->global_bins, 0, bins_bytes);
    memset(state->global_flux, 0, flux_bytes);
    memset(state->beats, 0, beats_bytes);
    state->global_bin_capacity = APTA_INTERNAL_GLOBAL_BIN_CAPACITY;
    state->global_flux_capacity = APTA_INTERNAL_GLOBAL_BIN_CAPACITY;
    state->beat_capacity = APTA_INTERNAL_GLOBAL_MAX_BEATS;
    apta_grid_revision_view_init(&state->revision);
    session->s6 = state;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_s6_process_sample(
    apta_session_t *session,
    apta_source_frame_t source_frame,
    float sample)
{
    apta_internal_s6_session_state_t *state;
    apta_internal_onset_bin_t *bin;
    uint64_t bin_index;
    float magnitude;

    if (!apta_s6_enabled(session)) {
        return APTA_STATUS_OK;
    }
    state = session->s6;
    if (state == NULL) {
        return APTA_ERROR_INTERNAL;
    }
    bin_index = source_frame / APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN;
    bin = &state->global_bins[(uint32_t)(bin_index % state->global_bin_capacity)];
    if (!bin->occupied || bin->bin_index != bin_index) {
        memset(bin, 0, sizeof(*bin));
        bin->occupied = 1u;
        bin->bin_index = bin_index;
    }
    if (bin->sample_count == UINT32_MAX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    /* A3: branchless clamp, as in S4. */
    magnitude = fminf(fabsf(sample), 1.0f);
    bin->sum_absolute +=
        (uint32_t)(magnitude * APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE);
    bin->sample_count += 1u;
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_s6_refresh(apta_session_t *session)
{
    apta_internal_s6_session_state_t *state;
    /* C3: stack array, bounded and asserted by APTA_INTERNAL_GLOBAL_MAX_WINDOWS
     * in apta_internal.h. This runs inside process(), so its size is part of
     * the host's task stack budget. */
    apta_s6_window_t windows[APTA_INTERNAL_GLOBAL_MAX_WINDOWS];
    uint32_t window_count = 0u;
    uint32_t segment_window_counts[APTA_INTERNAL_GLOBAL_MAX_SEGMENTS] = {0u};
    uint64_t evidence_first;
    uint64_t evidence_end;
    uint64_t cursor;
    uint64_t old_signature;
    uint64_t new_signature;
    uint64_t old_mutation;
    apta_feature_state_t old_state;
    apta_grid_revision_state_t old_revision_state;
    uint32_t index;
    uint32_t total_confidence = 0u;
    uint32_t valid_confidence_count = 0u;
    int degraded = 0;
    apta_status_t status;

    if (!apta_s6_enabled(session) || session->s6 == NULL) {
        return APTA_STATUS_OK;
    }
    state = session->s6;
    if (!apta_s6_find_evidence(session, &evidence_first, &evidence_end) ||
        evidence_end - evidence_first < APTA_INTERNAL_GLOBAL_MIN_BINS) {
        return APTA_STATUS_OK;
    }

    /* A2: as in S4, skip the per-window autocorrelation when too little new
     * evidence has arrived. Draining and completed sessions always refresh so
     * the grid can reach its final state. */
    if (evidence_end < state->refreshed_evidence_end) {
        state->refreshed_evidence_end = 0u;
    } else if (!session->end_of_input_signalled &&
               atomic_load_explicit(&session->state, memory_order_acquire) !=
                   APTA_SESSION_COMPLETED &&
               evidence_end < state->refreshed_evidence_end +
                                  APTA_INTERNAL_S6_REFRESH_MIN_NEW_BINS) {
        return APTA_STATUS_OK;
    }
    state->refreshed_evidence_end = evidence_end;

    old_signature = state->signature;
    old_state = state->state;
    old_revision_state = state->revision.state;
    old_mutation = state->mutation_serial;
    state->segment_count = 0u;
    state->beat_count = 0u;
    state->flags = 0u;

    /* A1: fill the flux array once for the whole evidence range rather than
     * once per window. Flux depends on the window start only at the window's
     * first bin, where the predecessor is treated as absent, so each window
     * needs a single boundary patch below instead of its own fill. */
    if (state->global_flux == NULL ||
        evidence_end - evidence_first > (uint64_t)state->global_flux_capacity) {
        return APTA_STATUS_OK;
    }
    for (cursor = evidence_first; cursor < evidence_end; ++cursor) {
        state->global_flux[(uint32_t)(cursor - evidence_first)] =
            (float)apta_s6_flux_uncached(state, cursor, evidence_first);
    }
    state->flux_base_bin = evidence_first;

    for (cursor = evidence_first; cursor < evidence_end;) {
        uint64_t window_end = cursor + APTA_INTERNAL_GLOBAL_WINDOW_BINS;
        if (window_end > evidence_end) {
            window_end = evidence_end;
        }
        /* Boundary patch: for this window the bin at `cursor` has no
         * predecessor. Windows are disjoint and contiguous, so this slot is
         * read only by the window that starts on it and never needs
         * restoring. */
        state->global_flux[(uint32_t)(cursor - state->flux_base_bin)] =
            (float)apta_s6_flux_uncached(state, cursor, cursor);
        if (window_count < sizeof(windows) / sizeof(windows[0]) &&
            apta_s6_estimate_window(
                session,
                cursor,
                window_end,
                &windows[window_count])) {
            window_count += 1u;
        }
        cursor = window_end;
    }

    if (window_count == 0u && session->has_tempo) {
        memset(&windows[0], 0, sizeof(windows[0]));
        windows[0].first_bin = evidence_first;
        windows[0].end_bin = evidence_end;
        windows[0].tempo_millibpm = session->tempo_value.tempo_millibpm;
        windows[0].phase_bins = 0u;
        windows[0].confidence = session->tempo_value.confidence;
        windows[0].score = 0.05f;
        window_count = 1u;
        degraded = 1;
    }
    if (window_count == 0u) {
        return APTA_STATUS_OK;
    }

    for (index = 0u; index < window_count; ++index) {
        const apta_s6_window_t *window = &windows[index];
        apta_grid_segment_t *segment;
        uint32_t difference = UINT32_MAX;

        if (state->segment_count != 0u) {
            const uint32_t previous_tempo =
                state->segments[state->segment_count - 1u].nominal_tempo_millibpm;
            difference = previous_tempo > window->tempo_millibpm
                             ? previous_tempo - window->tempo_millibpm
                             : window->tempo_millibpm - previous_tempo;
        }
        if (state->segment_count == 0u || difference > 1500u) {
            if (state->segment_count >= APTA_INTERNAL_GLOBAL_MAX_SEGMENTS) {
                segment = &state->segments[state->segment_count - 1u];
                segment->applicability_range.end_frame =
                    window->end_bin * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN;
                segment->flags |= APTA_GRID_FLAG_DEGRADED;
                degraded = 1;
                continue;
            }
            segment = &state->segments[state->segment_count];
            memset(segment, 0, sizeof(*segment));
            segment->struct_size = (uint32_t)sizeof(*segment);
            segment->api_version = APTA_API_VERSION;
            apta_s6_init_range(
                &segment->applicability_range,
                window->first_bin * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN,
                window->end_bin * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN);
            segment->anchor_position.whole_frame =
                (window->first_bin + window->phase_bins) *
                APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN;
            segment->nominal_tempo_millibpm = window->tempo_millibpm;
            segment->confidence = window->confidence;
            segment->state = APTA_FEATURE_PROVISIONAL;
            segment->segment_id = state->segment_count + 1u;
            segment_window_counts[state->segment_count] = 1u;
            state->segment_count += 1u;
        } else {
            const uint32_t segment_index = state->segment_count - 1u;
            const uint32_t count = segment_window_counts[segment_index];
            segment = &state->segments[segment_index];
            segment->nominal_tempo_millibpm =
                (uint32_t)(((uint64_t)segment->nominal_tempo_millibpm * count +
                            window->tempo_millibpm) /
                           (count + 1u));
            segment->confidence = (apta_confidence_value_t)(
                ((uint32_t)segment->confidence * count + window->confidence) /
                (count + 1u));
            segment->applicability_range.end_frame =
                window->end_bin * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN;
            segment_window_counts[segment_index] = count + 1u;
        }
        total_confidence += window->confidence;
        valid_confidence_count += 1u;
    }

    if (state->segment_count == 0u) {
        return APTA_STATUS_OK;
    }

    state->confidence = valid_confidence_count != 0u
                            ? (apta_confidence_value_t)(
                                  total_confidence / valid_confidence_count)
                            : APTA_CONFIDENCE_UNKNOWN;
    state->state = evidence_end - evidence_first >=
                           APTA_INTERNAL_GLOBAL_STABLE_BINS &&
                       state->confidence >= 50u
                       ? APTA_FEATURE_STABLE
                       : APTA_FEATURE_PROVISIONAL;
    if (session->end_of_input_signalled && evidence_first == 0u &&
        evidence_end * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN >=
            session->final_end_frame &&
        atomic_load_explicit(&session->state, memory_order_acquire) ==
            APTA_SESSION_COMPLETED) {
        state->state = APTA_FEATURE_FINAL;
    }

    for (index = 0u; index < state->segment_count; ++index) {
        apta_grid_segment_t *segment = &state->segments[index];
        if (index + 1u == state->segment_count &&
            session->end_of_input_signalled) {
            segment->applicability_range.end_frame = apta_s6_min_frame(
                segment->applicability_range.end_frame,
                session->final_end_frame);
        }
        segment->state = state->state;
        if (state->segment_count > 1u) {
            segment->flags |= APTA_GRID_FLAG_DYNAMIC_TEMPO;
        }
        if (degraded) {
            segment->flags |= APTA_GRID_FLAG_DEGRADED;
        }
        apta_s6_period_from_tempo(
            session,
            segment->nominal_tempo_millibpm,
            &segment->frames_per_beat);
        segment->beat_count = apta_s6_beat_count(segment);
    }

    state->has_dynamic_tempo = state->segment_count > 1u ? 1u : 0u;
    if (state->has_dynamic_tempo) {
        state->flags |= APTA_GRID_FLAG_DYNAMIC_TEMPO;
    }
    if (degraded) {
        state->flags |= APTA_GRID_FLAG_DEGRADED;
    }
    state->representation =
        ((session->config.requested_features & APTA_FEATURE_DYNAMIC_TEMPO) != 0u ||
         state->has_dynamic_tempo)
            ? APTA_GRID_REPRESENTATION_HYBRID
            : APTA_GRID_REPRESENTATION_SEGMENTS;

    apta_s6_init_range(
        &state->requested_range,
        0u,
        session->config.total_frames != APTA_TOTAL_FRAMES_UNKNOWN
            ? session->config.total_frames
            : evidence_end * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN);
    apta_s6_init_range(
        &state->evidence_range,
        evidence_first * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN,
        session->end_of_input_signalled
            ? apta_s6_min_frame(
                  evidence_end * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN,
                  session->final_end_frame)
            : evidence_end * APTA_INTERNAL_GLOBAL_FRAMES_PER_BIN);
    state->applicability_range = state->evidence_range;
    state->coverage_range = state->evidence_range;

    if (state->representation != APTA_GRID_REPRESENTATION_SEGMENTS) {
        status = apta_s6_generate_beats(state);
        if (status < 0) {
            return status;
        }
    }

    new_signature = apta_s6_signature(state);
    if (state->revision_id == 0u) {
        state->revision_id = 1u;
        state->previous_revision_id = 0u;
    } else if (new_signature != old_signature) {
        state->previous_revision_id = state->revision_id;
        state->revision_id += 1u;
        if (state->revision_id == 0u) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
    }
    for (index = 0u; index < state->segment_count; ++index) {
        state->segments[index].revision = state->revision_id;
    }
    for (index = 0u; index < state->beat_count; ++index) {
        state->beats[index].revision = state->revision_id;
    }

    apta_grid_revision_view_init(&state->revision);
    state->revision.revision_id = state->revision_id;
    state->revision.previous_revision_id = state->previous_revision_id;
    state->revision.confidence = state->confidence;
    state->revision.affected_range = state->applicability_range;
    state->revision.proposed_representation = state->representation;
    state->revision.proposed_segment_count = state->segment_count;
    state->revision.proposed_beat_count = state->beat_count;
    if (state->has_dynamic_tempo) {
        state->revision.flags |= APTA_GRID_REVISION_FLAG_DYNAMIC_TEMPO;
    }
    if (degraded) {
        state->revision.flags |= APTA_GRID_REVISION_FLAG_DEGRADED;
    }
    if (apta_s6_locked_conflict(session)) {
        state->revision.state = APTA_GRID_REVISION_PENDING;
        state->revision.flags |=
            APTA_GRID_REVISION_FLAG_CONFLICTS_LOCKED_RANGE;
        state->revision.affected_range =
            session->local_grid_segment.applicability_range;
        state->revision_pending = 1u;
    } else {
        state->revision.state = APTA_GRID_REVISION_APPLIED;
        state->revision_pending = 0u;
    }

    state->signature = new_signature;
    state->has_global_grid = 1u;
    if (new_signature != old_signature || old_state != state->state ||
        old_revision_state != state->revision.state || old_mutation == 0u) {
        state->mutation_serial += 1u;
    }
    return APTA_STATUS_OK;
}

apta_feature_mask_t apta_internal_s6_pending_features(
    const apta_session_t *session)
{
    apta_feature_mask_t features = 0u;
    const apta_internal_s6_session_state_t *state;

    if (session == NULL || session->s6 == NULL) {
        return 0u;
    }
    state = session->s6;
    if (state->mutation_serial == state->published_serial ||
        !state->has_global_grid) {
        return 0u;
    }
    features |= APTA_FEATURE_GLOBAL_BEATGRID;
    if ((session->config.requested_features & APTA_FEATURE_DYNAMIC_TEMPO) != 0u) {
        features |= APTA_FEATURE_DYNAMIC_TEMPO;
    }
    if ((session->config.requested_features & APTA_FEATURE_CONFIDENCE) != 0u) {
        features |= APTA_FEATURE_CONFIDENCE;
    }
    if (state->revision_pending &&
        (session->config.requested_features & APTA_FEATURE_GRID_LOCKING) != 0u) {
        features |= APTA_FEATURE_GRID_LOCKING;
    }
    return features;
}

void apta_internal_s6_mark_published(apta_session_t *session)
{
    if (session != NULL && session->s6 != NULL) {
        session->s6->published_serial = session->s6->mutation_serial;
    }
}

apta_status_t apta_internal_s6_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    apta_internal_s6_result_state_t *snapshot;
    const apta_internal_s6_session_state_t *state;
    size_t segments_bytes;
    size_t beats_bytes;

    if (session == NULL || result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (session->s6 == NULL || !session->s6->has_global_grid) {
        return APTA_STATUS_OK;
    }
    state = session->s6;
    snapshot = (apta_internal_s6_result_state_t *)apta_internal_context_allocate(
        session->context,
        sizeof(*snapshot),
        alignof(apta_internal_s6_result_state_t),
        APTA_MEMORY_PERSISTENT);
    if (snapshot == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    result->s6 = snapshot;

    snapshot->coverage_ranges =
        (apta_frame_range_t *)apta_internal_context_allocate(
            session->context,
            sizeof(apta_frame_range_t),
            alignof(apta_frame_range_t),
            APTA_MEMORY_PERSISTENT);
    segments_bytes = (size_t)state->segment_count * sizeof(apta_grid_segment_t);
    beats_bytes = (size_t)state->beat_count * sizeof(apta_beat_t);
    snapshot->segments = state->segment_count != 0u
                             ? (apta_grid_segment_t *)apta_internal_context_allocate(
                                   session->context,
                                   segments_bytes,
                                   alignof(apta_grid_segment_t),
                                   APTA_MEMORY_PERSISTENT)
                             : NULL;
    snapshot->beats = state->beat_count != 0u
                          ? (apta_beat_t *)apta_internal_context_allocate(
                                session->context,
                                beats_bytes,
                                alignof(apta_beat_t),
                                APTA_MEMORY_PERSISTENT)
                          : NULL;
    if (snapshot->coverage_ranges == NULL ||
        (state->segment_count != 0u && snapshot->segments == NULL) ||
        (state->beat_count != 0u && snapshot->beats == NULL)) {
        apta_internal_s6_cleanup_result(result);
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    *snapshot->coverage_ranges = state->coverage_range;
    if (segments_bytes != 0u) {
        memcpy(snapshot->segments, state->segments, segments_bytes);
    }
    if (beats_bytes != 0u) {
        memcpy(snapshot->beats, state->beats, beats_bytes);
    }

    apta_grid_view_init(&snapshot->global_grid);
    snapshot->global_grid.requested_range = state->requested_range;
    snapshot->global_grid.evidence_range = state->evidence_range;
    snapshot->global_grid.applicability_range = state->applicability_range;
    snapshot->global_grid.representation = state->representation;
    snapshot->global_grid.state = state->state;
    snapshot->global_grid.confidence = state->confidence;
    snapshot->global_grid.coverage_range_count = 1u;
    snapshot->global_grid.coverage_ranges = snapshot->coverage_ranges;
    snapshot->global_grid.segment_count = state->segment_count;
    snapshot->global_grid.segments = snapshot->segments;
    snapshot->global_grid.beat_count = state->beat_count;
    snapshot->global_grid.beats = snapshot->beats;
    snapshot->global_grid.flags = state->flags;
    snapshot->revision = state->revision;

    result->info.available_features |= APTA_FEATURE_GLOBAL_BEATGRID;
    if ((session->config.requested_features & APTA_FEATURE_DYNAMIC_TEMPO) != 0u) {
        result->info.available_features |= APTA_FEATURE_DYNAMIC_TEMPO;
    }
    if ((session->config.requested_features & APTA_FEATURE_CONFIDENCE) != 0u) {
        result->info.available_features |= APTA_FEATURE_CONFIDENCE;
    }
    return APTA_STATUS_OK;
}

void apta_internal_s6_cleanup_session(apta_session_t *session)
{
    if (session == NULL || session->s6 == NULL) {
        return;
    }
    apta_internal_session_deallocate(session, session->s6->beats);
    apta_internal_session_deallocate(session, session->s6->global_flux);
    apta_internal_session_deallocate(session, session->s6->global_bins);
    apta_internal_session_deallocate(session, session->s6);
    session->s6 = NULL;
}

void apta_internal_s6_cleanup_result(apta_result_t *result)
{
    if (result == NULL || result->s6 == NULL) {
        return;
    }
    apta_internal_context_deallocate(result->context, result->s6->beats);
    apta_internal_context_deallocate(result->context, result->s6->segments);
    apta_internal_context_deallocate(result->context, result->s6->coverage_ranges);
    apta_internal_context_deallocate(result->context, result->s6);
    result->s6 = NULL;
}

apta_status_t APTA_CALL apta_session_apply_grid_revision(
    apta_session_t *session,
    uint32_t revision_id)
{
    apta_internal_s6_session_state_t *state;
    uint32_t index;
    apta_grid_segment_t *selected = NULL;
    apta_status_t status;

    if (session == NULL || revision_id == 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    state = session->s6;
    if (state == NULL || !state->revision_pending ||
        state->revision.state != APTA_GRID_REVISION_PENDING) {
        return APTA_ERROR_INVALID_STATE;
    }
    if (revision_id != state->revision_id) {
        return APTA_ERROR_CONFLICT;
    }
    for (index = 0u; index < state->segment_count; ++index) {
        apta_grid_segment_t *candidate = &state->segments[index];
        if (candidate->applicability_range.first_frame <
                session->local_grid_segment.applicability_range.end_frame &&
            session->local_grid_segment.applicability_range.first_frame <
                candidate->applicability_range.end_frame) {
            selected = candidate;
            break;
        }
    }
    if (selected == NULL) {
        return APTA_ERROR_CONFLICT;
    }

    session->local_grid_segment.anchor_position = selected->anchor_position;
    session->local_grid_segment.anchor_ordinal = selected->anchor_ordinal;
    session->local_grid_segment.frames_per_beat = selected->frames_per_beat;
    session->local_grid_segment.nominal_tempo_millibpm =
        selected->nominal_tempo_millibpm;
    session->local_grid_segment.confidence = selected->confidence;
    session->local_grid_segment.revision = revision_id;
    session->local_grid_segment.flags =
        selected->flags | APTA_GRID_FLAG_LOCKED;
    session->local_grid_segment.beat_count =
        apta_s6_beat_count(&session->local_grid_segment);
    session->tempo_value.tempo_millibpm = selected->nominal_tempo_millibpm;
    session->tempo_value.confidence = selected->confidence;
    session->tempo_value.flags |=
        state->has_dynamic_tempo ? APTA_TEMPO_FLAG_DYNAMIC : 0u;
    session->s4_mutation_serial += 1u;

    state->revision.state = APTA_GRID_REVISION_APPLIED;
    state->revision_pending = 0u;
    state->mutation_serial += 1u;
    status = apta_internal_publish_result(
        session,
        APTA_FEATURE_LOCAL_BEATGRID |
            APTA_FEATURE_GLOBAL_BEATGRID |
            APTA_FEATURE_GRID_LOCKING |
            ((session->config.requested_features & APTA_FEATURE_DYNAMIC_TEMPO) != 0u
                 ? APTA_FEATURE_DYNAMIC_TEMPO
                 : 0u));
    if (status < 0) {
        return status;
    }
    apta_internal_s4_mark_published(session);
    apta_internal_s6_mark_published(session);
    return APTA_STATUS_OK;
}
