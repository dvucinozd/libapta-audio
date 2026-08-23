// SPDX-License-Identifier: Apache-2.0
/*
 * B2 / APTA 1.1: tempo relation and ensemble policy set.
 *
 * The relation is serialized as a single byte in the TEMP section, so the
 * numeric values are wire format. This pins every one of them so a future
 * renumbering fails here rather than silently changing what an existing file
 * means. The shared internal classifier is exercised at its exact ratio and
 * on both sides of its two-percent tolerance.
 *
 * The 1.1 ensemble gate is tested here as pure policy: every recognized
 * metrical relation is eligible only when the independent S4 score floor is
 * met and the proposed fine grid fits the same onset evidence strictly better.
 */
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#include "apta_tempo_ensemble.h"
#include "apta_tempo_relation.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void)
{
    size_t entry;
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

    CHECK(apta_internal_tempo_relation(0u, 120000u) ==
          APTA_TEMPO_RELATION_INDEPENDENT);
    CHECK(apta_internal_tempo_relation(120000u, 0u) ==
          APTA_TEMPO_RELATION_INDEPENDENT);
    CHECK(apta_internal_tempo_relation(120000u, 120000u) ==
          APTA_TEMPO_RELATION_INDEPENDENT);

    for (entry = 0u; entry < APTA_INTERNAL_TEMPO_RATIO_COUNT; ++entry) {
        const apta_internal_tempo_ratio_t *ratio =
            &apta_internal_tempo_ratios[entry];
        const uint32_t selected = 120000u;
        const uint32_t expected = (uint32_t)(
            (uint64_t)selected * ratio->numerator / ratio->denominator);
        const uint32_t tolerance = expected / 50u + 1u;

        CHECK(apta_internal_tempo_relation(selected, expected) ==
              ratio->relation);
        CHECK(apta_internal_tempo_relation(
                  selected,
                  expected - tolerance) == ratio->relation);
        CHECK(apta_internal_tempo_relation(
                  selected,
                  expected + tolerance) == ratio->relation);
        CHECK(apta_internal_tempo_relation(
                  selected,
                  expected - tolerance - 1u) ==
              APTA_TEMPO_RELATION_INDEPENDENT);
        CHECK(apta_internal_tempo_relation(
                  selected,
                  expected + tolerance + 1u) ==
              APTA_TEMPO_RELATION_INDEPENDENT);

        /* 1.1 ensemble: relation alone is insufficient; the proposal must
         * retain local S4 support and improve the fine-grid fit. */
        CHECK(apta_internal_tempo_ensemble_should_promote(
                  ratio->relation,
                  APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE,
                  0.20f,
                  0.21f));
        CHECK(!apta_internal_tempo_ensemble_should_promote(
                   ratio->relation,
                   APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE - 1u,
                   0.20f,
                   0.30f));
        CHECK(!apta_internal_tempo_ensemble_should_promote(
                   ratio->relation,
                   APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE,
                   0.20f,
                   0.20f));
        CHECK(!apta_internal_tempo_ensemble_should_promote(
                   ratio->relation,
                   APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE,
                   0.20f,
                   0.19f));
    }

    CHECK(!apta_internal_tempo_ensemble_should_promote(
               APTA_TEMPO_RELATION_INDEPENDENT,
               65535u,
               0.10f,
               0.50f));
    CHECK(!apta_internal_tempo_ensemble_should_promote(
               APTA_TEMPO_RELATION_DOUBLE,
               65535u,
               -0.10f,
               0.0f));

    /* Close-candidate arbitration is deliberately narrower than metrical
     * recovery: S4 must already contain the proposal and S6 confidence must
     * strictly exceed the current local-grid confidence. */
    CHECK(apta_internal_tempo_ensemble_should_promote_close(
              1, 70u, 71u,
              APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE,
              0.20f, 0.21f));
    CHECK(!apta_internal_tempo_ensemble_should_promote_close(
               0, 70u, 90u, 65535u, 0.20f, 0.30f));
    CHECK(!apta_internal_tempo_ensemble_should_promote_close(
               1, 70u, 70u, 65535u, 0.20f, 0.30f));
    CHECK(!apta_internal_tempo_ensemble_should_promote_close(
               1, 70u, APTA_CONFIDENCE_UNKNOWN,
               65535u, 0.20f, 0.30f));
    CHECK(!apta_internal_tempo_ensemble_should_promote_close(
               1, 70u, 90u,
               APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE - 1u,
               0.20f, 0.30f));
    CHECK(!apta_internal_tempo_ensemble_should_promote_close(
               1, 70u, 90u, 65535u, 0.20f, 0.20f));

    /* Promotion may select a lower-scored S4 candidate, but the published
     * TEMP order remains non-increasing and therefore round-trippable. */
    CHECK(apta_internal_tempo_promotion_score(62312u, 65535u) == 65535u);
    CHECK(apta_internal_tempo_promotion_score(65535u, 62312u) == 65535u);

    return 0;
}
