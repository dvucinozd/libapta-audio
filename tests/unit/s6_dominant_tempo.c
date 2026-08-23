// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "apta_s6_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void segment(
    apta_grid_segment_t *value,
    uint64_t first,
    uint64_t end,
    uint32_t tempo,
    apta_confidence_value_t confidence)
{
    memset(value, 0, sizeof(*value));
    value->applicability_range.first_frame = first;
    value->applicability_range.end_frame = end;
    value->nominal_tempo_millibpm = tempo;
    value->confidence = confidence;
}

int main(void)
{
    apta_grid_segment_t segments[4];

    /* The last segment is individually longest, but the two mutually
     * agreeing, higher-confidence 128 BPM segments have greater support. */
    segment(&segments[0], 0u, 1000u, 128000u, 74u);
    segment(&segments[1], 1000u, 2000u, 127900u, 73u);
    segment(&segments[2], 2000u, 3900u, 82700u, 55u);
    CHECK(apta_internal_s6_dominant_tempo_segment(segments, 3u) == 0u);

    /* A tempo more than one percent away forms a separate family. */
    segment(&segments[1], 1000u, 2000u, 126000u, 73u);
    CHECK(apta_internal_s6_dominant_tempo_segment(segments, 3u) == 2u);

    /* Unknown confidence contributes duration but is never treated as 255. */
    segment(&segments[0], 0u, 1000u, 128000u, APTA_CONFIDENCE_UNKNOWN);
    segment(&segments[1], 1000u, 1100u, 120000u, 50u);
    CHECK(apta_internal_s6_dominant_tempo_segment(segments, 2u) == 1u);

    /* Equal support is deterministic: retain the earliest representative. */
    segment(&segments[0], 0u, 1000u, 128000u, 50u);
    segment(&segments[1], 1000u, 2000u, 120000u, 50u);
    CHECK(apta_internal_s6_dominant_tempo_segment(segments, 2u) == 0u);
    CHECK(apta_internal_s6_dominant_tempo_segment(NULL, 0u) == 0u);
    return 0;
}
