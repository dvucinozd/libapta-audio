// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <string.h>

#include <apta/apta.h>

#include "apta_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void set_range(
    apta_frame_range_t *range,
    apta_source_frame_t first,
    apta_source_frame_t end)
{
    apta_frame_range_init(range);
    range->first_frame = first;
    range->end_frame = end;
}

static void prepare_result(
    apta_result_t *result,
    apta_meter_segment_t segments[3],
    apta_quality_view_t quality[1])
{
    memset(result, 0, sizeof(*result));
    result->info.available_features =
        APTA_FEATURE_MUSICAL_KEY |
        APTA_FEATURE_METER_DOWNBEAT |
        APTA_FEATURE_CALIBRATED_QUALITY;

    apta_key_view_init(&result->key);
    set_range(&result->key.applicability_range, 100u, 300u);
    result->key.state = APTA_FEATURE_FINAL;
    result->key.confidence = 88u;

    memset(segments, 0, 3u * sizeof(*segments));
    set_range(&segments[0].applicability_range, 100u, 200u);
    set_range(&segments[1].applicability_range, 200u, 300u);
    set_range(&segments[2].applicability_range, 400u, 500u);
    segments[0].state = APTA_FEATURE_FINAL;
    segments[1].state = APTA_FEATURE_FINAL;
    segments[2].state = APTA_FEATURE_FINAL;

    apta_meter_view_init(&result->meter);
    result->meter.state = APTA_FEATURE_FINAL;
    result->meter.confidence = 91u;
    result->meter.segment_count = 3u;
    result->meter.segments = segments;

    apta_quality_view_init(&quality[0]);
    quality[0].feature = APTA_FEATURE_MUSICAL_KEY;
    quality[0].state = APTA_FEATURE_STABLE;
    quality[0].confidence = 80u;
    result->quality_count = 1u;
    result->quality = quality;
}

static int check_invalid_ranges(apta_result_t *result)
{
    const apta_feature_mask_t features[] = {
        APTA_FEATURE_MUSICAL_KEY,
        APTA_FEATURE_METER_DOWNBEAT,
        APTA_FEATURE_CALIBRATED_QUALITY
    };
    apta_frame_range_t range;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    apta_key_view_t key;
    apta_meter_view_t meter;
    size_t index;

    set_range(&range, 200u, 200u);
    for (index = 0u; index < sizeof(features) / sizeof(features[0]); ++index) {
        CHECK(apta_result_get_feature_state(
                  result, features[index], &range, &state, &confidence) ==
              APTA_ERROR_INVALID_ARGUMENT);
    }
    apta_key_view_init(&key);
    CHECK(apta_result_get_key(result, &range, &key) ==
          APTA_ERROR_INVALID_ARGUMENT);
    apta_meter_view_init(&meter);
    CHECK(apta_result_get_meter(result, &range, &meter) ==
          APTA_ERROR_INVALID_ARGUMENT);

    set_range(&range, 201u, 200u);
    for (index = 0u; index < sizeof(features) / sizeof(features[0]); ++index) {
        CHECK(apta_result_get_feature_state(
                  result, features[index], &range, &state, &confidence) ==
              APTA_ERROR_INVALID_ARGUMENT);
    }
    return 0;
}

static int check_meter_coverage(apta_result_t *result)
{
    apta_frame_range_t range;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    apta_meter_view_t view;

    set_range(&range, 120u, 180u);
    CHECK(apta_result_get_feature_state(
              result, APTA_FEATURE_METER_DOWNBEAT, &range,
              &state, &confidence) == APTA_STATUS_OK);
    CHECK(state == APTA_FEATURE_FINAL);
    CHECK(confidence == 91u);

    set_range(&range, 150u, 250u);
    CHECK(apta_result_get_feature_state(
              result, APTA_FEATURE_METER_DOWNBEAT, &range,
              &state, &confidence) == APTA_STATUS_OK);
    CHECK(state == APTA_FEATURE_FINAL);

    set_range(&range, 150u, 350u);
    CHECK(apta_result_get_feature_state(
              result, APTA_FEATURE_METER_DOWNBEAT, &range,
              &state, &confidence) == APTA_STATUS_OK);
    CHECK(state == APTA_FEATURE_PARTIAL);

    set_range(&range, 320u, 380u);
    CHECK(apta_result_get_feature_state(
              result, APTA_FEATURE_METER_DOWNBEAT, &range,
              &state, &confidence) == APTA_STATUS_NOT_AVAILABLE);
    CHECK(state == APTA_FEATURE_ABSENT);
    CHECK(confidence == APTA_CONFIDENCE_UNKNOWN);

    set_range(&range, 600u, 700u);
    CHECK(apta_result_get_feature_state(
              result, APTA_FEATURE_METER_DOWNBEAT, &range,
              &state, &confidence) == APTA_STATUS_NOT_AVAILABLE);

    apta_meter_view_init(&view);
    CHECK(apta_result_get_meter(result, &range, &view) ==
          APTA_STATUS_NOT_AVAILABLE);
    CHECK(view.state == APTA_FEATURE_ABSENT);
    CHECK(view.segment_count == 0u);
    CHECK(view.segments == NULL);
    return 0;
}

static int check_invalid_inputs(apta_result_t *result)
{
    apta_frame_range_t bad_range;
    apta_key_view_t key;
    apta_meter_view_t meter;
    apta_quality_view_t quality;

    apta_key_view_init(&key);
    apta_meter_view_init(&meter);
    apta_quality_view_init(&quality);
    CHECK(apta_result_get_key(NULL, NULL, &key) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_get_key(result, NULL, NULL) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_get_meter(NULL, NULL, &meter) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_get_meter(result, NULL, NULL) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_get_quality(result, 0u, &quality) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_get_quality(
              result,
              APTA_FEATURE_MUSICAL_KEY | APTA_FEATURE_METER_DOWNBEAT,
              &quality) == APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_get_quality(
              result, APTA_FEATURE_CALIBRATED_QUALITY, &quality) ==
          APTA_ERROR_INVALID_ARGUMENT);
    CHECK(apta_result_get_quality(
              result, UINT64_C(1) << 63, &quality) ==
          APTA_ERROR_INVALID_ARGUMENT);

    apta_frame_range_init(&bad_range);
    bad_range.struct_size = 0u;
    CHECK(apta_result_get_key(result, &bad_range, &key) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    key.struct_size = 0u;
    CHECK(apta_result_get_key(result, NULL, &key) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    meter.api_version = UINT32_MAX;
    CHECK(apta_result_get_meter(result, NULL, &meter) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    quality.struct_size = 0u;
    CHECK(apta_result_get_quality(
              result, APTA_FEATURE_MUSICAL_KEY, &quality) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    return 0;
}

int main(void)
{
    apta_result_t result;
    apta_meter_segment_t segments[3];
    apta_quality_view_t quality[1];

    prepare_result(&result, segments, quality);
    CHECK(check_meter_coverage(&result) == 0);
    CHECK(check_invalid_ranges(&result) == 0);
    CHECK(check_invalid_inputs(&result) == 0);
    return 0;
}
