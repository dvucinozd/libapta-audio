// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

apta_status_t APTA_CALL apta_session_create_s6_guard_base(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

static int apta_s6_feature_mask_is_coherent(apta_feature_mask_t features)
{
    if ((features & APTA_INTERNAL_S6_FEATURES) != 0u &&
        (features & APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
        return 0;
    }
    if ((features & APTA_FEATURE_GLOBAL_BEATGRID) != 0u &&
        (features & APTA_FEATURE_BPM) == 0u) {
        return 0;
    }
    if ((features & APTA_FEATURE_DYNAMIC_TEMPO) != 0u &&
        (features & APTA_FEATURE_GLOBAL_BEATGRID) == 0u) {
        return 0;
    }
    return 1;
}

apta_status_t APTA_CALL apta_session_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out)
{
    if (session_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *session_out = NULL;
    if (config == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (!apta_s6_feature_mask_is_coherent(config->requested_features)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    return apta_session_create_s6_guard_base(context, config, session_out);
}
