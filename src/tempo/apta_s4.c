// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_period_refine.h"
#include "../core/apta_session_workspace.h"
#include "../core/apta_tempo_ensemble.h"
#include "../core/apta_tempo_prior.h"
#include "../core/apta_tempo_relation.h"
#include "../confidence/apta_quality_model.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#ifdef APTA_INTERNAL_PROFILE_S4
static uint64_t apta_s4_profile_now(const apta_session_t *session)
{
    if (session == NULL || session->context == NULL ||
        session->context->clock.monotonic_time_ns == NULL) {
        return 0u;
    }
    return session->context->clock.monotonic_time_ns(
        session->context->clock.user_data);
}

static uint64_t apta_s4_profile_mark(
    const apta_session_t *session,
    uint64_t *accumulator,
    uint64_t started_at)
{
    uint64_t finished_at = apta_s4_profile_now(session);

    if (accumulator != NULL && started_at != 0u && finished_at >= started_at) {
        *accumulator += finished_at - started_at;
    }
    return finished_at;
}

void apta_internal_s4_profile_reset(apta_session_t *session)
{
    if (session != NULL) {
        memset(&session->s4_profile, 0, sizeof(session->s4_profile));
    }
}

void apta_internal_s4_profile_snapshot(
    const apta_session_t *session,
    apta_internal_s4_profile_t *profile_out)
{
    if (profile_out == NULL) {
        return;
    }
    if (session == NULL) {
        memset(profile_out, 0, sizeof(*profile_out));
        return;
    }
    *profile_out = session->s4_profile;
}

#define APTA_S4_PROFILE_DECLARE uint64_t apta_s4_profile_started_at = 0u
#define APTA_S4_PROFILE_BEGIN(session)                                      \
    do {                                                                    \
        apta_s4_profile_started_at = apta_s4_profile_now(session);           \
    } while (0)
#define APTA_S4_PROFILE_ADD(session, field)                                 \
    do {                                                                    \
        apta_s4_profile_started_at = apta_s4_profile_mark(                  \
            session, &(session)->s4_profile.field,                          \
            apta_s4_profile_started_at);                                    \
    } while (0)
#else
#define APTA_S4_PROFILE_DECLARE
#define APTA_S4_PROFILE_BEGIN(session) ((void)(session))
#define APTA_S4_PROFILE_ADD(session, field) ((void)(session))
#endif

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

/* A3/B3: recover normalized mean magnitude from the integer accumulator. */
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
static float apta_s4_band_energy(
    const apta_session_t *session,
    uint64_t bin_index,
    uint32_t band)
{
    const apta_internal_onset_bin_t *bin =
        apta_s4_const_bin(session, bin_index);

    return bin != NULL && bin->sample_count != 0u
               ? (float)bin->sums.multiband.band_sums[band] /
                     ((float)bin->sample_count *
                      (float)APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE)
               : 0.0f;
}

static float apta_s4_broadband_energy(
    const apta_session_t *session,
    uint64_t bin_index)
{
    const apta_internal_onset_bin_t *bin =
        apta_s4_const_bin(session, bin_index);

    return bin != NULL && bin->sample_count != 0u
               ? (float)bin->sums.multiband.broadband_sum /
                     ((float)bin->sample_count *
                      (float)APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE)
               : 0.0f;
}
#else
static float apta_s4_energy(
    const apta_session_t *session,
    uint64_t bin_index)
{
    const apta_internal_onset_bin_t *bin =
        apta_s4_const_bin(session, bin_index);

    return bin != NULL && bin->sample_count != 0u
               ? (float)bin->sum_absolute /
                     ((float)bin->sample_count *
                      APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE)
               : 0.0f;
}
#endif

/* A1: the single definition of the flux computation. Called once per bin by
 * the fill loop in apta_internal_s4_refresh(); the lag and phase loops read
 * session->onset_flux instead of calling this. */
static float apta_s4_flux_uncached(
    const apta_session_t *session,
    uint64_t bin_index,
    uint64_t evidence_first)
{
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
    static const float weights[APTA_INTERNAL_BAND_COUNT] = {
        APTA_INTERNAL_ONSET_WEIGHT_LOW,
        APTA_INTERNAL_ONSET_WEIGHT_MID,
        APTA_INTERNAL_ONSET_WEIGHT_HIGH};
    float novelty = 0.0f;
    float current_total = 0.0f;
    float previous_total = 0.0f;
    const float broadband_current =
        apta_s4_broadband_energy(session, bin_index);
    const float broadband_previous =
        bin_index > evidence_first
            ? apta_s4_broadband_energy(session, bin_index - 1u)
            : 0.0f;
    const float broadband_rise =
        broadband_current > broadband_previous
            ? broadband_current - broadband_previous
            : 0.0f;
    uint32_t band;

    for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
        const float current = apta_s4_band_energy(session, bin_index, band);
        const float previous =
            bin_index > evidence_first
                ? apta_s4_band_energy(session, bin_index - 1u, band)
                : 0.0f;
        const float rise = current > previous ? current - previous : 0.0f;

        novelty += weights[band] * rise;
        current_total += current;
        previous_total += previous;
    }
    /* A filter impulse redistributes energy from high to mid to low as its
     * tails decay. Per-band rectification alone mistakes those crossings for
     * fresh onsets. Require the aggregate band envelope to rise too, while
     * retaining per-band flux as the novelty magnitude. */
    return broadband_rise +
           (current_total > previous_total
                ? APTA_INTERNAL_ONSET_MULTIBAND_MIX * novelty
                : 0.0f);
