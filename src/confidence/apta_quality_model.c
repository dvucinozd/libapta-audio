// SPDX-License-Identifier: Apache-2.0
#include "apta_quality_model.h"

static const apta_confidence_value_t
    APTA_INTERNAL_BPM_QUALITY_LUT[APTA_CONFIDENCE_MAX + 1u] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
        21, 21, 21, 21, 21, 21, 31, 31, 38, 45,
        45, 45, 45, 45, 45, 45, 56, 57, 58, 59,
        60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
        70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
        80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
        90, 91, 92, 93, 93, 95, 96, 97, 98, 99,
        100};

apta_confidence_value_t apta_internal_bpm_quality_calibrate(
    apta_confidence_value_t raw_confidence)
{
    if (raw_confidence == APTA_CONFIDENCE_UNKNOWN ||
        raw_confidence > APTA_CONFIDENCE_MAX) {
        return raw_confidence;
    }
    return APTA_INTERNAL_BPM_QUALITY_LUT[raw_confidence];
}

uint16_t apta_internal_quality_coverage_permille(
    uint64_t covered,
    uint64_t total)
{
    uint64_t remainder;
    uint16_t permille;
    uint16_t place;

    if (total == 0u) {
        return 0u;
    }
    if (covered >= total) {
        return 1000u;
    }

    remainder = covered;
    permille = 0u;
    for (place = 100u; place != 0u; place /= 10u) {
        uint64_t accumulator = 0u;
        uint16_t addend;
        uint16_t digit = 0u;

        /* Decimal long division computes remainder * 10 without ever
         * overflowing uint64_t. */
        for (addend = 0u; addend < 10u; ++addend) {
            if (remainder != 0u && accumulator >= total - remainder) {
                accumulator -= total - remainder;
                ++digit;
            } else {
                accumulator += remainder;
            }
        }
        remainder = accumulator;
        permille = (uint16_t)(permille + digit * place);
    }
    return permille;
}
