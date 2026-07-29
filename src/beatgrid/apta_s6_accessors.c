// SPDX-License-Identifier: Apache-2.0
#include "apta_s6_internal.h"

#include <string.h>

apta_status_t APTA_CALL apta_result_get_feature_state_s6_base(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out);

apta_status_t APTA_CALL apta_result_get_beatgrid_s6_base(
    const apta_result_t *result,
    apta_feature_mask_t grid_feature,
    const apta_frame_range_t *range,
    apta_grid_view_t *view_out);

static int apta_s6_ranges_overlap(
    const apta_frame_range_t *left,
    const apta_frame_range_t *right)
{
    return left->first_frame < right->end_frame &&
           right->first_frame < left->end_frame;
}

static int apta_s6_range_contains(
    const apta_frame_range_t *outer,
    const apta_frame_range_t *inner)
{
    return outer->first_frame <= inner->first_frame &&
           inner->end_frame <= outer->end_frame;
}

static apta_status_t apta_s6_validate_range(const apta_frame_range_t *range)
{
    if (range == NULL) {
        return APTA_STATUS_OK;
    }
    if (!apta_internal_validate_struct(
            range,
            sizeof(*range),
            range->struct_size,
            range->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    return range->first_frame < range->end_frame
               ? APTA_STATUS_OK
               : APTA_ERROR_INVALID_ARGUMENT;
}

apta_status_t APTA_CALL apta_result_get_feature_state(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out)
{
    const apta_grid_view_t *grid;
    apta_status_t status;

    if (feature != APTA_FEATURE_GLOBAL_BEATGRID &&
        feature != APTA_FEATURE_DYNAMIC_TEMPO) {
        return apta_result_get_feature_state_s6_base(
            result,
            feature,
            range,
            state_out,
            confidence_out);
    }
    if (result == NULL || state_out == NULL || confidence_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    status = apta_s6_validate_range(range);
    if (status < 0) {
        return status;
    }
    *state_out = APTA_FEATURE_ABSENT;
    *confidence_out = APTA_CONFIDENCE_UNKNOWN;
    if (result->s6 == NULL ||
        (result->info.available_features & feature) == 0u) {
        return APTA_STATUS_NOT_AVAILABLE;
    }
    grid = &result->s6->global_grid;
    *confidence_out = grid->confidence;
    if (range == NULL || apta_s6_range_contains(&grid->applicability_range, range)) {
        *state_out = grid->state;
        return APTA_STATUS_OK;
    }
    if (apta_s6_ranges_overlap(&grid->applicability_range, range)) {
        *state_out = APTA_FEATURE_PARTIAL;
        return APTA_STATUS_OK;
    }
    return APTA_STATUS_NOT_AVAILABLE;
}

apta_status_t APTA_CALL apta_result_get_beatgrid(
    const apta_result_t *result,
    apta_feature_mask_t grid_feature,
    const apta_frame_range_t *range,
    apta_grid_view_t *view_out)
{
    apta_status_t status;

    if (grid_feature != APTA_FEATURE_GLOBAL_BEATGRID) {
        return apta_result_get_beatgrid_s6_base(
            result,
            grid_feature,
            range,
            view_out);
    }
    if (result == NULL || view_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            view_out,
            sizeof(*view_out),
            view_out->struct_size,
            view_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    status = apta_s6_validate_range(range);
    if (status < 0) {
        return status;
    }
    if (result->s6 == NULL ||
        (result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) == 0u ||
        (range != NULL &&
         !apta_s6_ranges_overlap(
             &result->s6->global_grid.applicability_range,
             range))) {
        apta_grid_view_init(view_out);
        return APTA_STATUS_NOT_AVAILABLE;
    }
    memcpy(view_out, &result->s6->global_grid, sizeof(*view_out));
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_get_grid_revision(
    const apta_result_t *result,
    apta_grid_revision_view_t *view_out)
{
    if (result == NULL || view_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            view_out,
            sizeof(*view_out),
            view_out->struct_size,
            view_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (result->s6 == NULL ||
        (result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) == 0u) {
        apta_grid_revision_view_init(view_out);
        return APTA_STATUS_NOT_AVAILABLE;
    }
    memcpy(view_out, &result->s6->revision, sizeof(*view_out));
    return APTA_STATUS_OK;
}
