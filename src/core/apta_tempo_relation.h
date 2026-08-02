// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_TEMPO_RELATION_H
#define APTA_TEMPO_RELATION_H

#include <stddef.h>
#include <stdint.h>

#include <apta/apta.h>

typedef struct {
    uint32_t numerator;
    uint32_t denominator;
    apta_tempo_relation_t relation;
} apta_internal_tempo_ratio_t;

/* Grouped by increasing multiplicative distance from unity, with reciprocal
 * pairs adjacent. This table is shared by classification, the family scan and
 * the relation tests. */
static const apta_internal_tempo_ratio_t apta_internal_tempo_ratios[] = {
    {3u, 2u, APTA_TEMPO_RELATION_THREE_HALF},
    {2u, 3u, APTA_TEMPO_RELATION_TWO_THIRDS},
    {2u, 1u, APTA_TEMPO_RELATION_DOUBLE},
    {1u, 2u, APTA_TEMPO_RELATION_HALF},
    {3u, 1u, APTA_TEMPO_RELATION_TRIPLE},
    {1u, 3u, APTA_TEMPO_RELATION_THIRD},
    {4u, 1u, APTA_TEMPO_RELATION_QUADRUPLE},
    {1u, 4u, APTA_TEMPO_RELATION_QUARTER}
};

#define APTA_INTERNAL_TEMPO_RATIO_COUNT \
    (sizeof(apta_internal_tempo_ratios) / \
     sizeof(apta_internal_tempo_ratios[0]))

static inline apta_tempo_relation_t apta_internal_tempo_relation(
    uint32_t selected,
    uint32_t candidate)
{
    size_t entry;

    if (selected == 0u || candidate == 0u || selected == candidate) {
        return APTA_TEMPO_RELATION_INDEPENDENT;
    }

    for (entry = 0u; entry < APTA_INTERNAL_TEMPO_RATIO_COUNT; ++entry) {
        const uint64_t expected =
            (uint64_t)selected * apta_internal_tempo_ratios[entry].numerator /
            apta_internal_tempo_ratios[entry].denominator;
        const uint64_t difference = candidate > expected
            ? (uint64_t)candidate - expected
            : expected - (uint64_t)candidate;
        const uint64_t tolerance = expected / 50u + 1u;

        if (difference <= tolerance) {
            return apta_internal_tempo_ratios[entry].relation;
        }
    }
    return APTA_TEMPO_RELATION_INDEPENDENT;
}

#endif
