// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_TRANSIENT_LATTICE_H
#define APTA_TRANSIENT_LATTICE_H

#include <stdint.h>

#ifndef APTA_INTERNAL_BAND_COUNT
#error "apta_transient_lattice.h requires apta_internal.h first"
#endif

#define APTA_INTERNAL_TRANSIENT_FAST_BINS 4u
#define APTA_INTERNAL_TRANSIENT_SLOW_BINS 16u
#define APTA_INTERNAL_TRANSIENT_HISTORY_BINS                            \
    (APTA_INTERNAL_TRANSIENT_SLOW_BINS + 1u)

typedef struct {
    float bands[APTA_INTERNAL_BAND_COUNT];
    float broadband;
} apta_internal_transient_frame_t;

static inline float apta_internal_transient_positive(float value)
{
    return value > 0.0f ? value : 0.0f;
}

/*
 * WP1 iteration 1: bounded transient novelty over a trailing energy history.
 *
 * The final entry is the current frame. Both floors exclude it, so a new
 * attack is compared with evidence that existed before the attack. The band
 * balance factor stays in [0.75, 1.0]: a single-band kick or hat is retained,
 * while attacks spread over several bands receive modest support. The peak
 * term subtracts half of the immediately preceding rise, providing a one-bin
 * refractory response without keeping state or allocating another timeline.
 */
static inline float apta_internal_transient_novelty(
    const apta_internal_transient_frame_t *history,
    uint32_t count)
{
    float fast_floor[APTA_INTERNAL_BAND_COUNT] = {0.0f, 0.0f, 0.0f};
    float slow_floor[APTA_INTERNAL_BAND_COUNT] = {0.0f, 0.0f, 0.0f};
    float current_total = 0.0f;
    float previous_total = 0.0f;
    float rise_total = 0.0f;
    float previous_rise_total = 0.0f;
    float fast_contrast = 0.0f;
    float slow_contrast = 0.0f;
    float dominant_rise = 0.0f;
    float broadband_rise;
    float band_balance;
    float band_factor;
    float refractory_peak;
    float local_evidence;
    uint32_t prior_count;
    uint32_t fast_count;
    uint32_t slow_count;
    uint32_t first_fast;
    uint32_t first_slow;
    uint32_t index;
    uint32_t band;

    if (history == NULL || count == 0u ||
        count > APTA_INTERNAL_TRANSIENT_HISTORY_BINS) {
        return 0.0f;
    }

    prior_count = count - 1u;
    fast_count = prior_count < APTA_INTERNAL_TRANSIENT_FAST_BINS
                     ? prior_count
                     : APTA_INTERNAL_TRANSIENT_FAST_BINS;
    slow_count = prior_count < APTA_INTERNAL_TRANSIENT_SLOW_BINS
                     ? prior_count
                     : APTA_INTERNAL_TRANSIENT_SLOW_BINS;
    first_fast = prior_count - fast_count;
    first_slow = prior_count - slow_count;

    for (index = first_slow; index < prior_count; ++index) {
        for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
            slow_floor[band] += history[index].bands[band];
        }
    }
    for (index = first_fast; index < prior_count; ++index) {
        for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
            fast_floor[band] += history[index].bands[band];
        }
    }

    for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
        const float current = history[prior_count].bands[band];
        const float previous = prior_count > 0u
                                   ? history[prior_count - 1u].bands[band]
                                   : 0.0f;
        const float rise = apta_internal_transient_positive(current - previous);

        current_total += current;
        previous_total += previous;
        rise_total += rise;
        if (rise > dominant_rise) {
            dominant_rise = rise;
        }
        if (fast_count > 0u) {
            fast_contrast += apta_internal_transient_positive(
                current - fast_floor[band] / (float)fast_count);
        } else {
            fast_contrast += current;
        }
        if (slow_count > 0u) {
            slow_contrast += apta_internal_transient_positive(
                current - slow_floor[band] / (float)slow_count);
        } else {
            slow_contrast += current;
        }
        if (prior_count > 1u) {
            previous_rise_total += apta_internal_transient_positive(
                previous - history[prior_count - 2u].bands[band]);
        }
    }

    broadband_rise = apta_internal_transient_positive(
        history[prior_count].broadband -
        (prior_count > 0u ? history[prior_count - 1u].broadband : 0.0f));
    if (broadband_rise == 0.0f && current_total <= previous_total) {
        return 0.0f;
    }

    band_balance = rise_total > 0.0f
                       ? 1.0f - dominant_rise / rise_total
                       : 0.0f;
    band_factor = 0.75f + 0.25f * band_balance;
    refractory_peak = apta_internal_transient_positive(
        rise_total - 0.5f * previous_rise_total);
    local_evidence = 0.40f * rise_total +
                     0.25f * fast_contrast +
                     0.25f * slow_contrast +
                     0.10f * refractory_peak;

    return broadband_rise + 0.25f * band_factor * local_evidence;
}

