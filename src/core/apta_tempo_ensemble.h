// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_TEMPO_ENSEMBLE_H
#define APTA_TEMPO_ENSEMBLE_H

#include "apta_internal.h"
#include "apta_tempo_relation.h"

/*
 * APTA 1.1 tempo/grid ensemble promotion gate.
 *
 * Phase 5 demonstrated that interpreting only the three published S4
 * candidates does not generalize: a correct metrical family member can be
 * absent from that short list. The metrical-recovery path may therefore
 * consider a tempo independently proposed by S6, but S6 is not allowed to
 * overrule S4 on its own. Promotion requires all of the following:
 *
 *  - the proposal is a recognized metrical relative of the current S4 answer;
 *  - the proposal retains at least the existing S4 local-evidence floor;
 *  - a fine S4 grid fitted at the proposed period explains the onset novelty
 *    better than the currently selected grid.
 *
 * The separate close-candidate path below cannot introduce an S6-only tempo;
 * it only arbitrates between candidates S4 already produced, using the same
 * score/grid-fit gates plus strictly greater global confidence.
 */
static inline int apta_internal_tempo_ensemble_should_promote(
    apta_tempo_relation_t relation,
    uint32_t normalized_s4_score,
    float selected_grid_fit,
    float proposed_grid_fit)
{
    if (relation == APTA_TEMPO_RELATION_INDEPENDENT) {
        return 0;
    }
    if (normalized_s4_score < APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE) {
        return 0;
    }
    if (proposed_grid_fit <= 0.0f) {
        return 0;
    }
    return proposed_grid_fit > selected_grid_fit;
}

/* A close, non-metrical S6 proposal may arbitrate between candidates S4
 * already found. Unlike metrical-family recovery, this path may not introduce
 * an S6-only tempo: the global estimate must have greater confidence than the
 * current local grid, while the existing score and strict grid-fit gates still
 * apply. A confidence value of 255 is unknown, not stronger than 100. */
static inline int apta_internal_tempo_ensemble_should_promote_close(
    int existing_s4_candidate,
    apta_confidence_value_t local_confidence,
    apta_confidence_value_t global_confidence,
    uint32_t normalized_s4_score,
    float selected_grid_fit,
    float proposed_grid_fit)
{
    if (!existing_s4_candidate ||
        local_confidence == APTA_CONFIDENCE_UNKNOWN ||
        global_confidence == APTA_CONFIDENCE_UNKNOWN ||
        global_confidence <= local_confidence) {
        return 0;
    }
    if (normalized_s4_score < APTA_INTERNAL_TEMPO_ENDORSE_MIN_SCORE ||
        proposed_grid_fit <= 0.0f) {
        return 0;
    }
    return proposed_grid_fit > selected_grid_fit;
}

/* A promoted candidate becomes rank zero. Its serialized score must therefore
 * be no lower than the candidate shifted to rank one. */
static inline uint16_t apta_internal_tempo_promotion_score(
    uint16_t promoted_score,
    uint16_t next_score)
{
    return promoted_score < next_score ? next_score : promoted_score;
}

#endif
