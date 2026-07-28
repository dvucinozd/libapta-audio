// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

APTA_API apta_status_t APTA_CALL apta_session_create_workspace_base(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

apta_status_t APTA_CALL apta_session_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out)
{
    if (session_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *session_out = NULL;

    if (context == NULL || config == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if ((config->flags &
         ~APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((config->flags &
         APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    return apta_session_create_workspace_base(
        context,
        config,
        session_out);
}