#ifdef APTA_INTERNAL_TRANSIENT_LATTICE_I2
#define APTA_INTERNAL_TRANSIENT_I2_DENOMINATOR_FLOOR (1.0f / 255.0f)

typedef struct {
    apta_internal_transient_frame_t
        frames[APTA_INTERNAL_TRANSIENT_SLOW_BINS];
    float fast_sum[APTA_INTERNAL_BAND_COUNT];
    float slow_sum[APTA_INTERNAL_BAND_COUNT];
    apta_internal_transient_frame_t previous;
    float previous_raw;
    uint32_t count;
    uint32_t cursor;
    uint32_t have_previous;
} apta_internal_transient_i2_state_t;

_Static_assert(sizeof(apta_internal_transient_i2_state_t) <= 320u,
               "WP1 I2 rolling transient state must remain bounded");

static inline float apta_internal_transient_i2_contrast(
    float current,
    float reference)
{
    const float rise = apta_internal_transient_positive(current - reference);

    return rise /
           (current + reference +
            APTA_INTERNAL_TRANSIENT_I2_DENOMINATOR_FLOOR);
}

static inline float apta_internal_transient_i2_process(
    apta_internal_transient_i2_state_t *state,
    const apta_internal_transient_frame_t *current)
{
    const uint32_t fast_count =
        state->count < APTA_INTERNAL_TRANSIENT_FAST_BINS
            ? state->count
            : APTA_INTERNAL_TRANSIENT_FAST_BINS;
    const float broadband_previous =
        state->have_previous ? state->previous.broadband : 0.0f;
    const float broadband = apta_internal_transient_i2_contrast(
        current->broadband,
        broadband_previous);
    float band_evidence = 0.0f;
    float raw;
    float peak;
    float novelty;
    uint32_t leaving_fast = 0u;
    uint32_t band;

    if (fast_count == APTA_INTERNAL_TRANSIENT_FAST_BINS) {
        leaving_fast =
            (state->cursor + APTA_INTERNAL_TRANSIENT_SLOW_BINS -
             APTA_INTERNAL_TRANSIENT_FAST_BINS) %
            APTA_INTERNAL_TRANSIENT_SLOW_BINS;
    }

    for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
        const float previous = state->have_previous
                                   ? state->previous.bands[band]
                                   : 0.0f;
        const float fast_floor = fast_count > 0u
                                     ? state->fast_sum[band] /
                                           (float)fast_count
                                     : 0.0f;
        const float slow_floor = state->count > 0u
                                     ? state->slow_sum[band] /
                                           (float)state->count
                                     : 0.0f;

        band_evidence +=
            0.50f * apta_internal_transient_i2_contrast(
                        current->bands[band], previous) +
            0.25f * apta_internal_transient_i2_contrast(
                        current->bands[band], fast_floor) +
            0.25f * apta_internal_transient_i2_contrast(
                        current->bands[band], slow_floor);
    }
    band_evidence /= (float)APTA_INTERNAL_BAND_COUNT;
    raw = 0.50f * broadband + 0.50f * band_evidence;
    peak = apta_internal_transient_positive(
        raw - 0.50f * state->previous_raw);
    novelty = 0.75f * raw + 0.25f * peak;

    for (band = 0u; band < APTA_INTERNAL_BAND_COUNT; ++band) {
        if (fast_count == APTA_INTERNAL_TRANSIENT_FAST_BINS) {
            state->fast_sum[band] -=
                state->frames[leaving_fast].bands[band];
        }
        if (state->count == APTA_INTERNAL_TRANSIENT_SLOW_BINS) {
            state->slow_sum[band] -=
                state->frames[state->cursor].bands[band];
        }
        state->fast_sum[band] += current->bands[band];
        state->slow_sum[band] += current->bands[band];
    }
    state->frames[state->cursor] = *current;
    state->cursor =
        (state->cursor + 1u) % APTA_INTERNAL_TRANSIENT_SLOW_BINS;
    if (state->count < APTA_INTERNAL_TRANSIENT_SLOW_BINS) {
        state->count += 1u;
    }
    state->previous = *current;
    state->previous_raw = raw;
    state->have_previous = 1u;
    return novelty;
}
#endif

#endif /* APTA_TRANSIENT_LATTICE_H */
