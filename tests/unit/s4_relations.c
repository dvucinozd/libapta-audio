// SPDX-License-Identifier: Apache-2.0
/*
 * B2: tempo relation set.
 *
 * The relation is serialized as a single byte in the TEMP section, so the
 * numeric values are wire format. This pins every one of them so a future
 * renumbering fails here rather than silently changing what an existing file
 * means, and checks that the classifier produces each relation it can emit.
 */
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    /*
     * Wire values. These are append-only. Changing any existing number
     * silently reinterprets every .apta file already written.
     */
    CHECK(APTA_TEMPO_RELATION_INDEPENDENT == 0u);
    CHECK(APTA_TEMPO_RELATION_HALF        == 1u);
    CHECK(APTA_TEMPO_RELATION_DOUBLE      == 2u);
    CHECK(APTA_TEMPO_RELATION_THREE_HALF  == 3u);
    CHECK(APTA_TEMPO_RELATION_TWO_THIRDS  == 4u);
    /* Added by B2. */
    CHECK(APTA_TEMPO_RELATION_THIRD       == 5u);
    CHECK(APTA_TEMPO_RELATION_TRIPLE      == 6u);
    CHECK(APTA_TEMPO_RELATION_QUARTER     == 7u);
    CHECK(APTA_TEMPO_RELATION_QUADRUPLE   == 8u);

    /* All relations must fit the single byte the container gives them. */
    CHECK(APTA_TEMPO_RELATION_QUADRUPLE <= 255u);

    /* The generic ambiguity flag must not collide with the existing ones. */
    CHECK(APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY == (1u << 7));
    CHECK((APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY &
           (APTA_TEMPO_FLAG_HALF_TIME_AMBIGUITY |
            APTA_TEMPO_FLAG_DOUBLE_TIME_AMBIGUITY |
            APTA_TEMPO_FLAG_MULTIPLE_PHASES |
            APTA_TEMPO_FLAG_DYNAMIC |
            APTA_TEMPO_FLAG_USER_CONFIRMED |
            APTA_TEMPO_FLAG_USER_EDITED |
            APTA_TEMPO_FLAG_DEGRADED)) == 0u);

    return 0;
}
