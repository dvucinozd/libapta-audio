// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_result_pool.h"

#include <string.h>

apta_status_t apta_internal_result_pool_create_session_result_base(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_generation_t generation,
    apta_feature_mask_t changed_features,
    apta_result_t **result_out);

static apta_status_t apta_s4_pool_build(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result)
{
    const apta_internal_result_pool_layout_t *layout;
    uint8_t *storage;

    layout = apta_internal_result_pool_get_layout(pool);
    storage = (uint8_t *)apta_internal_result_pool_get_slot_storage(
        pool,
        result->result_pool_slot_index);
    if (layout == NULL || storage == NULL) {
        return APTA_ERROR_INTERNAL;
    }

    if (session->has_tempo) {
        size_t bytes;

        if (session->tempo_candidate_count >
            layout->tempo_candidate_capacity) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        bytes = (size_t)session->tempo_candidate_count *
                sizeof(apta_tempo_candidate_t);
        result->tempo_candidates =
            session->tempo_candidate_count != 0u
                ? (apta_tempo_candidate_t *)(void *)(
                      storage + layout->tempo_candidates_offset)
                : NULL;
        if (bytes != 0u) {
            memcpy(result->tempo_candidates, session->tempo_candidates, bytes);
        }

        apta_tempo_view_init(&result->tempo);
        result->tempo.selected = session->tempo_value;
        result->tempo.candidate_count = session->tempo_candidate_count;
        result->tempo.candidates = result->tempo_candidates;
        result->info.available_features |= APTA_FEATURE_BPM;
        if ((session->config.requested_features &
             APTA_FEATURE_CONFIDENCE) != 0u) {
            result->info.available_features |= APTA_FEATURE_CONFIDENCE;
        }
    }

    if (session->has_local_grid) {
        if (layout->local_grid_coverage_capacity < 1u ||
            layout->local_grid_segment_capacity < 1u) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }

        result->local_grid_coverage =
            (apta_frame_range_t *)(void *)(
                storage + layout->local_grid_coverage_offset);
        result->local_grid_segments =
            (apta_grid_segment_t *)(void *)(
                storage + layout->local_grid_segments_offset);
        *result->local_grid_coverage = session->local_grid_coverage_range;
        *result->local_grid_segments = session->local_grid_segment;

        apta_grid_view_init(&result->local_grid);
        result->local_grid.requested_range =
            session->local_grid_requested_range;
        result->local_grid.evidence_range =
            session->local_grid_evidence_range;
        result->local_grid.applicability_range =
            session->local_grid_applicability_range;
        result->local_grid.representation = APTA_GRID_REPRESENTATION_SEGMENTS;
        result->local_grid.state = session->local_grid_segment.state;
        result->local_grid.confidence = session->local_grid_segment.confidence;
        result->local_grid.coverage_range_count = 1u;
        result->local_grid.coverage_ranges = result->local_grid_coverage;
        result->local_grid.segment_count = 1u;
        result->local_grid.segments = result->local_grid_segments;
        result->local_grid.flags = session->local_grid_segment.flags;
        result->info.available_features |= APTA_FEATURE_LOCAL_BEATGRID;
        if ((session->config.requested_features &
             APTA_FEATURE_GRID_LOCKING) != 0u) {
            result->info.available_features |= APTA_FEATURE_GRID_LOCKING;
        }
    }

    return APTA_STATUS_OK;
}

apta_status_t apta_internal_result_pool_create_session_result(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_generation_t generation,
    apta_feature_mask_t changed_features,
    apta_result_t **result_out)
{
    apta_status_t status;

    status = apta_internal_result_pool_create_session_result_base(
        pool,
        session,
        generation,
        changed_features,
        result_out);
    if (status < 0) {
        return status;
    }

    status = apta_s4_pool_build(pool, session, *result_out);
    if (status < 0) {
        apta_internal_result_release(*result_out);
        *result_out = NULL;
    }
    return status;
}
