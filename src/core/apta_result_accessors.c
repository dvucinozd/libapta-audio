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

static int apta_result_ranges_overlap(
    const apta_frame_range_t *left,
    const apta_frame_range_t *right)
{
    return left->first_frame < right->end_frame &&
           right->first_frame < left->end_frame;
}

static int apta_result_range_contains(
    const apta_frame_range_t *outer,
    const apta_frame_range_t *inner)
{
    return outer->first_frame <= inner->first_frame &&
           inner->end_frame <= outer->end_frame;
}

static apta_status_t apta_result_validate_range(
    const apta_frame_range_t *range)
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

static const apta_quality_view_t *apta_result_find_quality(
    const apta_result_t *result,
    apta_feature_mask_t feature)
{
    uint32_t index;

    for (index = 0u; index < result->quality_count; ++index) {
        if (result->quality[index].feature == feature) {
            return &result->quality[index];
        }
    }
    return NULL;
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

    if (feature == APTA_FEATURE_MUSICAL_KEY) {
        if ((result->info.available_features & feature) == 0u) {
            return APTA_STATUS_NOT_AVAILABLE;
        }
        *confidence_out = result->key.confidence;
        if (range == NULL || apta_result_range_contains(
                                 &result->key.applicability_range,
                                 range)) {
            *state_out = result->key.state;
            return APTA_STATUS_OK;
        }
        if (apta_result_ranges_overlap(
                &result->key.applicability_range,
                range)) {
            *state_out = APTA_FEATURE_PARTIAL;
            return APTA_STATUS_OK;
        }
        return APTA_STATUS_NOT_AVAILABLE;
    }

    if (feature == APTA_FEATURE_METER_DOWNBEAT) {
        if ((result->info.available_features & feature) == 0u) {
            return APTA_STATUS_NOT_AVAILABLE;
        }
        *state_out = result->meter.state;
        *confidence_out = result->meter.confidence;
        return APTA_STATUS_OK;
    }

    if (feature == APTA_FEATURE_CALIBRATED_QUALITY) {
        if ((result->info.available_features & feature) == 0u ||
            result->quality_count == 0u || result->quality == NULL) {
            return APTA_STATUS_NOT_AVAILABLE;
        }
        *state_out = result->quality[0].state;
        *confidence_out = result->quality[0].confidence;
        return APTA_STATUS_OK;
    }

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

apta_status_t APTA_CALL apta_result_get_key(
    const apta_result_t *result,
    const apta_frame_range_t *range,
    apta_key_view_t *view_out)
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
    status = apta_result_validate_range(range);
    if (status < 0) {
        return status;
    }
    if ((result->info.available_features & APTA_FEATURE_MUSICAL_KEY) == 0u ||
        (range != NULL && !apta_result_ranges_overlap(
                              &result->key.applicability_range,
                              range))) {
        apta_key_view_init(view_out);
        return APTA_STATUS_NOT_AVAILABLE;
    }
    memcpy(view_out, &result->key, sizeof(*view_out));
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_get_meter(
    const apta_result_t *result,
    const apta_frame_range_t *range,
    apta_meter_view_t *view_out)
{
    apta_status_t status;
    uint32_t index;
    int overlaps = 0;

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
    status = apta_result_validate_range(range);
    if (status < 0) {
        return status;
    }
    if (range == NULL || result->meter.segment_count == 0u) {
        overlaps = 1;
    } else {
        for (index = 0u; index < result->meter.segment_count; ++index) {
            if (apta_result_ranges_overlap(
                    &result->meter.segments[index].applicability_range,
                    range)) {
                overlaps = 1;
                break;
            }
        }
    }
    if ((result->info.available_features &
         APTA_FEATURE_METER_DOWNBEAT) == 0u || !overlaps) {
        apta_meter_view_init(view_out);
        return APTA_STATUS_NOT_AVAILABLE;
    }
    memcpy(view_out, &result->meter, sizeof(*view_out));
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_get_quality(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    apta_quality_view_t *view_out)
{
    const apta_quality_view_t *quality;

    if (result == NULL || view_out == NULL || feature == 0u ||
        (feature & (feature - 1u)) != 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            view_out,
            sizeof(*view_out),
            view_out->struct_size,
            view_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    quality = (result->info.available_features &
               APTA_FEATURE_CALIBRATED_QUALITY) != 0u &&
                      result->quality != NULL
                  ? apta_result_find_quality(result, feature)
                  : NULL;
    if (quality == NULL) {
        apta_quality_view_init(view_out);
        view_out->feature = feature;
        return APTA_STATUS_NOT_AVAILABLE;
    }
    memcpy(view_out, quality, sizeof(*view_out));
    return APTA_STATUS_OK;
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