#else
    float current = apta_s4_energy(session, bin_index);
    float previous = bin_index > evidence_first
                         ? apta_s4_energy(session, bin_index - 1u)
                         : 0.0f;
    return current > previous ? current - previous : 0.0f;
#endif
}

/*
 * Grid fit: does the selected grid actually explain the onsets?
 *
 * The existing confidence terms are all properties of the autocorrelation --
 * how strong the peak is, how far ahead of the runner-up, how far ahead of its
 * octave siblings. None of them asks whether the beats the grid predicts are
 * where the onsets actually are, and measurement showed none of them separates
 * correct answers from incorrect ones once timing is not perfectly quantized.
 *
 * This compares the novelty at predicted beat positions against the novelty
 * everywhere else, as a contrast in [-1, 1]:
 *
 *     fit = (on_beat_mean - off_beat_mean) / (on_beat_mean + off_beat_mean)
 *
 * It is two-sided by construction. A grid that is too fast predicts beats
 * where nothing happens, which lowers on_beat_mean. A grid that is too slow
 * leaves real onsets between its beats, which raises off_beat_mean. Both push
 * the contrast down.
 *
 * The on-beat window is one bin either side. That is deliberate: an onset bin
 * is 5.8 ms at 44.1 kHz and human timing error is of the same order, so a
 * zero-width window would measure quantization rather than correctness.
 *
 * It is a ratio of means within one signal, so a uniformly weaker novelty --
 * which is what jitter produces -- does not move it the way it moves the
 * absolute correlation score.
 */
static float apta_s4_grid_fit(
    const apta_session_t *session,
    uint64_t evidence_first,
    uint64_t evidence_end,
    uint32_t lag,
    uint32_t phase)
{
    const float *flux = session->onset_flux;
    const uint32_t span = (uint32_t)(evidence_end - evidence_first);
    float on_sum = 0.0f;
    float off_sum = 0.0f;
    uint32_t on_count = 0u;
    uint32_t off_count = 0u;
    uint32_t offset;

    if (lag == 0u || span == 0u) {
        return 0.0f;
    }

    for (offset = 0u; offset < span; ++offset) {
        /* Distance to the nearest predicted beat, in bins. */
        const uint32_t from_phase = (offset + lag - (phase % lag)) % lag;
        const uint32_t distance =
            from_phase < lag - from_phase ? from_phase : lag - from_phase;

        if (distance <= 1u) {
            on_sum += flux[offset];
            on_count += 1u;
        } else {
            off_sum += flux[offset];
            off_count += 1u;
        }
    }

    if (on_count == 0u || off_count == 0u) {
        return 0.0f;
    }
    {
        const float on_mean = on_sum / (float)on_count;
        const float off_mean = off_sum / (float)off_count;
        const float sum = on_mean + off_mean;

        if (sum <= 1e-12f) {
            return 0.0f;
        }
        return (on_mean - off_mean) / sum;
    }
}

/* B1: log-normal weight for a tempo, peaking at the configured centre. */
#define apta_s4_tempo_prior apta_internal_tempo_prior

/* B1: normalized autocorrelation of the precomputed flux at one lag. Extracted
 * so the octave-family scan below can evaluate related lags without repeating
 * the loop body. The arithmetic is shared with S6 -- see apta_period_refine.h;
 * this wrapper only maps the evidence run onto the slice the shared function
 * takes. */
static float apta_s4_correlation_at_lag(
    const apta_session_t *session,
    uint64_t evidence_first,
    uint64_t evidence_end,
    uint32_t lag)
{
    float score;

    if (lag == 0u || (uint64_t)lag >= evidence_end - evidence_first) {
        return 0.0f;
    }
    score = apta_internal_correlation_at_lag(
        session->onset_flux,
        (uint32_t)(evidence_end - evidence_first),
        lag);
    return score > 0.0f ? score : 0.0f;
}

static int apta_s4_find_evidence(
    apta_session_t *session,
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

    if (!session->end_of_input_signalled && session->s4_evidence_valid &&
        !session->s4_evidence_dirty) {
        *first_out = session->s4_evidence_first;
        *end_out = session->s4_evidence_end;
#ifdef APTA_INTERNAL_PROFILE_S4
        session->s4_profile.evidence_cache_hits += 1u;
#endif
        return 1;
    }

#ifdef APTA_INTERNAL_PROFILE_S4
    session->s4_profile.evidence_full_scans += 1u;
#endif

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
        session->s4_evidence_valid = 0u;
        session->s4_evidence_dirty = 0u;
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
        session->s4_evidence_valid = 0u;
        session->s4_evidence_dirty = 0u;
        return 0;
    }
    session->s4_evidence_first = best_first;
    session->s4_evidence_end = best_end;
    session->s4_evidence_valid = 1u;
    session->s4_evidence_dirty = 0u;
    *first_out = best_first;
    *end_out = best_end;
    return 1;
}

