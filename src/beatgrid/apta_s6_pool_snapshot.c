// SPDX-License-Identifier: Apache-2.0
#include "apta_s6_internal.h"
#include "../core/apta_result_pool.h"

#include <string.h>

apta_status_t apta_internal_result_pool_create_session_result_s6_base(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_generation_t generation,
    apta_feature_mask_t changed_features,
    apta_result_t **result_out);

static apta_status_t apta_s6_pool_build(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_result_t *result)
{
    const apta_internal_result_pool_layout_t *layout;
    const apta_internal_s6_session_state_t *state;
    apta_internal_s6_result_state_t *snapshot;
    uint8_t *storage;
    size_t segment_bytes;
    size_t beat_bytes;
    uint32_t index;
    const int completed =
        atomic_load_explicit(&session->state, memory_order_acquire) ==
        APTA_SESSION_COMPLETED;

    if (session->s6 == NULL || !session->s6->has_global_grid) {
        return APTA_STATUS_OK;
    }
    state = session->s6;
    layout = apta_internal_result_pool_get_layout(pool);
    storage = (uint8_t *)apta_internal_result_pool_get_slot_storage(
        pool,
        result->result_pool_slot_index);
    if (layout == NULL || storage == NULL ||
        layout->global_grid_coverage_capacity < 1u ||
        state->segment_count > layout->global_grid_segment_capacity ||
        state->beat_count > layout->global_grid_beat_capacity) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    snapshot = (apta_internal_s6_result_state_t *)(void *)(
        storage + layout->global_grid_state_offset);
    memset(snapshot, 0, sizeof(*snapshot));
    result->s6 = snapshot;
    snapshot->coverage_ranges = (apta_frame_range_t *)(void *)(
        storage + layout->global_grid_coverage_offset);
    snapshot->segments = state->segment_count != 0u
                             ? (apta_grid_segment_t *)(void *)(
                                   storage + layout->global_grid_segments_offset)
                             : NULL;
    snapshot->beats = state->beat_count != 0u
                          ? (apta_beat_t *)(void *)(
                                storage + layout->global_grid_beats_offset)
                          : NULL;

    *snapshot->coverage_ranges = state->coverage_range;
    segment_bytes = (size_t)state->segment_count * sizeof(apta_grid_segment_t);
    beat_bytes = (size_t)state->beat_count * sizeof(apta_beat_t);
    if (segment_bytes != 0u) {
        memcpy(snapshot->segments, state->segments, segment_bytes);
    }
    if (beat_bytes != 0u) {
        memcpy(snapshot->beats, state->beats, beat_bytes);
    }

    apta_grid_view_init(&snapshot->global_grid);
    snapshot->global_grid.requested_range = state->requested_range;
    snapshot->global_grid.evidence_range = state->evidence_range;
    snapshot->global_grid.applicability_range = state->applicability_range;
    snapshot->global_grid.representation = state->representation;
    snapshot->global_grid.state = completed
                                      ? APTA_FEATURE_FINAL
                                      : state->state;
    snapshot->global_grid.confidence = state->confidence;
    snapshot->global_grid.coverage_range_count = 1u;
    snapshot->global_grid.coverage_ranges = snapshot->coverage_ranges;
    snapshot->global_grid.segment_count = state->segment_count;
    snapshot->global_grid.segments = snapshot->segments;
    snapshot->global_grid.beat_count = state->beat_count;
    snapshot->global_grid.beats = snapshot->beats;
    snapshot->global_grid.flags = state->flags;
    if (completed) {
        for (index = 0u; index < state->segment_count; ++index) {
            snapshot->segments[index].state = APTA_FEATURE_FINAL;
        }
    }
    snapshot->revision = state->revision;

    result->info.available_features |= APTA_FEATURE_GLOBAL_BEATGRID;
    if ((session->config.requested_features &
         APTA_FEATURE_DYNAMIC_TEMPO) != 0u) {
        result->info.available_features |= APTA_FEATURE_DYNAMIC_TEMPO;
    }
    if ((session->config.requested_features &
         APTA_FEATURE_CONFIDENCE) != 0u) {
        result->info.available_features |= APTA_FEATURE_CONFIDENCE;
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

    status = apta_internal_result_pool_create_session_result_s6_base(
        pool,
        session,
        generation,
        changed_features,
        result_out);
    if (status < 0) {
        return status;
    }
    status = apta_s6_pool_build(pool, session, *result_out);
    if (status < 0) {
        apta_internal_result_release(*result_out);
        *result_out = NULL;
    }
    return status;
}
