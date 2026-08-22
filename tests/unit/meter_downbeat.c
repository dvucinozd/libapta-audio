// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>

#include "../../src/beatgrid/apta_meter_internal.h"

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

    return EXIT_SUCCESS;
}