static void apta_s4_note_bin_replaced(
    apta_session_t *session,
    uint64_t replaced_index)
{
    if (!session->s4_evidence_valid || session->s4_evidence_dirty ||
        replaced_index < session->s4_evidence_first ||
        replaced_index >= session->s4_evidence_end) {
        return;
    }
    if (replaced_index == session->s4_evidence_first) {
        session->s4_evidence_first += 1u;
        if (session->s4_evidence_first == session->s4_evidence_end) {
            session->s4_evidence_valid = 0u;
        }
        return;
    }
    session->s4_evidence_dirty = 1u;
}

static void apta_s4_note_bin_complete(
    apta_session_t *session,
    uint64_t completed_index)
{
    if (session->s4_evidence_dirty) {
        return;
    }
    if (!session->s4_evidence_valid) {
        session->s4_evidence_first = completed_index;
        session->s4_evidence_end = completed_index + 1u;
        session->s4_evidence_valid = 1u;
        return;
    }
    if (completed_index == session->s4_evidence_end) {
        session->s4_evidence_end += 1u;
    } else if (completed_index + 1u == session->s4_evidence_first) {
        session->s4_evidence_first = completed_index;
    } else if (completed_index < session->s4_evidence_first ||
               completed_index >= session->s4_evidence_end) {
        /* A disjoint complete run may be longer than the cached one. Rebuild
         * conservatively on the next refresh rather than maintaining a second
         * interval in the hot per-sample state. */
        session->s4_evidence_dirty = 1u;
    }
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

/* Sub-bin period refinement. The mechanism, and the parabola that was tried
 * and rejected before it, are described in apta_period_refine.h. */
static float apta_s4_lag_offset(
    const apta_session_t *session,
    uint64_t evidence_first,
    uint64_t evidence_end,
    uint32_t lag)
{
    return apta_internal_refine_lag(
        session->onset_flux,
        (uint32_t)(evidence_end - evidence_first),
        lag,
        APTA_INTERNAL_TEMPO_REFINE_MAX_BEATS);
}

static uint32_t apta_s4_tempo_from_refined_lag(
    const apta_session_t *session,
    uint32_t lag,
    float offset)
{
    return apta_internal_tempo_with_offset(
        apta_s4_tempo_from_lag(session, lag), lag, offset);
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

static void apta_s4_update_cached_ranges(apta_session_t *session)
{
    apta_source_frame_t requested_first;
    apta_source_frame_t requested_end;
    apta_source_frame_t applicability_first;
    apta_source_frame_t applicability_end;
    const apta_source_frame_t evidence_first =
        session->tempo_value.evidence_range.first_frame;
    const apta_source_frame_t evidence_end =
        session->tempo_value.evidence_range.end_frame;

    if (!session->has_tempo || !session->has_local_grid) {
        return;
    }
    requested_first = evidence_first;
    requested_end = evidence_end;
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
    applicability_first = apta_s4_max_frame(requested_first, evidence_first);
    applicability_end = apta_s4_min_frame(requested_end, evidence_end);
    if (applicability_end <= applicability_first) {
        applicability_first = evidence_first;
        applicability_end = evidence_end;
    }

    apta_s4_init_range(&session->tempo_value.applicability_range,
                       applicability_first,
                       applicability_end);
    apta_s4_init_range(&session->local_grid_segment.applicability_range,
                       applicability_first,
                       applicability_end);
    session->local_grid_segment.beat_count =
        apta_s4_beat_count(&session->local_grid_segment);
    apta_s4_init_range(&session->local_grid_requested_range,
                       requested_first,
                       requested_end);
    session->local_grid_evidence_range = session->tempo_value.evidence_range;
    apta_s4_init_range(&session->local_grid_applicability_range,
                       applicability_first,
                       applicability_end);
    session->local_grid_coverage_range =
        session->local_grid_applicability_range;
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
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
    const float bands[APTA_INTERNAL_BAND_COUNT])
#else
    float sample)
#endif
{
    uint64_t bin_index;
    uint32_t slot;
    apta_internal_onset_bin_t *bin;
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
    uint32_t band;
#else
    float magnitude;
#endif

    if (!apta_s4_enabled(session)) {
        return APTA_STATUS_OK;
    }
    if (session->onset_bins == NULL) {
        return APTA_ERROR_INTERNAL;
    }

    bin_index = source_frame / APTA_INTERNAL_ONSET_FRAMES_PER_BIN;
    /* The bin identity is stored in 32 bits. Beyond that a bin would alias an
     * earlier one and the ring would report evidence it does not hold. */
    if (bin_index > APTA_INTERNAL_MAX_BIN_INDEX) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    slot = (uint32_t)(bin_index % session->onset_bin_capacity);
    bin = &session->onset_bins[slot];
    if (!bin->occupied || bin->bin_index != (uint32_t)bin_index) {
        if (bin->occupied) {
            apta_s4_note_bin_replaced(session, bin->bin_index);
        }
        memset(bin, 0, sizeof(*bin));
        bin->occupied = 1u;
        bin->bin_index = (uint32_t)bin_index;
    }
    if (bin->sample_count ==
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
        UINT16_MAX
#else
        UINT32_MAX
#endif
    ) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    /* A3: apta_read_channel_sample() already returns a finite value in
     * [-1, 1], but the conversion below must not depend on a caller invariant
     * for its defined behaviour. fminf is branchless, so the guard costs one
     * instruction rather than a per-sample branch. */
#ifdef APTA_INTERNAL_MULTIBAND_ONSET
    const float sample = bands[0] + bands[1] + bands[2];
    const float broadband_magnitude = fminf(fabsf(sample), 1.0f);

    for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
        const float magnitude = fminf(fabsf(bands[band]), 1.0f);
        bin->sums.multiband.band_sums[band] = (uint16_t)(
            bin->sums.multiband.band_sums[band] +
            (uint16_t)(magnitude *
                       (float)APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE));
    }
    bin->sums.multiband.broadband_sum = (uint16_t)(
        bin->sums.multiband.broadband_sum +
        (uint16_t)(broadband_magnitude *
                   (float)APTA_INTERNAL_S4_BAND_MAGNITUDE_SCALE));
#else
    magnitude = fminf(fabsf(sample), 1.0f);
    bin->sum_absolute +=
        (uint32_t)(magnitude * APTA_INTERNAL_SAMPLE_MAGNITUDE_SCALE);
#endif
    bin->sample_count += 1u;
    if (bin->sample_count ==
        apta_s4_expected_bin_samples(session, bin_index)) {
        apta_s4_note_bin_complete(session, bin_index);
    }
    return APTA_STATUS_OK;
}

