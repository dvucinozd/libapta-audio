// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <string.h>

apta_status_t APTA_CALL apta_result_get_feature_state_s4_base(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out);

static int apta_s4_ranges_overlap(
    const apta_frame_range_t *left,
    const apta_frame_range_t *right)
{
    return left->first_frame < right->end_frame &&
           right->first_frame < left->end_frame;
}

static int apta_s4_range_contains(
    const apta_frame_range_t *outer,
    const apta_frame_range_t *inner)
{
    return outer->first_frame <= inner->first_frame &&
           inner->end_frame <= outer->end_frame;
}

static apta_status_t apta_s4_validate_range(const apta_frame_range_t *range)
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

static apta_status_t apta_s4_feature_state(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out)
{
    const apta_frame_range_t *scope;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;

    *state_out = APTA_FEATURE_ABSENT;
    *confidence_out = APTA_CONFIDENCE_UNKNOWN;
    if ((result->info.available_features & feature) == 0u) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    if (feature == APTA_FEATURE_BPM || feature == APTA_FEATURE_CONFIDENCE) {
        scope = &result->tempo.selected.applicability_range;
        state = result->tempo.selected.state;
        confidence = result->tempo.selected.confidence;
    } else {
        scope = &result->local_grid.applicability_range;
        state = result->local_grid.state;
        confidence = result->local_grid.confidence;
    }

    *confidence_out = confidence;
    if (range == NULL || apta_s4_range_contains(scope, range)) {
        *state_out = state;
        return APTA_STATUS_OK;
    }
    if (apta_s4_ranges_overlap(scope, range)) {
        *state_out = APTA_FEATURE_PARTIAL;
        return APTA_STATUS_OK;
    }
    return APTA_STATUS_NOT_AVAILABLE;
}

apta_status_t APTA_CALL apta_result_get_feature_state(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out)
{
    apta_status_t status;
    int answered_here;

    if (result == NULL || state_out == NULL || confidence_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    /* A4: CONFIDENCE is no longer owned by S4. Answer it from the tempo
     * estimate only when there is one; otherwise fall through so the waveform
     * layer can report its own coverage confidence. */
    answered_here =
        feature == APTA_FEATURE_BPM ||
        feature == APTA_FEATURE_LOCAL_BEATGRID ||
        feature == APTA_FEATURE_GRID_LOCKING ||
        (feature == APTA_FEATURE_CONFIDENCE &&
         (result->info.available_features & APTA_FEATURE_BPM) != 0u);

    if (!answered_here) {
        return apta_result_get_feature_state_s4_base(
            result,
            feature,
            range,
            state_out,
            confidence_out);
    }
    status = apta_s4_validate_range(range);
    if (status < 0) {
        return status;
    }
    return apta_s4_feature_state(
        result,
        feature,
        range,
        state_out,
        confidence_out);
}

apta_status_t APTA_CALL apta_result_get_tempo(
    const apta_result_t *result,
    const apta_frame_range_t *range,
    apta_tempo_view_t *view_out)
{
    apta_status_t status;

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
    status = apta_s4_validate_range(range);
    if (status < 0) {
        return status;
    }

    if ((result->info.available_features & APTA_FEATURE_BPM) == 0u ||
        (range != NULL &&
         !apta_s4_ranges_overlap(
             &result->tempo.selected.applicability_range,
             range))) {
        apta_tempo_view_init(view_out);
        return APTA_STATUS_NOT_AVAILABLE;
    }

    memcpy(view_out, &result->tempo, sizeof(*view_out));
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_get_beatgrid(
    const apta_result_t *result,
    apta_feature_mask_t grid_feature,
    const apta_frame_range_t *range,
    apta_grid_view_t *view_out)
{
    apta_status_t status;

    if (result == NULL || view_out == NULL ||
        grid_feature != APTA_FEATURE_LOCAL_BEATGRID) {
        return grid_feature == APTA_FEATURE_GLOBAL_BEATGRID
                   ? APTA_STATUS_NOT_AVAILABLE
                   : APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            view_out,
            sizeof(*view_out),
            view_out->struct_size,
            view_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    status = apta_s4_validate_range(range);
    if (status < 0) {
        return status;
    }

    if ((result->info.available_features &
         APTA_FEATURE_LOCAL_BEATGRID) == 0u ||
        (range != NULL &&
         !apta_s4_ranges_overlap(
             &result->local_grid.applicability_range,
             range))) {
        apta_grid_view_init(view_out);
        return APTA_STATUS_NOT_AVAILABLE;
    }

    memcpy(view_out, &result->local_grid, sizeof(*view_out));
    return APTA_STATUS_OK;
}
