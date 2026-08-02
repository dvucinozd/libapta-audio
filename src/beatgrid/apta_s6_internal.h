// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_S6_INTERNAL_H
#define APTA_S6_INTERNAL_H

#include "../core/apta_internal.h"

struct apta_internal_s6_session_state {
    apta_internal_onset_bin_t *global_bins;
    uint32_t global_bin_capacity;
    /* A1: precomputed onset flux, indexed linearly as
     * flux[bin_index - flux_base_bin]. Filled once per refresh over the whole
     * evidence range and shared by every analysis window: flux depends on the
     * window start only at the window's first bin, where the predecessor is
     * treated as absent, so apta_internal_s6_refresh() patches that single
     * boundary per window instead of refilling the array. */
    float *global_flux;
    uint32_t global_flux_capacity;
    /* Bin index that global_flux[0] corresponds to: the evidence start of the
     * refresh that filled the array. */
    uint64_t flux_base_bin;
    /* A2: evidence_end of the last refresh that actually ran the per-window
     * autocorrelation. Zero before the first one. */
    uint64_t refreshed_evidence_end;

    /* Phase 7 cooperative refresh. Pending segments and window-argmax state
     * are private until the entire evidence generation commits, so a result
     * snapshot can never observe a partially refreshed global grid. */
    apta_grid_segment_t refresh_segments[APTA_INTERNAL_GLOBAL_MAX_SEGMENTS];
    uint32_t refresh_segment_window_counts[APTA_INTERNAL_GLOBAL_MAX_SEGMENTS];
    uint64_t refresh_evidence_first;
    uint64_t refresh_evidence_end;
    uint64_t refresh_next_window;
    uint32_t refresh_segment_count;
    uint32_t refresh_window_count;
    uint32_t refresh_total_confidence;
    uint32_t refresh_valid_confidence_count;
    uint8_t refresh_stage;
    uint8_t refresh_degraded;
    uint8_t refresh_pending;
    uint8_t refresh_reserved8;

    apta_grid_segment_t segments[APTA_INTERNAL_GLOBAL_MAX_SEGMENTS];
    uint32_t segment_count;

    apta_beat_t *beats;
    uint32_t beat_count;
    uint32_t beat_capacity;

    apta_frame_range_t requested_range;
    apta_frame_range_t evidence_range;
    apta_frame_range_t applicability_range;
    apta_frame_range_t coverage_range;

    apta_grid_revision_view_t revision;

    apta_grid_representation_t representation;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    uint8_t reserved8[3];
    uint32_t flags;

    uint32_t has_global_grid;
    uint32_t has_dynamic_tempo;
    uint32_t revision_pending;
    uint32_t revision_id;
    uint32_t previous_revision_id;
    uint64_t signature;
    uint64_t mutation_serial;
    uint64_t published_serial;
};

struct apta_internal_s6_result_state {
    apta_grid_view_t global_grid;
    apta_frame_range_t *coverage_ranges;
    apta_grid_segment_t *segments;
    apta_beat_t *beats;
    apta_grid_revision_view_t revision;
};

#endif /* APTA_S6_INTERNAL_H */
