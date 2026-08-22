// SPDX-License-Identifier: Apache-2.0
#include "apta_meter_internal.h"

apta_status_t apta_internal_waveform_build_snapshot_meter_base(
    apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_waveform_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    apta_status_t status;

    status = apta_internal_waveform_build_snapshot_meter_base(session, result);
    if (status < 0) {
        return status;
    }
    status = apta_internal_meter_build_snapshot(session, result);
    if (status < 0) {
        apta_internal_context_deallocate(result->context, result->meter_segments);
        result->meter_segments = NULL;
        apta_meter_view_init(&result->meter);
    }
    return status;
}
