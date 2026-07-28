// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

apta_status_t apta_internal_waveform_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    apta_status_t status;

    status = apta_internal_waveform_build_overview_snapshot(session, result);
    if (status < 0) {
        return status;
    }

    return apta_internal_detail_build_snapshot(session, result);
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
}
