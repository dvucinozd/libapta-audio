// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_CORE_PERIOD_REFINE_H
#define APTA_CORE_PERIOD_REFINE_H

#include <math.h>
#include <stdint.h>

#include "apta_internal.h"

/*
 * Sub-bin period refinement, shared by the local and global estimators.
 *
 * Both search for the beat period as an integer argmax over their own bins, so
 * both can only report the tempi those bins land on. S4's bin is 5.8 ms, which
 * is 1.6 BPM near 128; S6's is 46 ms, which is 13 BPM and leaves only three
 * reachable tempi in the whole 110-150 range.
 *
 * Resolution comes from measuring across beats. Correlating at a lag of N beats
 * puts the peak near N*lag, and dividing the integer argmax found there by N
 * divides the error by N as well.
 *
 * Fitting a parabola through the winning lag and its neighbours was tried
 * instead and measured worse -- the peak is as narrow as the onsets that make
 * it, so three samples across it describe local asymmetry, not a parabola. See
 * section 26.2 of the S4 status document.
 *
 * These live in a header as `static inline` so that sharing them does not add a
 * translation unit to both the root and ESP-IDF build files, where each source
 * carries its own symbol-renaming definitions.
 */

/*
 * Normalized autocorrelation of `flux[0 .. span)` at `lag`.
 *
 * Callers pass a pointer to the start of their own slice, so the same function
 * serves S4's evidence run and S6's window without either needing to know how
 * the other addresses its ring.
 */
static inline float apta_internal_correlation_at_lag(
    const float *flux,
    uint32_t span,
    uint32_t lag)
{
    float numerator = 0.0f;
    float left_square = 0.0f;
    float right_square = 0.0f;
    uint32_t offset;

    if (flux == NULL || lag == 0u || lag >= span) {
        return 0.0f;
    }
    for (offset = lag; offset < span; ++offset) {
        const float left = flux[offset];
        const float right = flux[offset - lag];
        numerator += left * right;
        left_square += left * left;
        right_square += right * right;
    }
    /* A3: float guard, sized for normalized flux. */
    if (left_square <= 1e-12f || right_square <= 1e-12f) {
        return 0.0f;
    }
    return numerator / sqrtf(left_square * right_square);
}

/*
 * Sub-bin offset of the beat period at `lag`, in bins, within [-0.5, 0.5].
 *
 * `max_beats` is how far across the track the caller is willing to measure. It
 * is reduced automatically when the span cannot spare the shift: half the
 * evidence must survive it, or the correlation is measured over too little
 * material to mean anything.
 *
 * The search window is +/- max_beats/2 bins around max_beats*lag, which is
 * exactly half a bin either side of the integer lag, so the result cannot walk
 * onto a neighbouring beat and change the answer's octave.
 */
static inline float apta_internal_refine_lag(
    const float *flux,
    uint32_t span,
    uint32_t lag,
    uint32_t max_beats)
{
    uint32_t multiple = max_beats;
    uint32_t best_extended;
    uint32_t window;
    uint32_t step;
    float best_score = 0.0f;
    float offset;

    if (flux == NULL || lag == 0u || span == 0u) {
        return 0.0f;
    }
    while (multiple > 1u && multiple * lag > span / 2u) {
        multiple -= 1u;
    }
    if (multiple < 2u) {
        return 0.0f;
    }

    window = multiple / 2u;
    best_extended = multiple * lag;
    for (step = 0u; step <= 2u * window; ++step) {
        const uint32_t extended = multiple * lag - window + step;
        float score;

        if (extended == 0u || extended >= span) {
            continue;
        }
        score = apta_internal_correlation_at_lag(flux, span, extended);
        if (score > best_score) {
            best_score = score;
            best_extended = extended;
        }
    }
    if (best_score <= 0.0f) {
        return 0.0f;
    }

    offset = (float)best_extended / (float)multiple - (float)lag;
    if (offset > 0.5f) {
        offset = 0.5f;
    } else if (offset < -0.5f) {
        offset = -0.5f;
    }
    return offset;
}

/*
 * Tempo at a fractional lag, as `tempo(lag) * lag / (lag + offset)`.
 *
 * Scaling an exact integer-lag tempo keeps the wide arithmetic where it belongs
 * and leaves float carrying only a ratio within a few percent of one, where its
 * precision costs a hundredth of a millibpm. The target has no hardware double
 * (A3), so computing the whole expression in float would put a 2.6e9 numerator
 * through a 24-bit mantissa for no gain.
 */
static inline uint32_t apta_internal_tempo_with_offset(
    uint32_t tempo,
    uint32_t lag,
    float offset)
{
    float refined;

    if (tempo == 0u || lag == 0u || offset == 0.0f) {
        return tempo;
    }
    refined = (float)tempo * (float)lag / ((float)lag + offset);
    if (refined <= 0.0f) {
        return tempo;
    }
    return (uint32_t)(refined + 0.5f);
}

#endif /* APTA_CORE_PERIOD_REFINE_H */
