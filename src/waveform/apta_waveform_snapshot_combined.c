// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

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
    if (status >= 0) {
        apta_finalize_s4_result(session, result);
    }
    return status;
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
}
