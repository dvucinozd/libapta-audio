// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/beatgrid/apta_meter_internal.h"
#include "../../src/core/apta_grid_match_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return EXIT_FAILURE;                                             \
        }                                                                    \
    } while (0)

static void fill(float *beats, uint32_t count, uint32_t meter, uint32_t phase)
{
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        beats[index] = (index % meter) == phase ? 4.0f : 1.0f;
    }
}

static int check_fractional_downbeat_binding(void)
{
    apta_grid_segment_t segment;
    apta_fractional_frame_t position;
    apta_beat_t beat;
    apta_grid_view_t grid;
    apta_internal_grid_match_cursor_t cursor;

    memset(&segment, 0, sizeof(segment));
    segment.applicability_range.first_frame = 0u;
    segment.applicability_range.end_frame = 100000u;
    segment.anchor_position.whole_frame = 1000u;
    segment.anchor_position.fraction_q32 = UINT32_C(0x80000000);
    segment.anchor_ordinal = 10;
    segment.frames_per_beat.whole_frames = 22050u;
    segment.frames_per_beat.fraction_q32 = UINT32_C(0x80000000);

    CHECK(apta_internal_grid_segment_position_at_ordinal(
        &segment, 12, &position));
    CHECK(position.whole_frame == 45101u);
    CHECK(position.fraction_q32 == UINT32_C(0x80000000));

    /* MTRD can encode only `whole_frame`, so a valid beat with a fractional
     * Q32 remainder must still match by whole-frame component + ordinal. */
    CHECK(apta_internal_grid_segment_has_downbeat(&segment, 45101u, 12));
    CHECK(!apta_internal_grid_segment_has_downbeat(&segment, 45102u, 12));
    CHECK(!apta_internal_grid_segment_has_downbeat(&segment, 45101u, 13));

    memset(&beat, 0, sizeof(beat));
    beat.position = position;
    beat.ordinal = 12;
    memset(&grid, 0, sizeof(grid));
    grid.beat_count = 1u;
    grid.beats = &beat;
    apta_internal_grid_match_cursor_init(&cursor, &grid, NULL);
    CHECK(apta_internal_grid_match_cursor_next(&cursor, 45101u, 12));
    return EXIT_SUCCESS;
}

int main(void)
{
    float beats[32];
    apta_internal_meter_selection_t selection;
    apta_status_t status;

    fill(beats, 24u, 4u, 0u);
    status = apta_internal_meter_select(beats, 24u, &selection);
    CHECK(status == APTA_STATUS_OK);
    CHECK(selection.numerator == 4u);
    CHECK(selection.denominator == 4u);
    CHECK(selection.downbeat_phase == 0u);
    CHECK(selection.confidence >= 50u);
    CHECK(selection.score > selection.runner_up_score);

    fill(beats, 24u, 3u, 2u);
    status = apta_internal_meter_select(beats, 24u, &selection);
    CHECK(status == APTA_STATUS_OK);
    CHECK(selection.numerator == 3u);
    CHECK(selection.denominator == 4u);
    CHECK(selection.downbeat_phase == 2u);
    CHECK(selection.confidence >= 50u);
    CHECK(selection.score > selection.runner_up_score);

    fill(beats, 8u, 4u, 0u);
    status = apta_internal_meter_select(beats, 8u, &selection);
    CHECK(status == APTA_STATUS_NOT_AVAILABLE);

    CHECK(check_fractional_downbeat_binding() == EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
