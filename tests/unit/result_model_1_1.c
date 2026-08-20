// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#include "apta_result_pool.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void configure_session(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = 48000u;
    config->channel_count = 1u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    config->total_frames = 480000u;
    config->requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    config->flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;
}

static int check_public_constants(void)
{
    CHECK(APTA_FEATURE_MUSICAL_KEY == (UINT64_C(1) << 9));
    CHECK(APTA_FEATURE_METER_DOWNBEAT == (UINT64_C(1) << 10));
    CHECK(APTA_FEATURE_CALIBRATED_QUALITY == (UINT64_C(1) << 11));
    CHECK(APTA_KEY_MODE_MAJOR != APTA_KEY_MODE_MINOR);
    CHECK(APTA_QUALITY_FLAG_AMBIGUOUS == (1u << 0));
    CHECK(APTA_QUALITY_FLAG_DEGRADED == (1u << 1));
    CHECK(APTA_QUALITY_FLAG_OUT_OF_DOMAIN == (1u << 2));
    CHECK(APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT == (1u << 3));
    return 0;
}

static int check_absent_views(const apta_result_t *result)
{
    apta_key_view_t key;
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    apta_feature_state_t state = APTA_FEATURE_FINAL;
    apta_confidence_value_t confidence = 0u;

    apta_key_view_init(&key);
    apta_meter_view_init(&meter);
    apta_quality_view_init(&quality);

    CHECK(apta_result_get_key(result, NULL, &key) ==
          APTA_STATUS_NOT_AVAILABLE);
    CHECK(key.state == APTA_FEATURE_ABSENT);
    CHECK(key.confidence == APTA_CONFIDENCE_UNKNOWN);
    CHECK(key.candidate_count == 0u);
    CHECK(key.candidates == NULL);

    CHECK(apta_result_get_meter(result, NULL, &meter) ==
          APTA_STATUS_NOT_AVAILABLE);
    CHECK(meter.state == APTA_FEATURE_ABSENT);
    CHECK(meter.confidence == APTA_CONFIDENCE_UNKNOWN);
    CHECK(meter.segment_count == 0u);
    CHECK(meter.segments == NULL);

    CHECK(apta_result_get_quality(
              result,
              APTA_FEATURE_MUSICAL_KEY,
              &quality) == APTA_STATUS_NOT_AVAILABLE);
    CHECK(quality.feature == APTA_FEATURE_MUSICAL_KEY);
    CHECK(quality.state == APTA_FEATURE_ABSENT);
    CHECK(quality.evidence_coverage_permille ==
          APTA_EVIDENCE_COVERAGE_UNKNOWN);
    CHECK(quality.confidence == APTA_CONFIDENCE_UNKNOWN);

    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_MUSICAL_KEY,
              NULL,
              &state,
              &confidence) == APTA_STATUS_NOT_AVAILABLE);
    CHECK(state == APTA_FEATURE_ABSENT);
    CHECK(confidence == APTA_CONFIDENCE_UNKNOWN);
    return 0;
}

