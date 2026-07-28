// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <string.h>

apta_status_t APTA_CALL apta_result_get_feature_state_base(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out);

apta_status_t APTA_CALL apta_result_get_waveform_tile_base(
    const apta_result_t *result,
    uint32_t level_id,
    uint32_t tile_index,
    apta_waveform_tile_view_t *view_out);

static apta_status_t apta_detail_get_state(
    const apta_result_t *result,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out)
{
    apta_source_frame_t cursor;
    apta_feature_state_t weakest;
    uint32_t index;
    int have_output;

    *state_out = APTA_FEATURE_ABSENT;
    *confidence_out = APTA_CONFIDENCE_UNKNOWN;

    if ((result->info.available_features &
         APTA_FEATURE_WAVEFORM_DETAIL) == 0u ||
        result->detail_tile_count == 0u) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    if (range == NULL) {
        *state_out = APTA_FEATURE_PARTIAL;
        return APTA_STATUS_OK;
    }
    if (range->first_frame >= range->end_frame) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    cursor = range->first_frame;
    weakest = APTA_FEATURE_FINAL;
    have_output = 0;

    for (index = 0u; index < result->detail_tile_count; ++index) {
        const apta_waveform_tile_view_t *tile = &result->detail_tiles[index];

        if (tile->source_range.end_frame <= cursor) {
            continue;
        }
        if (tile->source_range.first_frame > cursor) {
            break;
        }

        have_output = 1;
        if (tile->state < weakest) {
            weakest = tile->state;
        }
        if (tile->source_range.end_frame > cursor) {
            cursor = tile->source_range.end_frame;
        }
        if (cursor >= range->end_frame) {
            *state_out = weakest;
            return APTA_STATUS_OK;
        }
    }

    if (have_output) {
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
    if (feature != APTA_FEATURE_WAVEFORM_DETAIL) {
        return apta_result_get_feature_state_base(
            result,
            feature,
            range,
            state_out,
            confidence_out);
    }

    if (result == NULL || state_out == NULL || confidence_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (range != NULL &&
        !apta_internal_validate_struct(
            range,
            sizeof(*range),
            range->struct_size,
            range->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    return apta_detail_get_state(
        result,
        range,
        state_out,
        confidence_out);
}

apta_status_t APTA_CALL apta_result_get_waveform_tile(
    const apta_result_t *result,
    uint32_t level_id,
    uint32_t tile_index,
    apta_waveform_tile_view_t *view_out)
{
    uint32_t index;

    if (level_id != APTA_INTERNAL_DETAIL_LEVEL_ID) {
        return apta_result_get_waveform_tile_base(
            result,
            level_id,
            tile_index,
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

    for (index = 0u; index < result->detail_tile_count; ++index) {
        if (result->detail_tiles[index].tile_index == tile_index) {
            memcpy(view_out, &result->detail_tiles[index], sizeof(*view_out));
            return APTA_STATUS_OK;
        }
    }

    memset(view_out, 0, sizeof(*view_out));
    view_out->struct_size = (uint32_t)sizeof(*view_out);
    view_out->api_version = APTA_API_VERSION;
    view_out->confidence = APTA_CONFIDENCE_UNKNOWN;
    return APTA_STATUS_NOT_AVAILABLE;
}
