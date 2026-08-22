// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_GRID_MATCH_INTERNAL_H
#define APTA_GRID_MATCH_INTERNAL_H

#include "apta_grid_position_internal.h"

#include <apta/apta_result.h>

#include <stdint.h>

typedef struct {
    const apta_grid_view_t *grid;
    uint32_t beat_index;
    uint32_t segment_index;
    uint64_t *work_counter;
} apta_internal_grid_match_cursor_t;

typedef struct {
    apta_internal_grid_match_cursor_t local;
    apta_internal_grid_match_cursor_t global;
    uint32_t has_local;
    uint32_t has_global;
} apta_internal_grid_match_set_t;

static void apta_internal_grid_match_cursor_init(
    apta_internal_grid_match_cursor_t *cursor,
    const apta_grid_view_t *grid,
    uint64_t *work_counter)
{
    cursor->grid = grid;
    cursor->beat_index = 0u;
    cursor->segment_index = 0u;
    cursor->work_counter = work_counter;
}

static void apta_internal_grid_match_count(
    apta_internal_grid_match_cursor_t *cursor)
{
    if (cursor->work_counter != NULL) ++*cursor->work_counter;
}

static int apta_internal_grid_segment_has_downbeat(
    const apta_grid_segment_t *segment,
    apta_source_frame_t frame,
    apta_beat_ordinal_t ordinal)
{
    apta_fractional_frame_t position;

    if (frame < segment->applicability_range.first_frame ||
        frame >= segment->applicability_range.end_frame ||
        !apta_internal_grid_segment_position_at_ordinal(
            segment, ordinal, &position)) {
        return 0;
    }

    /* MTRD stores only an integer source frame. The 1.1 wire contract binds
     * that value to the grid beat's encoded whole-frame component and ordinal;
     * a non-zero Q32 remainder is not representable in MTRD and therefore must
     * not make an otherwise identical beat impossible to reference. */
    return position.whole_frame == frame;
}

static int apta_internal_grid_match_cursor_next(
    apta_internal_grid_match_cursor_t *cursor,
    apta_source_frame_t frame,
    apta_beat_ordinal_t ordinal)
{
    const apta_grid_view_t *grid = cursor->grid;

    if (grid == NULL ||
        (grid->beat_count != 0u && grid->beats == NULL) ||
        (grid->segment_count != 0u && grid->segments == NULL)) {
        return 0;
    }
    while (cursor->beat_index < grid->beat_count) {
        const apta_beat_t *beat = &grid->beats[cursor->beat_index];
        apta_internal_grid_match_count(cursor);
        if (beat->position.whole_frame < frame ||
            (beat->position.whole_frame == frame &&
             beat->ordinal < ordinal)) {
            ++cursor->beat_index;
            continue;
        }
        if (beat->position.whole_frame == frame &&
            beat->ordinal == ordinal) {
            return 1;
        }
        break;
    }
    while (cursor->segment_index < grid->segment_count) {
        const apta_grid_segment_t *segment =
            &grid->segments[cursor->segment_index];
        apta_internal_grid_match_count(cursor);
        if (segment->applicability_range.end_frame <= frame) {
            ++cursor->segment_index;
            continue;
        }
        return apta_internal_grid_segment_has_downbeat(
            segment, frame, ordinal);
    }
    return 0;
}

static void apta_internal_grid_match_set_init(
    apta_internal_grid_match_set_t *set,
    const apta_grid_view_t *local,
    const apta_grid_view_t *global,
    uint64_t *work_counter)
{
    set->has_local = local != NULL;
    set->has_global = global != NULL;
    apta_internal_grid_match_cursor_init(&set->local, local, work_counter);
    apta_internal_grid_match_cursor_init(&set->global, global, work_counter);
}

static int apta_internal_grid_match_set_next(
    apta_internal_grid_match_set_t *set,
    apta_source_frame_t frame,
    apta_beat_ordinal_t ordinal)
{
    if (set->has_local && apta_internal_grid_match_cursor_next(
            &set->local, frame, ordinal)) {
        return 1;
    }
    if (set->has_global && apta_internal_grid_match_cursor_next(
            &set->global, frame, ordinal)) {
        return 1;
    }
    return !set->has_local && !set->has_global;
}

#endif