apta_status_t apta_internal_s4_refresh(
    apta_session_t *session,
    uint32_t step_limit,
    uint32_t *completed_steps_out)
{
    APTA_S4_PROFILE_DECLARE;
    float best_scores[APTA_INTERNAL_MAX_TEMPO_CANDIDATES] = {0.0f, 0.0f, 0.0f};
    uint32_t best_lags[APTA_INTERNAL_MAX_TEMPO_CANDIDATES] = {0u, 0u, 0u};
    float lag_offsets[APTA_INTERNAL_MAX_TEMPO_CANDIDATES] = {0.0f, 0.0f, 0.0f};
    uint64_t evidence_first;
    uint64_t evidence_end;
    uint64_t run_count;
    uint64_t index;
    uint32_t flux_capacity;
    float family_best;
    float family_ambiguity;
    float grid_fit;
    int estimate = 0;
    uint32_t minimum_lag;
    uint32_t maximum_lag;
    uint32_t lag;
    uint32_t candidate_count = 0u;
    uint32_t selected_tempo;
    uint32_t phase = 0u;
    float phase_score = -1.0f;
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
    uint32_t completed_steps = 0u;

    if (completed_steps_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *completed_steps_out = 0u;

    if (!apta_s4_enabled(session) || session->onset_bins == NULL ||
        session->onset_flux == NULL) {
        return APTA_STATUS_OK;
    }

#ifdef APTA_INTERNAL_PROFILE_S4
    session->s4_profile.process_calls += 1u;
#endif

    if (session->local_grid_locked) {
        session->s4_refresh_active = 0u;
        session->s4_refresh_pending = 0u;
        if ((atomic_load_explicit(&session->state, memory_order_acquire) ==
                 APTA_SESSION_DRAINING ||
             atomic_load_explicit(&session->state, memory_order_acquire) ==
                 APTA_SESSION_COMPLETED) &&
            session->tempo_value.state != APTA_FEATURE_FINAL) {
            session->tempo_value.state = APTA_FEATURE_FINAL;
            session->local_grid_segment.state = APTA_FEATURE_FINAL;
            session->s4_mutation_serial += 1u;
        }
        return APTA_STATUS_OK;
    }

    /* Focus/range changes may recontextualize the last committed estimate,
     * but never expose an active generation's partial argmax. Do this before
     * either starting or resuming the expensive generation. */
    apta_s4_update_cached_ranges(session);
    if (session->s4_refresh_active) {
        evidence_first = session->s4_refresh_evidence_first;
        evidence_end = session->s4_refresh_evidence_end;
        run_count = evidence_end - evidence_first;
        minimum_lag = session->s4_refresh_minimum_lag;
        maximum_lag = session->s4_refresh_maximum_lag;
        estimate = 1;
        goto resume_lag_sweep;
    }

    APTA_S4_PROFILE_BEGIN(session);
    if (!apta_s4_find_evidence(session, &evidence_first, &evidence_end)) {
        APTA_S4_PROFILE_ADD(session, find_evidence_ns);
        return APTA_STATUS_OK;
    }
    APTA_S4_PROFILE_ADD(session, find_evidence_ns);
    run_count = evidence_end - evidence_first;
    if (run_count < APTA_INTERNAL_MIN_TEMPO_BINS) {
        return APTA_STATUS_OK;
    }

    /* A2: decide whether to re-run the autocorrelation. Only the two expensive
     * loops are gated; everything derived from the current ranges is
     * recomputed on every call from the cached estimate, so focus movement
     * keeps updating the applicability range and republishing.
     *
     * A draining session estimates once against the complete evidence range
     * so APTA_FEATURE_FINAL is reachable. Once that generation is final it is
     * reused; otherwise a downstream S6 refresh with a one-step budget could
     * be starved by redundant final S4 scans. */
    if (evidence_end < session->s4_refreshed_evidence_end) {
        /* Focus or a region request moved the range backwards. Reset the
         * tracker and re-estimate. */
        session->s4_refreshed_evidence_end = 0u;
        estimate = 1;
    } else if ((session->end_of_input_signalled &&
                session->tempo_value.state != APTA_FEATURE_FINAL) ||
               evidence_end >= session->s4_refreshed_evidence_end +
                                   APTA_INTERNAL_S4_REFRESH_MIN_NEW_BINS) {
        estimate = 1;
    } else if (session->s4_refreshed_evidence_end == 0u) {
        /* No cached estimate to fall back on yet. */
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
     * array reads. A3 subsequently converted the correlation, refinement and
     * phase accumulators to float, so the whole hot path now stays in the
     * target's native single-precision arithmetic.
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
    if (!estimate) {
        /* A2: reuse the last estimate; skip the fill and both loops. */
#ifdef APTA_INTERNAL_PROFILE_S4
        session->s4_profile.gated_calls += 1u;
#endif
        for (lag = 0u; lag < APTA_INTERNAL_MAX_TEMPO_CANDIDATES; ++lag) {
            best_scores[lag] = session->s4_cached_scores[lag];
            best_lags[lag] = session->s4_cached_lags[lag];
            lag_offsets[lag] = session->s4_cached_lag_offsets[lag];
        }
        goto estimate_ready;
    }

    if (step_limit == 0u) {
        session->s4_refresh_pending = 1u;
        return APTA_STATUS_MORE_WORK;
    }
#ifdef APTA_INTERNAL_PROFILE_S4
    session->s4_profile.refresh_scans += 1u;
    session->s4_profile.evidence_bins_scanned += run_count;
#endif
    APTA_S4_PROFILE_BEGIN(session);
    for (index = evidence_first; index < evidence_end; ++index) {
        session->onset_flux[(uint32_t)(index - evidence_first)] =
            (float)apta_s4_flux_uncached(session, index, evidence_first);
    }
    APTA_S4_PROFILE_ADD(session, flux_ns);
    session->s4_refresh_evidence_first = evidence_first;
    session->s4_refresh_evidence_end = evidence_end;
    session->s4_refresh_minimum_lag = minimum_lag;
    session->s4_refresh_maximum_lag = maximum_lag;
    session->s4_refresh_next_lag = minimum_lag;
    memset(session->s4_refresh_best_scores,
           0,
           sizeof(session->s4_refresh_best_scores));
    memset(session->s4_refresh_best_lags,
           0,
           sizeof(session->s4_refresh_best_lags));
    session->s4_refresh_active = 1u;
    session->s4_refresh_pending = 0u;
    completed_steps += 1u;

resume_lag_sweep:
    while (session->s4_refresh_next_lag <= maximum_lag &&
           completed_steps < step_limit) {
        uint32_t last_lag = session->s4_refresh_next_lag +
                            APTA_INTERNAL_S4_LAGS_PER_STEP - 1u;

        if (last_lag < session->s4_refresh_next_lag ||
            last_lag > maximum_lag) {
            last_lag = maximum_lag;
        }
        APTA_S4_PROFILE_BEGIN(session);
        for (lag = session->s4_refresh_next_lag; lag <= last_lag; ++lag) {
            float score;
            uint32_t position;

            /* B1: preserve the original ordered argmax exactly; only the
             * scheduler boundary moved between groups of lags. */
            score = apta_s4_correlation_at_lag(
                        session,
                        evidence_first,
                        evidence_end,
                        lag) *
                    apta_s4_tempo_prior(
                        apta_s4_tempo_from_lag(session, lag));
            if (score <= 0.0f) {
                continue;
            }
            for (position = 0u;
                 position < APTA_INTERNAL_MAX_TEMPO_CANDIDATES;
                 ++position) {
                if (score > session->s4_refresh_best_scores[position]) {
                    uint32_t move;
                    for (move = APTA_INTERNAL_MAX_TEMPO_CANDIDATES - 1u;
                         move > position;
                         --move) {
                        session->s4_refresh_best_scores[move] =
                            session->s4_refresh_best_scores[move - 1u];
                        session->s4_refresh_best_lags[move] =
                            session->s4_refresh_best_lags[move - 1u];
                    }
                    session->s4_refresh_best_scores[position] = score;
                    session->s4_refresh_best_lags[position] = lag;
                    break;
                }
            }
        }
        session->s4_refresh_next_lag = last_lag + 1u;
        completed_steps += 1u;
        APTA_S4_PROFILE_ADD(session, lag_sweep_ns);
        if (session->process_deadline_ns != 0u &&
            session->context->clock.monotonic_time_ns != NULL &&
            session->context->clock.monotonic_time_ns(
                session->context->clock.user_data) >=
                session->process_deadline_ns) {
            break;
        }
    }
    if (session->s4_refresh_next_lag <= maximum_lag ||
        completed_steps >= step_limit) {
        *completed_steps_out = completed_steps;
        return APTA_STATUS_MORE_WORK;
    }

    /* Refinement, ambiguity, phase and publication form one final scheduler
     * step. They consume only a small fixed number of correlations and the
     * cached/public state remains untouched until this point. */
    completed_steps += 1u;
    for (lag = 0u; lag < APTA_INTERNAL_MAX_TEMPO_CANDIDATES; ++lag) {
        best_scores[lag] = session->s4_refresh_best_scores[lag];
        best_lags[lag] = session->s4_refresh_best_lags[lag];
    }

    /* Refine each candidate, not just the winner: all three are published, and
     * the duplicate suppression below compares their tempi. Three correlations
     * per candidate against several hundred in the scan above. */
    for (lag = 0u; lag < APTA_INTERNAL_MAX_TEMPO_CANDIDATES; ++lag) {
        lag_offsets[lag] = best_lags[lag] != 0u
                               ? apta_s4_lag_offset(session,
                                                    evidence_first,
                                                    evidence_end,
                                                    best_lags[lag])
                               : 0.0f;
    }

    APTA_S4_PROFILE_ADD(session, refinement_ns);

    /* A2: cache the estimate so a gated pass can reuse it. */
    for (lag = 0u; lag < APTA_INTERNAL_MAX_TEMPO_CANDIDATES; ++lag) {
        session->s4_cached_scores[lag] = best_scores[lag];
        session->s4_cached_lags[lag] = best_lags[lag];
        session->s4_cached_lag_offsets[lag] = lag_offsets[lag];
    }
    session->s4_refreshed_evidence_end = evidence_end;
    session->s4_refresh_active = 0u;
    session->s4_refresh_pending = 0u;
    {
        uint64_t current_first;
        uint64_t current_end;

        if (apta_s4_find_evidence(session, &current_first, &current_end) &&
            (current_first < evidence_first ||
             current_end < evidence_end ||
             (session->end_of_input_signalled &&
              (current_first != evidence_first ||
               current_end != evidence_end)) ||
             current_end >= evidence_end +
                                APTA_INTERNAL_S4_REFRESH_MIN_NEW_BINS)) {
            session->s4_refresh_pending = 1u;
        }
    }
    *completed_steps_out = completed_steps;

estimate_ready:
    if (best_lags[0] == 0u || best_scores[0] < 0.05f) {
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
        tempo = apta_s4_tempo_from_refined_lag(
            session, best_lags[lag], lag_offsets[lag]);
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

        score = (uint32_t)(best_scores[lag] / best_scores[0] * 65535.0f + 0.5f);
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

    /*
     * Let the global estimator promote one of these candidates over the local
     * winner. Only a candidate already in the list, only when S6 lands squarely
     * on it, and only when it already scored well on S4's own evidence. See
     * APTA_INTERNAL_TEMPO_ENDORSE_TOLERANCE for why it is this weak.
     */
    if (session->s6_nominal_tempo_millibpm != 0u && candidate_count > 1u) {
        const float endorsed = (float)session->s6_nominal_tempo_millibpm;
        uint32_t best = 0u;
        uint32_t entry;

        for (entry = 1u; entry < candidate_count; ++entry) {
            const apta_tempo_candidate_t *candidate =
                &session->tempo_candidates[entry];
            float distance;

            if (candidate->score < APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE) {
                continue;
            }
            distance = (float)candidate->tempo_millibpm - endorsed;
            if (distance < 0.0f) {
                distance = -distance;
            }
            if (distance / endorsed >
                APTA_INTERNAL_TEMPO_ENDORSE_TOLERANCE) {
                continue;
            }
            if (best == 0u ||
                candidate->score > session->tempo_candidates[best].score) {
                best = entry;
            }
        }
        if (best != 0u) {
            apta_tempo_candidate_t promoted =
                session->tempo_candidates[best];

            /* Move it to the front rather than rewriting the selection, so the
             * candidate list stays ordered by what was published and a host
             * reading it sees the same answer at slot zero. */
            for (entry = best; entry > 0u; --entry) {
                session->tempo_candidates[entry] =
                    session->tempo_candidates[entry - 1u];
            }
            /* Endorsement changes the published rank, so its encoded score
             * must reflect that rank as well. Otherwise TEMP would put a
             * lower score before the original 65535 winner and violate the
             * container's non-increasing candidate-order contract. */
            promoted.score = apta_internal_tempo_promotion_score(
                promoted.score, session->tempo_candidates[1].score);
            session->tempo_candidates[0] = promoted;
            flags |= APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY;
        }
    }

    selected_tempo = session->tempo_candidates[0].tempo_millibpm;
    for (lag = 0u; lag < candidate_count; ++lag) {
        session->tempo_candidates[lag].relation_to_selected =
            apta_internal_tempo_relation(
                selected_tempo,
                session->tempo_candidates[lag].tempo_millibpm);
        if (lag != 0u &&
            session->tempo_candidates[lag].score >=
                (uint16_t)((uint32_t)session->tempo_candidates[0].score * 7u / 10u)) {
            const apta_tempo_relation_t relation =
                session->tempo_candidates[lag].relation_to_selected;

            /* B2: flag every metrical relation, not just half and double. The
             * two dominant errors measured on the accuracy corpus are
             * two-thirds and third, neither of which had a flag before, so a
             * host reading the flags saw nothing wrong. */
            if (relation != APTA_TEMPO_RELATION_INDEPENDENT) {
                flags |= APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY;
            }
            if (relation == APTA_TEMPO_RELATION_HALF) {
                flags |= APTA_TEMPO_FLAG_HALF_TIME_AMBIGUITY;
            }
            if (relation == APTA_TEMPO_RELATION_DOUBLE) {
                flags |= APTA_TEMPO_FLAG_DOUBLE_TIME_AMBIGUITY;
            }
        }
    }

    /* B1: how close did the best octave sibling of the winner come? A
     * candidate whose family sibling scores nearly as well is ambiguous, and
     * the honest response is lower confidence rather than high confidence with
     * a flag nobody reads. That failure -- confidence 82 on a third-relation
     * error -- is what this task exists to stop. */
    if (estimate) {
        APTA_S4_PROFILE_BEGIN(session);
    }
    if (!estimate) {
        /* A2/B1: a gated pass must not run the family scan. It reads
         * onset_flux, which was filled relative to the evidence start of the
         * last refresh that actually estimated; the current evidence start may
         * differ, and the newest bins are not in the array at all. Reuse the
         * ambiguity computed alongside the cached estimate instead. */
        family_ambiguity = session->s4_cached_ambiguity;
    } else {
        const float winner = best_scores[0];
        uint32_t entry;

        family_best = 0.0f;
        for (entry = 0u; entry < APTA_INTERNAL_TEMPO_RATIO_COUNT; ++entry) {
            /* B2: the same ratio table the relation classifier uses. A lag
             * scaled by r corresponds to a tempo scaled by 1/r, so the
             * numerator and denominator swap here. */
            const float scaled =
                (float)best_lags[0] *
                    (float)apta_internal_tempo_ratios[entry].denominator /
                    (float)apta_internal_tempo_ratios[entry].numerator + 0.5f;
            uint32_t sibling_lag;
            float sibling;

            if (scaled < 1.0f) {
                continue;
            }
            sibling_lag = (uint32_t)scaled;
            if (sibling_lag < minimum_lag || sibling_lag > maximum_lag ||
                sibling_lag == best_lags[0]) {
                continue;
            }
            sibling = apta_s4_correlation_at_lag(
                          session,
                          evidence_first,
                          evidence_end,
                          sibling_lag) *
                      apta_s4_tempo_prior(
                          apta_s4_tempo_from_lag(session, sibling_lag));
            if (sibling > family_best) {
                family_best = sibling;
            }
        }
        if (winner > 0.0f && family_best > 0.0f) {
            const float ratio = family_best / winner;

            if (ratio <= APTA_INTERNAL_TEMPO_AMBIGUITY_KNEE) {
                family_ambiguity = 0.0f;
            } else {
                family_ambiguity =
                    (ratio - APTA_INTERNAL_TEMPO_AMBIGUITY_KNEE) /
                    (1.0f - APTA_INTERNAL_TEMPO_AMBIGUITY_KNEE);
                if (family_ambiguity > 1.0f) {
                    family_ambiguity = 1.0f;
                }
            }
        } else {
            family_ambiguity = 0.0f;
        }
        session->s4_cached_ambiguity = family_ambiguity;
    }
    if (estimate) {
        APTA_S4_PROFILE_ADD(session, family_scan_ns);
    }

    /* B2: a strong family sibling is ambiguity even when it did not survive
     * into the candidate list, which holds only three entries. */
    if (family_ambiguity > 0.0f) {
        flags |= APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY;
    }

    if (estimate) {
        APTA_S4_PROFILE_BEGIN(session);
        for (lag = 0u; lag < best_lags[0]; ++lag) {
            float score = 0.0f;
            for (index = evidence_first + lag;
                 index < evidence_end;
                 index += best_lags[0]) {
                score += session->onset_flux[
                    (uint32_t)(index - evidence_first)];
            }
            if (score > phase_score) {
                phase_score = score;
                phase = lag;
            }
        }
        session->s4_cached_phase = phase;
        session->s4_cached_grid_fit = apta_s4_grid_fit(
            session,
            evidence_first,
            evidence_end,
            best_lags[0],
            phase);
    } else {
        phase = session->s4_cached_phase;
    }
    if (estimate) {
        APTA_S4_PROFILE_ADD(session, phase_search_ns);
    }
    grid_fit = session->s4_cached_grid_fit;

    APTA_S4_PROFILE_BEGIN(session);

    /*
     * Confidence is computed here rather than with the candidate list because
     * the grid fit needs the phase, and the phase search runs after candidate
     * selection. Nothing between the two points reads confidence.
     */
    {
        /*
         * The dominant term is the grid fit rather than the raw correlation
         * strength it replaces.
         *
         * `best_scores[0]` measures how metronomic the recording is, not how
         * likely the answer is right: measured over sixty tracks, adding 6 ms
         * of timing jitter -- an ordinary human performance -- dropped the
         * highest confidence attached to a correct answer from 90 to 64 while
         * accuracy barely moved, so a host gating on it rejected everything.
         *
         * The grid fit is a ratio of novelty on the predicted beats to novelty
         * between them, so a uniformly weaker novelty does not move it. Its
         * median for correct answers is 0.34 with exact timing and 0.34 with
         * jitter, while for incorrect answers it falls from 0.18 to 0.12.
         *
         * The doubling maps the fit's practical range, roughly 0 to 0.5, onto
         * the term's full weight. It is a range mapping, not a tuning knob.
         */
        const float fit = grid_fit > 0.0f ? grid_fit * 2.0f : 0.0f;

        confidence = 35u + (uint32_t)((fit > 1.0f ? 1.0f : fit) * 50.0f);
    }
    if (candidate_count > 1u && best_scores[0] > 0.0f) {
        float separation = 1.0f - best_scores[1] / best_scores[0];
        if (separation > 0.0f) {
            confidence += (uint32_t)(separation * 15.0f);
        }
    }
    if (run_count >= APTA_INTERNAL_STABLE_TEMPO_BINS) {
        confidence += 5u;
    }
    if (confidence > APTA_CONFIDENCE_MAX) {
        confidence = APTA_CONFIDENCE_MAX;
    }
#ifdef APTA_INTERNAL_REPORT_GRID_FIT
    /* Diagnostic build only: report the raw grid fit in place of confidence so
     * the two populations can be compared before deciding whether, and how, it
     * belongs in the formula. Never enabled in a shipped build. */
    confidence = (uint32_t)(grid_fit > 0.0f ? grid_fit * 100.0f : 0.0f);
    if (confidence > APTA_CONFIDENCE_MAX) {
        confidence = APTA_CONFIDENCE_MAX;
    }
#else
    (void)grid_fit;  /* Measured before it is wired into the formula. */
#endif
    /* Scale the whole figure by how unambiguous the octave choice was. A
     * sibling scoring as well as the winner drives confidence to zero. */
    confidence = (uint32_t)((float)confidence * (1.0f - family_ambiguity));

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
        (atomic_load_explicit(&session->state, memory_order_acquire) ==
             APTA_SESSION_DRAINING ||
         atomic_load_explicit(&session->state, memory_order_acquire) ==
             APTA_SESSION_COMPLETED)) {
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
    APTA_S4_PROFILE_ADD(session, publication_ns);
    if (session->s4_refresh_pending) {
        if (completed_steps < step_limit) {
            uint32_t follow_steps = 0u;
            const apta_status_t follow_status = apta_internal_s4_refresh(
                session,
                step_limit - completed_steps,
                &follow_steps);

            completed_steps += follow_steps;
            *completed_steps_out = completed_steps;
            return follow_status;
        }
        return APTA_STATUS_MORE_WORK;
    }
    return APTA_STATUS_OK;
}

int apta_internal_s4_refresh_pending(const apta_session_t *session)
{
    return session != NULL &&
           (session->s4_refresh_active || session->s4_refresh_pending);
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
    const int completed =
        session != NULL &&
        atomic_load_explicit(&session->state, memory_order_acquire) ==
            APTA_SESSION_COMPLETED;

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

        if ((session->config.requested_features &
             APTA_FEATURE_CALIBRATED_QUALITY) != 0u &&
            session->final_end_frame != APTA_TOTAL_FRAMES_UNKNOWN &&
            session->final_end_frame != 0u &&
            result->tempo.selected.confidence <= APTA_CONFIDENCE_MAX) {
            /* Task-6: publish the accepted BPM calibration as an optional
             * quality record. The model can only lower a confidence value,
             * so the safety property of the raw detector is preserved.
             * Coverage reports how much of the track the tempo evidence
             * explains, in permille, computed with a bounded divide. */
            apta_quality_view_t *quality =
                (apta_quality_view_t *)apta_internal_context_allocate(
                    session->context,
                    sizeof(*quality),
                    alignof(apta_quality_view_t),
                    APTA_MEMORY_PERSISTENT);
            const uint64_t total = (uint64_t)session->final_end_frame;
            uint64_t covered = session->greatest_accepted_end;

            if (covered > total) {
                covered = total;
            }
            apta_quality_view_init(quality);
            quality->feature = APTA_FEATURE_BPM;
            quality->calibration_model_id =
                APTA_INTERNAL_BPM_QUALITY_MODEL_ID;
            quality->confidence = apta_internal_bpm_quality_calibrate(
                result->tempo.selected.confidence);
            quality->state = completed
                                 ? APTA_FEATURE_FINAL
                                 : result->tempo.selected.state;
            quality->evidence_coverage_permille =
                (uint16_t)((covered * 1000u) / total);
            result->quality = quality;
            result->quality_count = 1u;
            result->info.available_features |=
                APTA_FEATURE_CALIBRATED_QUALITY;
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
    apta_internal_context_deallocate(result->context, result->local_grid_beats);
    result->tempo_candidates = NULL;
    result->local_grid_coverage = NULL;
    result->local_grid_segments = NULL;
    result->local_grid_beats = NULL;
    result->tempo.candidates = NULL;
    result->tempo.candidate_count = 0u;
    result->local_grid.coverage_ranges = NULL;
    result->local_grid.coverage_range_count = 0u;
    result->local_grid.segments = NULL;
    result->local_grid.segment_count = 0u;
    result->local_grid.beats = NULL;
    result->local_grid.beat_count = 0u;
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
