// SPDX-License-Identifier: Apache-2.0
#include "apta_key_internal.h"

apta_status_t apta_internal_result_pool_create_session_result_key_base(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_generation_t generation,
    apta_feature_mask_t changed_features,
    apta_result_t **result_out);

apta_status_t apta_internal_result_pool_create_session_result(
    apta_internal_result_pool_control_t *pool,
    const apta_session_t *session,
    apta_generation_t generation,
    apta_feature_mask_t changed_features,
    apta_result_t **result_out)
{
    apta_status_t status;

    status = apta_internal_result_pool_create_session_result_key_base(
        pool,
        session,
        generation,
        changed_features,
        result_out);
    if (status < 0) {
        return status;
    }
    status = apta_internal_key_pool_build(pool, session, *result_out);
    if (status < 0) {
        apta_internal_result_release(*result_out);
        *result_out = NULL;
    }
    return status;
}
