// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_RESULT_H
#define APTA_RESULT_H

#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APTA_WAVEFORM_COLUMN_VALID        (1u << 0)
#define APTA_WAVEFORM_COLUMN_PROVISIONAL  (1u << 1)
#define APTA_WAVEFORM_COLUMN_CLIPPED      (1u << 2)
#define APTA_WAVEFORM_COLUMN_HAS_3BAND    (1u << 3)
#define APTA_WAVEFORM_COLUMN_DEGRADED     (1u << 4)

#define APTA_TEMPO_FLAG_HALF_TIME_AMBIGUITY   (1u << 0)
#define APTA_TEMPO_FLAG_DOUBLE_TIME_AMBIGUITY (1u << 1)
#define APTA_TEMPO_FLAG_MULTIPLE_PHASES       (1u << 2)
#define APTA_TEMPO_FLAG_DYNAMIC               (1u << 3)
#define APTA_TEMPO_FLAG_USER_CONFIRMED        (1u << 4)
#define APTA_TEMPO_FLAG_USER_EDITED           (1u << 5)
#define APTA_TEMPO_FLAG_DEGRADED              (1u << 6)
/*
 * B2: set when any candidate stands in a metrical relation to the selected
 * tempo and scores comparably. The two flags above name only half and double;
 * with eight relations defined, one flag per ratio does not scale, and the
 * relations that actually dominate in practice -- thirds and two-thirds --
 * had no flag at all. Those two remain set for their own relations so
 * existing hosts keep working.
 */
#define APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY      (1u << 7)

#define APTA_GRID_FLAG_PROVISIONAL_PHASE     (1u << 0)
#define APTA_GRID_FLAG_DYNAMIC_TEMPO         (1u << 1)
#define APTA_GRID_FLAG_PHASE_AMBIGUITY       (1u << 2)
#define APTA_GRID_FLAG_HALF_TIME_AMBIGUITY   (1u << 3)
#define APTA_GRID_FLAG_DOUBLE_TIME_AMBIGUITY (1u << 4)
#define APTA_GRID_FLAG_USER_CONFIRMED        (1u << 5)
#define APTA_GRID_FLAG_USER_EDITED           (1u << 6)
#define APTA_GRID_FLAG_DEGRADED              (1u << 7)

typedef struct {
    int16_t minimum;
    int16_t maximum;
    uint16_t rms;

    uint8_t low;
    uint8_t mid;
    uint8_t high;
    uint8_t flags;
} apta_waveform_column_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t level_id;
    uint32_t frames_per_column;
    apta_source_frame_t origin_frame;

    uint32_t flags;
    uint32_t reserved32[3];
} apta_waveform_level_info_t;

typedef struct {
    apta_frame_range_t source_range;
    uint32_t first_column_index;
    uint32_t column_count;
    const apta_waveform_column_t *columns;
} apta_waveform_span_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_waveform_level_info_t level;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    uint8_t reserved8[3];

    uint32_t span_count;
    const apta_waveform_span_t *spans;

    uint32_t flags;
    uint32_t reserved32[3];
} apta_waveform_overview_view_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t level_id;
    uint32_t tile_index;

    apta_frame_range_t source_range;
    uint32_t first_column_index;
    uint32_t column_count;
    const apta_waveform_column_t *columns;

    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    uint8_t reserved8[3];

    uint32_t flags;
    uint32_t reserved32[3];
} apta_waveform_tile_view_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_frame_range_t evidence_range;
    apta_frame_range_t applicability_range;

    apta_tempo_millibpm_t tempo_millibpm;
    apta_confidence_value_t confidence;
    uint8_t reserved8;
    uint16_t reserved16;

    apta_feature_state_t state;
    uint32_t flags;
    uint32_t candidate_set_id;
    uint32_t reserved32[2];
} apta_tempo_value_t;

typedef struct {
    apta_tempo_millibpm_t tempo_millibpm;
    uint16_t score;
    apta_confidence_value_t confidence;
    uint8_t reserved8;
    apta_tempo_relation_t relation_to_selected;
    uint32_t flags;
} apta_tempo_candidate_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_tempo_value_t selected;
    uint32_t candidate_count;
    const apta_tempo_candidate_t *candidates;

    uint32_t flags;
    uint32_t reserved32[3];
} apta_tempo_view_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_frame_range_t applicability_range;

    apta_fractional_frame_t anchor_position;
    apta_beat_ordinal_t anchor_ordinal;
    apta_frame_period_t frames_per_beat;

    uint32_t beat_count;
    apta_tempo_millibpm_t nominal_tempo_millibpm;

    apta_confidence_value_t confidence;
    uint8_t reserved8;
    uint16_t reserved16;

    apta_feature_state_t state;
    uint32_t flags;
    uint32_t segment_id;
    uint32_t revision;
} apta_grid_segment_t;

