// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <string.h>

static apta_status_t apta_prepare_unavailable_view(
    void *view,
    size_t view_size,
    uint32_t struct_size,
    uint32_t api_version)
{
    if (view == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (!apta_internal_validate_struct(
            view,
            view_size,
            struct_size,
            api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    memset(view, 0, view_size);
    *(uint32_t *)view = (uint32_t)view_size;
    *((uint32_t *)view + 1) = APTA_API_VERSION;
    return APTA_STATUS_NOT_AVAILABLE;
}

static int apta_overview_range_fully_covered(
    const apta_result_t *result,
    const apta_frame_range_t *range)
{
    apta_source_frame_t cursor;
    uint32_t index;

    cursor = range->first_frame;
    for (index = 0u; index < result->overview.span_count; ++index) {
        const apta_frame_range_t *span =
            &result->overview.spans[index].source_range;

        if (span->end_frame <= cursor) {
            continue;
        }
        if (span->first_frame > cursor) {
            return 0;
        }
        cursor = span->end_frame;
        if (cursor >= range->end_frame) {
            return 1;
        }
    }

    return 0;
}

static int apta_overview_range_has_coverage(
    const apta_result_t *result,
    const apta_frame_range_t *range)
{
    uint32_t index;

    for (index = 0u; index < result->overview.span_count; ++index) {
        const apta_frame_range_t *span =
            &result->overview.spans[index].source_range;
        if (span->first_frame < range->end_frame &&
            range->first_frame < span->end_frame) {
            return 1;
        }
    }

    return 0;
}

apta_status_t APTA_CALL apta_result_get_feature_state(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out)
{
    if (result == NULL || feature == 0u ||
        state_out == NULL || confidence_out == NULL) {
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

    *state_out = APTA_FEATURE_ABSENT;
    *confidence_out = APTA_CONFIDENCE_UNKNOWN;

    if (feature != APTA_FEATURE_WAVEFORM_OVERVIEW ||
        (result->info.available_features & feature) == 0u) {
        return APTA_STATUS_NOT_AVAILABLE;
    }

    *confidence_out = result->overview.confidence;
    if (range == NULL) {
        *state_out = result->overview.state;
        return APTA_STATUS_OK;
    }

    if (range->first_frame >= range->end_frame) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (apta_overview_range_fully_covered(result, range)) {
        *state_out = result->overview.state == APTA_FEATURE_FINAL
                         ? APTA_FEATURE_FINAL
                         : APTA_FEATURE_STABLE;
        return APTA_STATUS_OK;
    }

    if (apta_overview_range_has_coverage(result, range)) {
        *state_out = APTA_FEATURE_PARTIAL;
        return APTA_STATUS_OK;
    }

    return APTA_STATUS_NOT_AVAILABLE;
}

apta_status_t APTA_CALL apta_result_get_waveform_overview(
    const apta_result_t *result,
    uint32_t level_id,
    apta_waveform_overview_view_t *view_out)
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

    if (level_id != 0u ||
        (result->info.available_features &
         APTA_FEATURE_WAVEFORM_OVERVIEW) == 0u) {
        return apta_prepare_unavailable_view(
            view_out,
            sizeof(*view_out),
            view_out->struct_size,
            view_out->api_version);
    }

    memcpy(view_out, &result->overview, sizeof(*view_out));
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_get_waveform_tile(
    const apta_result_t *result,
    uint32_t level_id,
    uint32_t tile_index,
    apta_waveform_tile_view_t *view_out)
{
    (void)level_id;
    (void)tile_index;

    if (result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    return apta_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}

apta_status_t APTA_CALL apta_result_get_tempo(
    const apta_result_t *result,
    const apta_frame_range_t *range,
    apta_tempo_view_t *view_out)
{
    if (result == NULL) {
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

    return apta_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}

apta_status_t APTA_CALL apta_result_get_beatgrid(
    const apta_result_t *result,
    apta_feature_mask_t grid_feature,
    const apta_frame_range_t *range,
    apta_grid_view_t *view_out)
{
    if (result == NULL ||
        (grid_feature != APTA_FEATURE_LOCAL_BEATGRID &&
         grid_feature != APTA_FEATURE_GLOBAL_BEATGRID)) {
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

    return apta_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}

uint32_t APTA_CALL apta_result_get_diagnostic_count(
    const apta_result_t *result)
{
    return result != NULL ? result->info.diagnostic_count : 0u;
}

apta_status_t APTA_CALL apta_result_get_diagnostic(
    const apta_result_t *result,
    uint32_t index,
    apta_diagnostic_view_t *view_out)
{
    (void)index;

    if (result == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    return apta_prepare_unavailable_view(
        view_out,
        sizeof(*view_out),
        view_out != NULL ? view_out->struct_size : 0u,
        view_out != NULL ? view_out->api_version : 0u);
}
