// SPDX-License-Identifier: Apache-2.0
#include "../../src/confidence/apta_quality_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #condition);                         \
            return EXIT_FAILURE;                                             \
        }                                                                    \
    } while (0)

int main(void)
{
    apta_confidence_value_t previous = 0u;
    uint32_t value;

    /* The accepted model is clamped: it can only lower a confidence, and it
     * stays monotone non-decreasing so ordering semantics survive. */
    for (value = 0u; value <= APTA_CONFIDENCE_MAX; ++value) {
        const apta_confidence_value_t calibrated =
            apta_internal_bpm_quality_calibrate(
                (apta_confidence_value_t)value);

        CHECK(calibrated <= value);
        if (value > 0u) {
            CHECK(calibrated >= previous);
        }
        previous = calibrated;
    }

    /* Unknown sentinel passes through untouched. */
    CHECK(apta_internal_bpm_quality_calibrate(APTA_CONFIDENCE_UNKNOWN) ==
          APTA_CONFIDENCE_UNKNOWN);

    /* Spot values from the frozen model JSON. */
    CHECK(apta_internal_bpm_quality_calibrate(0u) == 0u);
    CHECK(apta_internal_bpm_quality_calibrate(30u) == 21u);
    CHECK(apta_internal_bpm_quality_calibrate(50u) == 45u);
    CHECK(apta_internal_bpm_quality_calibrate(75u) == 75u);
    CHECK(apta_internal_bpm_quality_calibrate(100u) == 100u);

    /* Coverage scaling must remain defined across the complete uint64_t
     * domain, not only for track lengths where covered * 1000 happens to fit. */
    CHECK(apta_internal_quality_coverage_permille(0u, UINT64_MAX) == 0u);
    CHECK(apta_internal_quality_coverage_permille(
              UINT64_MAX / 2u, UINT64_MAX) == 499u);
    CHECK(apta_internal_quality_coverage_permille(
              UINT64_MAX - 1u, UINT64_MAX) == 999u);
    CHECK(apta_internal_quality_coverage_permille(
              UINT64_MAX, UINT64_MAX) == 1000u);
    CHECK(apta_internal_quality_coverage_permille(1u, 0u) == 0u);

    printf("quality_model: all checks passed\n");
    return EXIT_SUCCESS;
}