typedef struct {
    apta_fractional_frame_t position;
    apta_beat_ordinal_t ordinal;

    apta_confidence_value_t confidence;
    uint8_t reserved8;
    uint16_t reserved16;

    uint32_t flags;
    uint32_t revision;
} apta_beat_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_frame_range_t requested_range;
    apta_frame_range_t evidence_range;
    apta_frame_range_t applicability_range;

    apta_grid_representation_t representation;
    apta_feature_state_t state;
    apta_confidence_value_t confidence;
    uint8_t reserved8[3];

    uint32_t coverage_range_count;
    const apta_frame_range_t *coverage_ranges;

    uint32_t segment_count;
    const apta_grid_segment_t *segments;

    uint32_t beat_count;
    const apta_beat_t *beats;

    uint32_t flags;
    uint32_t reserved32[3];
} apta_grid_view_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_source_frame_t total_frames;
    uint32_t sample_rate;
    uint16_t channel_count;
    uint16_t reserved16;
    apta_channel_layout_t channel_layout;
    apta_source_fingerprint_kind_t fingerprint_kind;
    uint8_t fingerprint[APTA_SOURCE_FINGERPRINT_SIZE];
    uint32_t flags;
    uint32_t reserved32[3];
} apta_source_info_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t specification_major;
    uint32_t specification_minor;
    uint32_t producer_api_version;
    uint32_t container_version;

    apta_generation_t generation;
    apta_feature_mask_t available_features;
    apta_feature_mask_t changed_features;
    apta_session_state_t session_state;

    uint32_t diagnostic_count;
    uint32_t flags;
    uint64_t lineage_id_high;
    uint64_t lineage_id_low;
} apta_result_info_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t code;
    apta_diagnostic_severity_t severity;
    apta_feature_mask_t affected_features;
    apta_frame_range_t affected_range;

    uint32_t request_id;
    uint32_t flags;
    const char *message;
} apta_diagnostic_view_t;

APTA_API const apta_result_t *APTA_CALL
apta_session_acquire_result(const apta_session_t *session);

APTA_API void APTA_CALL
apta_result_release(const apta_result_t *result);

APTA_API apta_status_t APTA_CALL
apta_result_get_info(
    const apta_result_t *result,
    apta_result_info_t *info_out);

APTA_API apta_status_t APTA_CALL
apta_result_get_source_info(
    const apta_result_t *result,
    apta_source_info_t *info_out);

APTA_API apta_generation_t APTA_CALL
apta_result_get_generation(const apta_result_t *result);

APTA_API apta_feature_mask_t APTA_CALL
apta_result_get_available_features(const apta_result_t *result);

APTA_API apta_status_t APTA_CALL
apta_result_get_feature_state(
    const apta_result_t *result,
    apta_feature_mask_t feature,
    const apta_frame_range_t *range,
    apta_feature_state_t *state_out,
    apta_confidence_value_t *confidence_out);

APTA_API apta_status_t APTA_CALL
apta_result_get_waveform_overview(
    const apta_result_t *result,
    uint32_t level_id,
    apta_waveform_overview_view_t *view_out);

APTA_API apta_status_t APTA_CALL
apta_result_get_waveform_tile(
    const apta_result_t *result,
    uint32_t level_id,
    uint32_t tile_index,
    apta_waveform_tile_view_t *view_out);

APTA_API apta_status_t APTA_CALL
apta_result_get_tempo(
    const apta_result_t *result,
    const apta_frame_range_t *range,
    apta_tempo_view_t *view_out);

APTA_API apta_status_t APTA_CALL
apta_result_get_beatgrid(
    const apta_result_t *result,
    apta_feature_mask_t grid_feature,
    const apta_frame_range_t *range,
    apta_grid_view_t *view_out);

APTA_API uint32_t APTA_CALL
apta_result_get_diagnostic_count(const apta_result_t *result);

APTA_API apta_status_t APTA_CALL
apta_result_get_diagnostic(
    const apta_result_t *result,
    uint32_t index,
    apta_diagnostic_view_t *view_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_RESULT_H */
