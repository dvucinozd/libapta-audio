// SPDX-License-Identifier: Apache-2.0
#include "apta_key_internal.h"

apta_status_t apta_internal_waveform_build_snapshot_key_base(
    apta_session_t *session,
    apta_result_t *result);

apta_status_t apta_internal_waveform_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    apta_status_t status;

    status = apta_internal_waveform_build_snapshot_key_base(session, result);
    if (status < 0) {
        return status;
    }
    status = apta_internal_key_build_snapshot(session, result);
    if (status < 0) {
        apta_internal_context_deallocate(result->context, result->key_candidates);
        result->key_candidates = NULL;
        apta_key_view_init(&result->key);
    }
    return status;
}
