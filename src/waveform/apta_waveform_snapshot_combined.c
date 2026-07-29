// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../beatgrid/apta_s6_internal.h"

static void apta_finalize_s4_result(
    const apta_session_t *session,
    apta_result_t *result)
{
    if (atomic_load_explicit(&session->state, memory_order_acquire) !=
        APTA_SESSION_COMPLETED) {
        return;
    }
    if ((result->info.available_features & APTA_FEATURE_BPM) != 0u) {
        result->tempo.selected.state = APTA_FEATURE_FINAL;
    }
    if ((result->info.available_features & APTA_FEATURE_LOCAL_BEATGRID) != 0u) {
        result->local_grid.state = APTA_FEATURE_FINAL;
        if (result->local_grid_segments != NULL &&
            result->local_grid.segment_count != 0u) {
            result->local_grid_segments[0].state = APTA_FEATURE_FINAL;
        }
    }
}

static void apta_finalize_s6_result(
    const apta_session_t *session,
    apta_result_t *result)
{
    uint32_t index;

    if (atomic_load_explicit(&session->state, memory_order_acquire) !=
            APTA_SESSION_COMPLETED ||
        result->s6 == NULL) {
        return;
    }
    result->s6->global_grid.state = APTA_FEATURE_FINAL;
    for (index = 0u; index < result->s6->global_grid.segment_count; ++index) {
        result->s6->segments[index].state = APTA_FEATURE_FINAL;
    }
}

apta_status_t apta_internal_waveform_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    apta_status_t status;

    status = apta_internal_waveform_build_overview_snapshot(session, result);
    if (status < 0) {
        return status;
    }

    status = apta_internal_detail_build_snapshot(session, result);
    if (status < 0) {
        return status;
    }

    status = apta_internal_s4_build_snapshot(session, result);
    if (status < 0) {
        return status;
    }
    apta_finalize_s4_result(session, result);

    status = apta_internal_s6_build_snapshot(session, result);
    if (status < 0) {
        return status;
    }
    apta_finalize_s6_result(session, result);
    return APTA_STATUS_OK;
}

void apta_internal_waveform_cleanup_result(apta_result_t *result)
{
    if (result == NULL) {
        return;
    }

    apta_internal_waveform_cleanup_result_base(result);
    apta_internal_context_deallocate(result->context, result->detail_tiles);
    apta_internal_context_deallocate(result->context, result->detail_columns);
    result->detail_tiles = NULL;
    result->detail_columns = NULL;
    result->detail_tile_count = 0u;
    apta_internal_s4_cleanup_result(result);
    apta_internal_s6_cleanup_result(result);
}
