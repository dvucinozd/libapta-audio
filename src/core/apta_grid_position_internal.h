// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_GRID_POSITION_INTERNAL_H
#define APTA_GRID_POSITION_INTERNAL_H

#include <apta/apta_result.h>

#include <stdint.h>

/* Resolve one segmented-grid beat without losing the Q32 remainder. Native
 * meter publication and MTRD/grid validation must use the same arithmetic so
 * an accepted downbeat cannot drift from the beat it names. */
static int apta_internal_grid_segment_position_at_ordinal(
    const apta_grid_segment_t *segment,
    apta_beat_ordinal_t ordinal,
    apta_fractional_frame_t *position_out)
{
    uint64_t delta;
    uint64_t whole;
    uint64_t fraction_product;
    uint32_t fraction;

    if (segment == NULL || position_out == NULL ||
        ordinal < segment->anchor_ordinal) {
        return 0;
    }
    delta = (uint64_t)ordinal - (uint64_t)segment->anchor_ordinal;
    if (segment->frames_per_beat.whole_frames != 0u &&
        delta > (UINT64_MAX - segment->anchor_position.whole_frame) /
                    segment->frames_per_beat.whole_frames) {
        return 0;
    }
    whole = segment->anchor_position.whole_frame +
            delta * segment->frames_per_beat.whole_frames;
    if (segment->frames_per_beat.fraction_q32 != 0u &&
        delta > UINT64_MAX / segment->frames_per_beat.fraction_q32) {
        return 0;
    }
    fraction_product =
        delta * (uint64_t)segment->frames_per_beat.fraction_q32;
    if (whole > UINT64_MAX - (fraction_product >> 32u)) {
        return 0;
    }
    whole += fraction_product >> 32u;
    fraction = segment->anchor_position.fraction_q32 +
               (uint32_t)fraction_product;
    if (fraction < segment->anchor_position.fraction_q32) {
        if (whole == UINT64_MAX) {
            return 0;
        }
        ++whole;
    }
    position_out->whole_frame = whole;
    position_out->fraction_q32 = fraction;
    return 1;
}

#endif
