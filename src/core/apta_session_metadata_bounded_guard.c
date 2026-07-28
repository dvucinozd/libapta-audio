// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

APTA_API apta_status_t APTA_CALL apta_session_set_metadata_workspace_base(
    apta_session_t *session,
    const apta_metadata_t *metadata);

apta_status_t APTA_CALL apta_session_set_metadata(
    apta_session_t *session,
    const apta_metadata_t *metadata)
{
    if (session == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((session->config.flags &
         APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    return apta_session_set_metadata_workspace_base(
        session,
        metadata);
}