static int check_stored_views(apta_result_t *result)
{
    apta_key_candidate_t candidates[2] = {0};
    apta_meter_segment_t segments[2] = {0};
    apta_quality_view_t qualities[1];
    apta_key_view_t key;
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    apta_frame_range_t query;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;

    candidates[0].tonic = 9u;
    candidates[0].mode = APTA_KEY_MODE_MINOR;
    candidates[0].tuning_offset_cents = -7;
    candidates[0].score = 900u;
    candidates[0].confidence = 88u;
    candidates[1].tonic = 0u;
    candidates[1].mode = APTA_KEY_MODE_MAJOR;
    candidates[1].score = 650u;
    candidates[1].confidence = 61u;

    apta_key_view_init(&result->key);
    result->key.applicability_range.first_frame = 1200u;
    result->key.applicability_range.end_frame = 470000u;
    result->key.tonic = 9u;
    result->key.mode = APTA_KEY_MODE_MINOR;
    result->key.tuning_offset_cents = -7;
    result->key.state = APTA_FEATURE_FINAL;
    result->key.confidence = 88u;
    result->key.candidate_count = 2u;
    result->key.candidates = candidates;
    result->key_candidates = candidates;

    segments[0].struct_size = (uint32_t)sizeof(segments[0]);
    segments[0].api_version = APTA_API_VERSION;
    apta_frame_range_init(&segments[0].applicability_range);
    segments[0].applicability_range.end_frame = 240000u;
    segments[0].numerator = 4u;
    segments[0].denominator = 4u;
    segments[0].downbeat_frame = 2400u;
    segments[0].downbeat_ordinal = 0;
    segments[0].state = APTA_FEATURE_FINAL;
    segments[0].confidence = 91u;
    segments[1] = segments[0];
    segments[1].applicability_range.first_frame = 240000u;
    segments[1].applicability_range.end_frame = 480000u;
    segments[1].numerator = 3u;
    segments[1].downbeat_frame = 241200u;
    segments[1].downbeat_ordinal = 40;

    apta_meter_view_init(&result->meter);
    result->meter.numerator = 4u;
    result->meter.denominator = 4u;
    result->meter.downbeat_frame = 2400u;
    result->meter.downbeat_ordinal = 0;
    result->meter.state = APTA_FEATURE_FINAL;
    result->meter.confidence = 91u;
    result->meter.segment_count = 2u;
    result->meter.segments = segments;
    result->meter_segments = segments;

    apta_quality_view_init(&qualities[0]);
    qualities[0].feature = APTA_FEATURE_MUSICAL_KEY;
    qualities[0].calibration_model_id = 17u;
    qualities[0].evidence_coverage_permille = 875u;
    qualities[0].confidence = 82u;
    qualities[0].state = APTA_FEATURE_FINAL;
    qualities[0].flags = APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT;
    result->quality_count = 1u;
    result->quality = qualities;

    result->info.available_features =
        APTA_FEATURE_MUSICAL_KEY |
        APTA_FEATURE_METER_DOWNBEAT |
        APTA_FEATURE_CALIBRATED_QUALITY;

    apta_frame_range_init(&query);
    query.first_frame = 2000u;
    query.end_frame = 3000u;
    apta_key_view_init(&key);
    CHECK(apta_result_get_key(result, &query, &key) == APTA_STATUS_OK);
    CHECK(key.tonic == 9u);
    CHECK(key.mode == APTA_KEY_MODE_MINOR);
    CHECK(key.tuning_offset_cents == -7);
    CHECK(key.candidate_count == 2u);
    CHECK(key.candidates == candidates);
    CHECK(key.candidates[0].score == 900u);

    apta_meter_view_init(&meter);
    CHECK(apta_result_get_meter(result, &query, &meter) == APTA_STATUS_OK);
    CHECK(meter.numerator == 4u);
    CHECK(meter.denominator == 4u);
    CHECK(meter.downbeat_frame == 2400u);
    CHECK(meter.segment_count == 2u);
    CHECK(meter.segments == segments);
    CHECK(meter.segments[1].numerator == 3u);

    apta_quality_view_init(&quality);
    CHECK(apta_result_get_quality(
              result,
              APTA_FEATURE_MUSICAL_KEY,
              &quality) == APTA_STATUS_OK);
    CHECK(quality.calibration_model_id == 17u);
    CHECK(quality.evidence_coverage_permille == 875u);
    CHECK(quality.flags == APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT);

    CHECK(apta_result_get_feature_state(
              result,
              APTA_FEATURE_MUSICAL_KEY,
              NULL,
              &state,
              &confidence) == APTA_STATUS_OK);
    CHECK(state == APTA_FEATURE_FINAL);
    CHECK(confidence == 88u);
    return 0;
}

int main(void)
{
    apta_session_config_t session_config;
    apta_context_config_t context_config;
    apta_context_t *context = NULL;
    apta_internal_result_pool_control_t *pool = NULL;
    apta_result_t *result = NULL;

    CHECK(check_public_constants() == 0);
    configure_session(&session_config);
    apta_context_config_init(&context_config);
    /* Task 1 adds result representation, not native analysis capability. */
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_internal_result_pool_create(
              context,
              &session_config,
              &pool) == APTA_STATUS_OK);
    CHECK(apta_internal_result_pool_create_empty_result(
              pool,
              &session_config,
              1u,
              APTA_SESSION_CREATED,
              0u,
              1u,
              2u,
              &result) == APTA_STATUS_OK);

    CHECK(check_absent_views(result) == 0);
    CHECK(check_stored_views(result) == 0);

    result->key_candidates = NULL;
    result->meter_segments = NULL;
    result->quality = NULL;
    apta_result_release(result);
    apta_internal_result_pool_release(pool);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
