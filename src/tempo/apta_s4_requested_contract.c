// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

apta_feature_mask_t apta_internal_s4_pending_features_base(
    const apta_session_t *session);

apta_status_t apta_internal_s4_build_snapshot_base(
    apta_session_t *session,
    apta_result_t *result);

apta_feature_mask_t apta_internal_s4_pending_features(
    const apta_session_t *session)
{
    apta_feature_mask_t features =
        apta_internal_s4_pending_features_base(session);

    if (session == NULL ||
        (session->config.requested_features &
         APTA_FEATURE_LOCAL_BEATGRID) == 0u) {
        features &= ~(APTA_FEATURE_LOCAL_BEATGRID |
                      APTA_FEATURE_GRID_LOCKING);
    }
    return features;
}

apta_status_t apta_internal_s4_build_snapshot(
    apta_session_t *session,
    apta_result_t *result)
{
    uint32_t saved_has_local_grid;
    apta_status_t status;

    if (session == NULL || result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((session->config.requested_features &
         APTA_FEATURE_LOCAL_BEATGRID) != 0u) {
        return apta_internal_s4_build_snapshot_base(session, result);
    }

    saved_has_local_grid = session->has_local_grid;
    session->has_local_grid = 0u;
    status = apta_internal_s4_build_snapshot_base(session, result);
    session->has_local_grid = saved_has_local_grid;
    return status;
}
