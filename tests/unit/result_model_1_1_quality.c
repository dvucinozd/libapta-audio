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
    apta_meter_segment_t *segment,
    apta_quality_view_t quality[2],
    int reverse)
{
    apta_quality_view_t key_quality;
    apta_quality_view_t meter_quality;

    memset(result, 0, sizeof(*result));
    result->info.available_features =
        APTA_FEATURE_MUSICAL_KEY |
        APTA_FEATURE_METER_DOWNBEAT |
        APTA_FEATURE_CALIBRATED_QUALITY;

    apta_key_view_init(&result->key);
    set_range(&result->key.applicability_range, 100u, 200u);
    result->key.state = APTA_FEATURE_FINAL;

    memset(segment, 0, sizeof(*segment));
    set_range(&segment->applicability_range, 200u, 300u);
    segment->state = APTA_FEATURE_FINAL;
    apta_meter_view_init(&result->meter);
    result->meter.state = APTA_FEATURE_FINAL;
    result->meter.segment_count = 1u;
    result->meter.segments = segment;

    apta_quality_view_init(&key_quality);
    key_quality.feature = APTA_FEATURE_MUSICAL_KEY;
    key_quality.state = APTA_FEATURE_STABLE;
    key_quality.confidence = 80u;
    apta_quality_view_init(&meter_quality);
    meter_quality.feature = APTA_FEATURE_METER_DOWNBEAT;
    meter_quality.state = APTA_FEATURE_FINAL;
    meter_quality.confidence = 90u;

    quality[reverse ? 1 : 0] = key_quality;
    quality[reverse ? 0 : 1] = meter_quality;
    result->quality_count = 2u;
    result->quality = quality;
}

static int expect_quality_state(
    apta_result_t *result,
    const apta_frame_range_t *range,
    apta_feature_state_t expected_state,
    apta_confidence_value_t expected_confidence,
    apta_status_t expected_status)
{
    apta_feature_state_t state = APTA_FEATURE_FAILED;
    apta_confidence_value_t confidence = 0u;

    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_CALIBRATED_QUALITY,
              range,
              &state,
              &confidence) == expected_status);
    CHECK(state == expected_state);
    CHECK(confidence == expected_confidence);
    return 0;
}

static int check_order(int reverse)
{
    apta_result_t result;
    apta_meter_segment_t segment;
    apta_quality_view_t quality[2];
    apta_quality_view_t selected;
    apta_frame_range_t range;

    prepare_result(&result, &segment, quality, reverse);
    CHECK(expect_quality_state(
              &result, NULL, APTA_FEATURE_STABLE, 80u,
              APTA_STATUS_OK) == 0);

    set_range(&range, 120u, 180u);
    CHECK(expect_quality_state(
              &result, &range, APTA_FEATURE_STABLE, 80u,
              APTA_STATUS_OK) == 0);

    set_range(&range, 220u, 280u);
    CHECK(expect_quality_state(
              &result, &range, APTA_FEATURE_FINAL, 90u,
              APTA_STATUS_OK) == 0);

    set_range(&range, 150u, 250u);
    CHECK(expect_quality_state(
              &result, &range, APTA_FEATURE_STABLE, 80u,
              APTA_STATUS_OK) == 0);

    set_range(&range, 400u, 500u);
    CHECK(expect_quality_state(
              &result, &range, APTA_FEATURE_ABSENT,
              APTA_CONFIDENCE_UNKNOWN,
              APTA_STATUS_NOT_AVAILABLE) == 0);

    apta_quality_view_init(&selected);
    CHECK(apta_result_get_quality(
              &result, APTA_FEATURE_MUSICAL_KEY, &selected) ==
          APTA_STATUS_OK);
    CHECK(selected.state == APTA_FEATURE_STABLE);
    CHECK(selected.confidence == 80u);
    return 0;
}

static int check_unknown_confidence_is_conservative(void)
{
    apta_result_t result;
    apta_meter_segment_t segment;
    apta_quality_view_t quality[2];

    prepare_result(&result, &segment, quality, 0);
    quality[0].confidence = APTA_CONFIDENCE_UNKNOWN;
    CHECK(expect_quality_state(
              &result, NULL, APTA_FEATURE_STABLE,
              APTA_CONFIDENCE_UNKNOWN, APTA_STATUS_OK) == 0);

    quality[0].state = APTA_FEATURE_FINAL;
    quality[0].confidence = 80u;
    quality[1].state = APTA_FEATURE_FAILED;
    quality[1].confidence = 90u;
    CHECK(expect_quality_state(
              &result, NULL, APTA_FEATURE_FAILED, 80u,
              APTA_STATUS_OK) == 0);
    return 0;
}

static int check_unavailable_target_is_filtered(void)
{
    apta_result_t result;
    apta_meter_segment_t segment;
    apta_quality_view_t quality[2];

    prepare_result(&result, &segment, quality, 0);
    result.info.available_features &= ~APTA_FEATURE_METER_DOWNBEAT;
    quality[1].state = APTA_FEATURE_FAILED;
    quality[1].confidence = 10u;
    CHECK(expect_quality_state(
              &result, NULL, APTA_FEATURE_STABLE, 80u,
              APTA_STATUS_OK) == 0);
    return 0;
}

static int check_waveform_target_coverage(void)
{
    apta_result_t result;
    apta_waveform_span_t span;
    apta_quality_view_t quality[1];
    apta_frame_range_t range;

    memset(&result, 0, sizeof(result));
    memset(&span, 0, sizeof(span));
    set_range(&span.source_range, 100u, 200u);
    result.overview.span_count = 1u;
    result.overview.spans = &span;
    result.info.available_features =
        APTA_FEATURE_WAVEFORM_OVERVIEW |
        APTA_FEATURE_WAVEFORM_3BAND |
        APTA_FEATURE_CALIBRATED_QUALITY;

    apta_quality_view_init(&quality[0]);
    quality[0].feature = APTA_FEATURE_WAVEFORM_3BAND;
    quality[0].state = APTA_FEATURE_FINAL;
    quality[0].confidence = 90u;
    result.quality_count = 1u;
    result.quality = quality;

    set_range(&range, 300u, 400u);
    CHECK(expect_quality_state(
              &result, &range, APTA_FEATURE_ABSENT,
              APTA_CONFIDENCE_UNKNOWN,
              APTA_STATUS_NOT_AVAILABLE) == 0);

    result.info.available_features &= ~APTA_FEATURE_WAVEFORM_3BAND;
    result.info.available_features |= APTA_FEATURE_CONFIDENCE;
    quality[0].feature = APTA_FEATURE_CONFIDENCE;
    quality[0].state = APTA_FEATURE_STABLE;
    quality[0].confidence = 80u;

    set_range(&range, 120u, 180u);
    CHECK(expect_quality_state(
              &result, &range, APTA_FEATURE_STABLE, 80u,
              APTA_STATUS_OK) == 0);
    return 0;
}

int main(void)
{
    CHECK(check_order(0) == 0);
    CHECK(check_order(1) == 0);
    CHECK(check_unknown_confidence_is_conservative() == 0);
    CHECK(check_unavailable_target_is_filtered() == 0);
    CHECK(check_waveform_target_coverage() == 0);
    return 0;
}
