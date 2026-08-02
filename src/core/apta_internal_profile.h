// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_INTERNAL_PROFILE_H
#define APTA_INTERNAL_PROFILE_H

#include <stdint.h>

#include <apta/apta.h>

/*
 * Internal measurement interface. This header is intentionally not installed
 * and the declarations exist only in an explicit profiling build. Keeping the
 * counters in the opaque session makes simultaneous probes independent and
 * avoids process-global debug symbols in the production library.
 */
#ifdef APTA_INTERNAL_PROFILE_S4
typedef struct {
    uint64_t process_calls;
    uint64_t refresh_scans;
    uint64_t gated_calls;
    uint64_t evidence_bins_scanned;
    uint64_t evidence_cache_hits;
    uint64_t evidence_full_scans;
    uint64_t find_evidence_ns;
    uint64_t flux_ns;
    uint64_t lag_sweep_ns;
    uint64_t refinement_ns;
    uint64_t family_scan_ns;
    uint64_t phase_search_ns;
    uint64_t publication_ns;
} apta_internal_s4_profile_t;

void apta_internal_s4_profile_reset(apta_session_t *session);

void apta_internal_s4_profile_snapshot(
    const apta_session_t *session,
    apta_internal_s4_profile_t *profile_out);
#endif

#endif
