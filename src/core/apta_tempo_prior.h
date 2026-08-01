// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_CORE_TEMPO_PRIOR_H
#define APTA_CORE_TEMPO_PRIOR_H

#include <math.h>
#include <stdint.h>

#include "apta_internal.h"

/*
 * B1's preferred-tempo prior, shared by the local and global estimators.
 *
 * A log-normal weight centred on the tempo a listener is most likely to hear as
 * the beat. Autocorrelation peaks at every multiple of the true period, so
 * without a prior the argmax is free to pick any member of the octave family,
 * and on real music it frequently picks the wrong one.
 *
 * It lives in a header as `static inline` rather than in a shared source file
 * so that adding it does not require a new translation unit in both the root
 * and ESP-IDF build files, where each source carries its own symbol-renaming
 * definitions.
 *
 * S4 had this from B1 onward; S6 did not, and measuring S6 on real audio for
 * the first time showed what that costs -- 38 of 68 tracks landed on a
 * metrical relation of the truth rather than the truth.
 */
static inline float apta_internal_tempo_prior(uint32_t tempo_millibpm)
{
    float ratio;
    float logarithm;

    if (tempo_millibpm == 0u) {
        return 0.0f;
    }
    ratio = (float)tempo_millibpm /
            (float)APTA_INTERNAL_TEMPO_PRIOR_CENTRE_MILLIBPM;
    logarithm = logf(ratio) / APTA_INTERNAL_TEMPO_PRIOR_WIDTH;
    return expf(-0.5f * logarithm * logarithm);
}

#endif /* APTA_CORE_TEMPO_PRIOR_H */
