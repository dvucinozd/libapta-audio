// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_S4_INTERNAL_H
#define APTA_S4_INTERNAL_H

#include "../core/apta_internal.h"

#ifdef APTA_INTERNAL_METER_TRACE
/* Development-only view of the immutable evidence snapshot used by the last
 * completed S4 refresh. Any output pointer may be NULL. The returned flux
 * pointer remains owned by the session. */
void apta_internal_s4_trace_get(
    const apta_session_t *session,
    const float **flux_out,
    uint32_t *flux_count_out,
    uint64_t *evidence_first_bin_out,
    const uint32_t **candidate_lags_out,
    const float **candidate_lag_offsets_out,
    uint32_t *candidate_count_out,
    uint32_t *selected_phase_out);
#endif

#endif /* APTA_S4_INTERNAL_H */
