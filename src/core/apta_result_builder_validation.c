// SPDX-License-Identifier: Apache-2.0
#include "apta_result_builder_internal.h"

#include <limits.h>

#define APTA_BUILDER_COLUMN_FLAGS (                                      \
    APTA_WAVEFORM_COLUMN_VALID | APTA_WAVEFORM_COLUMN_PROVISIONAL |      \
    APTA_WAVEFORM_COLUMN_CLIPPED | APTA_WAVEFORM_COLUMN_HAS_3BAND |      \
    APTA_WAVEFORM_COLUMN_DEGRADED)
#define APTA_BUILDER_TEMPO_FLAGS (                                       \
    APTA_TEMPO_FLAG_HALF_TIME_AMBIGUITY |                                \
    APTA_TEMPO_FLAG_DOUBLE_TIME_AMBIGUITY |                              \
    APTA_TEMPO_FLAG_MULTIPLE_PHASES | APTA_TEMPO_FLAG_DYNAMIC |          \
    APTA_TEMPO_FLAG_USER_CONFIRMED | APTA_TEMPO_FLAG_USER_EDITED |       \
    APTA_TEMPO_FLAG_DEGRADED | APTA_TEMPO_FLAG_OCTAVE_AMBIGUITY)
#define APTA_BUILDER_GRID_FLAGS (                                        \
    APTA_GRID_FLAG_PROVISIONAL_PHASE | APTA_GRID_FLAG_DYNAMIC_TEMPO |    \
    APTA_GRID_FLAG_PHASE_AMBIGUITY |                                     \
    APTA_GRID_FLAG_HALF_TIME_AMBIGUITY |                                 \
    APTA_GRID_FLAG_DOUBLE_TIME_AMBIGUITY |                               \
    APTA_GRID_FLAG_USER_CONFIRMED | APTA_GRID_FLAG_USER_EDITED |         \
    APTA_GRID_FLAG_DEGRADED | APTA_GRID_FLAG_LOCKED)
#define APTA_BUILDER_QUALITY_FLAGS (                                     \
    APTA_QUALITY_FLAG_AMBIGUOUS | APTA_QUALITY_FLAG_DEGRADED |           \
    APTA_QUALITY_FLAG_OUT_OF_DOMAIN |                                    \
    APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT)
#define APTA_BUILDER_QUALITY_TARGETS (                                   \
    APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL |      \
    APTA_FEATURE_WAVEFORM_3BAND | APTA_FEATURE_BPM |                     \
    APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GLOBAL_BEATGRID |         \
    APTA_FEATURE_DYNAMIC_TEMPO | APTA_FEATURE_CONFIDENCE |               \
    APTA_FEATURE_GRID_LOCKING | APTA_FEATURE_MUSICAL_KEY |               \
    APTA_FEATURE_METER_DOWNBEAT)

int apta_builder_confidence_valid(apta_confidence_value_t value)
{
    return value <= APTA_CONFIDENCE_MAX ||
           value == APTA_CONFIDENCE_UNKNOWN;
}

int apta_builder_state_valid(apta_feature_state_t state)
{
    return state >= APTA_FEATURE_PARTIAL && state <= APTA_FEATURE_FINAL;
}

apta_status_t apta_builder_validate_range(
    const apta_frame_range_t *range,
    const apta_source_info_t *source)
{
    if (range == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            range, sizeof(*range), range->struct_size, range->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (range->first_frame >= range->end_frame) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (source != NULL && source->total_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
        range->end_frame > source->total_frames) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    return APTA_STATUS_OK;
}

static const apta_source_info_t *apta_builder_source(
    const apta_result_builder_t *builder)
{
    return builder->has_source != 0u ? &builder->source : NULL;
}

static int apta_builder_u32_add(uint32_t left, uint32_t right, uint32_t *out)
{
    if (right > UINT32_MAX - left) {
        return 0;
    }
    *out = left + right;
    return 1;
}

static int apta_builder_array_within_limit(
    const apta_result_builder_t *builder,
    uint32_t count,
    size_t element_size)
{
    return apta_internal_size_array_fits(0u, count, element_size) &&
           (uint64_t)count * (uint64_t)element_size <=
               builder->options.maximum_allocation_bytes;
}

static int apta_builder_ranges_ordered(
    const apta_frame_range_t *previous,
    const apta_frame_range_t *current)
{
    return previous == NULL || previous->end_frame <= current->first_frame;
}

static apta_status_t apta_builder_validate_column(
    const apta_waveform_column_t *column)
{
    if (column->minimum > column->maximum) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    return (column->flags & ~APTA_BUILDER_COLUMN_FLAGS) == 0u
               ? APTA_STATUS_OK
               : APTA_ERROR_UNSUPPORTED;
}

apta_status_t apta_builder_validate_overview(
    const apta_result_builder_t *builder,
    const apta_waveform_overview_view_t *view,
    uint32_t *column_count_out)
{
    const apta_frame_range_t *previous = NULL;
    uint32_t total = 0u;
    uint32_t index;
    if (view == NULL || column_count_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            view, sizeof(*view), view->struct_size, view->api_version) ||
        !apta_internal_validate_struct(
            &view->level, sizeof(view->level), view->level.struct_size,
            view->level.api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (view->level.level_id != 0u ||
        view->level.frames_per_column == 0u || view->level.flags != 0u ||
        view->flags != 0u || !apta_builder_state_valid(view->state) ||
        !apta_builder_confidence_valid(view->confidence) ||
        !apta_builder_bytes_zero(view->reserved8, sizeof(view->reserved8)) ||
        !apta_builder_bytes_zero(view->reserved32, sizeof(view->reserved32)) ||
        !apta_builder_bytes_zero(
            view->level.reserved32, sizeof(view->level.reserved32))) {
        return (view->flags != 0u || view->level.flags != 0u)
                   ? APTA_ERROR_UNSUPPORTED
                   : APTA_ERROR_INVALID_ARGUMENT;
    }
    if (view->span_count == 0u || view->spans == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (view->span_count > builder->options.maximum_overview_spans ||
        !apta_builder_array_within_limit(
            builder, view->span_count, sizeof(apta_waveform_span_t))) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    for (index = 0u; index < view->span_count; ++index) {
        const apta_waveform_span_t *span = &view->spans[index];
        uint32_t column;
        uint32_t end_column;
        apta_status_t status = apta_builder_validate_range(
            &span->source_range, apta_builder_source(builder));
        if (status < 0) {
            return status;
        }
        if (!apta_builder_ranges_ordered(previous, &span->source_range) ||
            span->column_count == 0u || span->columns == NULL ||
            !apta_builder_u32_add(
                span->first_column_index, span->column_count, &end_column) ||
            !apta_builder_u32_add(total, span->column_count, &total)) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        (void)end_column;
        if (total > builder->options.maximum_waveform_columns ||
            !apta_builder_array_within_limit(
                builder, total, sizeof(apta_waveform_column_t))) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        for (column = 0u; column < span->column_count; ++column) {
            status = apta_builder_validate_column(&span->columns[column]);
            if (status < 0) {
                return status;
            }
        }
        previous = &span->source_range;
    }
    *column_count_out = total;
    return APTA_STATUS_OK;
}

apta_status_t apta_builder_validate_detail(
    const apta_result_builder_t *builder,
    const apta_waveform_detail_input_t *input,
    uint32_t *column_count_out)
{
    const apta_frame_range_t *previous = NULL;
    uint32_t previous_tile = 0u;
    uint32_t total = 0u;
    uint32_t index;
    if (input == NULL || column_count_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            input, sizeof(*input), input->struct_size, input->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (input->flags != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (!apta_builder_bytes_zero(
            input->reserved32, sizeof(input->reserved32)) ||
        input->tile_count == 0u || input->tiles == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (input->tile_count > builder->options.maximum_detail_tiles ||
        !apta_builder_array_within_limit(
            builder, input->tile_count, sizeof(apta_waveform_tile_view_t))) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    for (index = 0u; index < input->tile_count; ++index) {
        const apta_waveform_tile_view_t *tile = &input->tiles[index];
        uint32_t end_column;
        uint32_t column;
        apta_status_t status;
        if (!apta_internal_validate_struct(
                tile, sizeof(*tile), tile->struct_size, tile->api_version)) {
            return APTA_ERROR_INCOMPATIBLE_VERSION;
        }
        status = apta_builder_validate_range(
            &tile->source_range, apta_builder_source(builder));
        if (status < 0) {
            return status;
        }
        if (tile->level_id != 1u ||
            (index != 0u && tile->tile_index <= previous_tile) ||
            !apta_builder_ranges_ordered(previous, &tile->source_range) ||
            tile->column_count == 0u || tile->columns == NULL ||
            !apta_builder_u32_add(
                tile->first_column_index, tile->column_count, &end_column) ||
            !apta_builder_u32_add(total, tile->column_count, &total) ||
            !apta_builder_state_valid(tile->state) ||
            !apta_builder_confidence_valid(tile->confidence) ||
            !apta_builder_bytes_zero(
                tile->reserved8, sizeof(tile->reserved8)) ||
            !apta_builder_bytes_zero(
                tile->reserved32, sizeof(tile->reserved32))) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        (void)end_column;
        if (tile->flags != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        if (total > builder->options.maximum_waveform_columns ||
            !apta_builder_array_within_limit(
                builder, total, sizeof(apta_waveform_column_t))) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        for (column = 0u; column < tile->column_count; ++column) {
            status = apta_builder_validate_column(&tile->columns[column]);
            if (status < 0) {
                return status;
            }
        }
        previous = &tile->source_range;
        previous_tile = tile->tile_index;
    }
    *column_count_out = total;
    return APTA_STATUS_OK;
}

apta_status_t apta_builder_validate_tempo(
    const apta_result_builder_t *builder,
    const apta_tempo_view_t *view)
{
    uint16_t previous_score = UINT16_MAX;
    uint32_t index;
    apta_status_t status;
    if (view == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            view, sizeof(*view), view->struct_size, view->api_version) ||
        !apta_internal_validate_struct(
            &view->selected, sizeof(view->selected),
            view->selected.struct_size, view->selected.api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    status = apta_builder_validate_range(
        &view->selected.evidence_range, apta_builder_source(builder));
    if (status < 0) return status;
    status = apta_builder_validate_range(
        &view->selected.applicability_range, apta_builder_source(builder));
    if (status < 0) return status;
    if (view->selected.tempo_millibpm < APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
        view->selected.tempo_millibpm > APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
        !apta_builder_state_valid(view->selected.state) ||
        !apta_builder_confidence_valid(view->selected.confidence) ||
        view->selected.reserved8 != 0u || view->selected.reserved16 != 0u ||
        view->flags != 0u ||
        !apta_builder_bytes_zero(
            view->selected.reserved32, sizeof(view->selected.reserved32)) ||
        !apta_builder_bytes_zero(view->reserved32, sizeof(view->reserved32)) ||
        view->candidate_count == 0u || view->candidates == NULL) {
        return view->flags != 0u ? APTA_ERROR_UNSUPPORTED
                                : APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((view->selected.flags & ~APTA_BUILDER_TEMPO_FLAGS) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (view->candidate_count > builder->options.maximum_tempo_candidates ||
        !apta_builder_array_within_limit(
            builder, view->candidate_count, sizeof(apta_tempo_candidate_t))) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    for (index = 0u; index < view->candidate_count; ++index) {
        const apta_tempo_candidate_t *candidate = &view->candidates[index];
        if (candidate->tempo_millibpm < APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
            candidate->tempo_millibpm > APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
            candidate->confidence > APTA_CONFIDENCE_MAX ||
            candidate->reserved8 != 0u ||
            candidate->relation_to_selected >
                APTA_TEMPO_RELATION_QUADRUPLE ||
            candidate->score > previous_score) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        if ((candidate->flags & ~APTA_BUILDER_TEMPO_FLAGS) != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        previous_score = candidate->score;
    }
    return APTA_STATUS_OK;
}

static int apta_builder_fraction_reserved_zero(
    const apta_fractional_frame_t *value)
{
    return value->reserved == 0u;
}

static int apta_builder_period_valid(const apta_frame_period_t *value)
{
    return value->reserved == 0u &&
           (value->whole_frames != 0u || value->fraction_q32 != 0u);
}

static int apta_builder_position_less(
    const apta_fractional_frame_t *left,
    const apta_fractional_frame_t *right)
{
    return left->whole_frame < right->whole_frame ||
           (left->whole_frame == right->whole_frame &&
            left->fraction_q32 < right->fraction_q32);
}

apta_status_t apta_builder_validate_grid(
    const apta_result_builder_t *builder,
    const apta_grid_view_t *view)
{
    const apta_frame_range_t *previous = NULL;
    uint32_t index;
    apta_status_t status;
    if (view == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    if (!apta_internal_validate_struct(
            view, sizeof(*view), view->struct_size, view->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    status = apta_builder_validate_range(
        &view->requested_range, apta_builder_source(builder));
    if (status < 0) return status;
    status = apta_builder_validate_range(
        &view->evidence_range, apta_builder_source(builder));
    if (status < 0) return status;
    status = apta_builder_validate_range(
        &view->applicability_range, apta_builder_source(builder));
    if (status < 0) return status;
    if (view->representation < APTA_GRID_REPRESENTATION_SEGMENTS ||
        view->representation > APTA_GRID_REPRESENTATION_HYBRID) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (!apta_builder_state_valid(view->state) ||
        !apta_builder_confidence_valid(view->confidence) ||
        !apta_builder_bytes_zero(view->reserved8, sizeof(view->reserved8)) ||
        !apta_builder_bytes_zero(view->reserved32, sizeof(view->reserved32))) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if ((view->flags & ~APTA_BUILDER_GRID_FLAGS) != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if ((view->coverage_range_count == 0u) !=
            (view->coverage_ranges == NULL) ||
        (view->segment_count == 0u) != (view->segments == NULL) ||
        (view->beat_count == 0u) != (view->beats == NULL)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (view->coverage_range_count == 0u ||
        view->coverage_range_count >
            builder->options.maximum_grid_coverage_ranges ||
        view->segment_count > builder->options.maximum_grid_segments ||
        view->beat_count > builder->options.maximum_grid_beats ||
        !apta_builder_array_within_limit(
            builder, view->coverage_range_count, sizeof(apta_frame_range_t)) ||
        !apta_builder_array_within_limit(
            builder, view->segment_count, sizeof(apta_grid_segment_t)) ||
        !apta_builder_array_within_limit(
            builder, view->beat_count, sizeof(apta_beat_t))) {
        return view->coverage_range_count == 0u
                   ? APTA_ERROR_INVALID_ARGUMENT
                   : APTA_ERROR_LIMIT_EXCEEDED;
    }
    if ((view->representation == APTA_GRID_REPRESENTATION_SEGMENTS &&
         (view->segment_count == 0u || view->beat_count != 0u)) ||
        (view->representation == APTA_GRID_REPRESENTATION_EXPLICIT &&
         (view->segment_count != 0u || view->beat_count == 0u)) ||
        (view->representation == APTA_GRID_REPRESENTATION_HYBRID &&
         (view->segment_count == 0u || view->beat_count == 0u))) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    for (index = 0u; index < view->coverage_range_count; ++index) {
        const apta_frame_range_t *range = &view->coverage_ranges[index];
        status = apta_builder_validate_range(range, apta_builder_source(builder));
        if (status < 0) return status;
        if (!apta_builder_ranges_ordered(previous, range) ||
            range->first_frame < view->applicability_range.first_frame ||
            range->end_frame > view->applicability_range.end_frame) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        previous = range;
    }
    previous = NULL;
    for (index = 0u; index < view->segment_count; ++index) {
        const apta_grid_segment_t *segment = &view->segments[index];
        if (!apta_internal_validate_struct(
                segment, sizeof(*segment), segment->struct_size,
                segment->api_version)) {
            return APTA_ERROR_INCOMPATIBLE_VERSION;
        }
        status = apta_builder_validate_range(
            &segment->applicability_range, apta_builder_source(builder));
        if (status < 0) return status;
        if (!apta_builder_ranges_ordered(previous, &segment->applicability_range) ||
            segment->applicability_range.first_frame <
                view->applicability_range.first_frame ||
            segment->applicability_range.end_frame >
                view->applicability_range.end_frame ||
            !apta_builder_fraction_reserved_zero(&segment->anchor_position) ||
            !apta_builder_period_valid(&segment->frames_per_beat) ||
            segment->nominal_tempo_millibpm <
                APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
            segment->nominal_tempo_millibpm >
                APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
            !apta_builder_state_valid(segment->state) ||
            !apta_builder_confidence_valid(segment->confidence) ||
            segment->reserved8 != 0u || segment->reserved16 != 0u) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        if ((segment->flags & ~APTA_BUILDER_GRID_FLAGS) != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        previous = &segment->applicability_range;
    }
    for (index = 0u; index < view->beat_count; ++index) {
        const apta_beat_t *beat = &view->beats[index];
        if (!apta_builder_fraction_reserved_zero(&beat->position) ||
            beat->position.whole_frame < view->applicability_range.first_frame ||
            beat->position.whole_frame >= view->applicability_range.end_frame ||
            !apta_builder_confidence_valid(beat->confidence) ||
            beat->reserved8 != 0u || beat->reserved16 != 0u ||
            (index != 0u &&
             (!apta_builder_position_less(
                  &view->beats[index - 1u].position, &beat->position) ||
              beat->ordinal <= view->beats[index - 1u].ordinal))) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
        if ((beat->flags & ~APTA_BUILDER_GRID_FLAGS) != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
    }
    return APTA_STATUS_OK;
}

static int apta_builder_key_value_valid(
    uint8_t tonic,
    apta_key_mode_t mode,
    int16_t tuning)
{
    return tonic <= 11u &&
           (mode == APTA_KEY_MODE_MAJOR || mode == APTA_KEY_MODE_MINOR) &&
           tuning >= -100 && tuning <= 100;
}

apta_status_t apta_builder_validate_key(
    const apta_result_builder_t *builder,
    const apta_key_view_t *view)
{
    uint16_t previous_score = UINT16_MAX;
    uint32_t index;
    apta_status_t status;
    if (view == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    if (!apta_internal_validate_struct(
            view, sizeof(*view), view->struct_size, view->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    status = apta_builder_validate_range(
        &view->applicability_range, apta_builder_source(builder));
    if (status < 0) return status;
    if (!apta_builder_key_value_valid(
            view->tonic, view->mode, view->tuning_offset_cents) ||
        !apta_builder_state_valid(view->state) ||
        !apta_builder_confidence_valid(view->confidence) || view->flags != 0u ||
        !apta_builder_bytes_zero(view->reserved32, sizeof(view->reserved32)) ||
        view->candidate_count == 0u || view->candidates == NULL) {
        return view->flags != 0u ? APTA_ERROR_UNSUPPORTED
                                : APTA_ERROR_INVALID_ARGUMENT;
    }
    if (view->candidate_count > builder->options.maximum_key_candidates ||
        !apta_builder_array_within_limit(
            builder, view->candidate_count, sizeof(apta_key_candidate_t))) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    for (index = 0u; index < view->candidate_count; ++index) {
        const apta_key_candidate_t *candidate = &view->candidates[index];
        if (!apta_builder_key_value_valid(
                candidate->tonic, candidate->mode,
                candidate->tuning_offset_cents) ||
            !apta_builder_confidence_valid(candidate->confidence) ||
            candidate->reserved8 != 0u || candidate->reserved8_2 != 0u ||
            candidate->flags != 0u || candidate->score > previous_score) {
            return candidate->flags != 0u ? APTA_ERROR_UNSUPPORTED
                                          : APTA_ERROR_INVALID_ARGUMENT;
        }
        previous_score = candidate->score;
    }
    return APTA_STATUS_OK;
}

static int apta_builder_meter_value_valid(uint16_t numerator, uint16_t denominator)
{
    return numerator >= 1u && numerator <= 32u && denominator >= 1u &&
           denominator <= 32u && (denominator & (denominator - 1u)) == 0u;
}

apta_status_t apta_builder_validate_meter(
    const apta_result_builder_t *builder,
    const apta_meter_view_t *view)
{
    const apta_frame_range_t *previous = NULL;
    uint32_t index;
    if (view == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    if (!apta_internal_validate_struct(
            view, sizeof(*view), view->struct_size, view->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (!apta_builder_meter_value_valid(view->numerator, view->denominator) ||
        !apta_builder_state_valid(view->state) ||
        !apta_builder_confidence_valid(view->confidence) ||
        view->reserved8 != 0u || view->reserved16 != 0u || view->flags != 0u ||
        !apta_builder_bytes_zero(view->reserved32, sizeof(view->reserved32)) ||
        view->segment_count == 0u || view->segments == NULL) {
        return view->flags != 0u ? APTA_ERROR_UNSUPPORTED
                                : APTA_ERROR_INVALID_ARGUMENT;
    }
    if (view->segment_count > builder->options.maximum_meter_segments ||
        !apta_builder_array_within_limit(
            builder, view->segment_count, sizeof(apta_meter_segment_t))) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    for (index = 0u; index < view->segment_count; ++index) {
        const apta_meter_segment_t *segment = &view->segments[index];
        apta_status_t status;
        if (!apta_internal_validate_struct(
                segment, sizeof(*segment), segment->struct_size,
                segment->api_version)) {
            return APTA_ERROR_INCOMPATIBLE_VERSION;
        }
        status = apta_builder_validate_range(
            &segment->applicability_range, apta_builder_source(builder));
        if (status < 0) return status;
        if (!apta_builder_ranges_ordered(previous, &segment->applicability_range) ||
            !apta_builder_meter_value_valid(
                segment->numerator, segment->denominator) ||
            segment->downbeat_frame < segment->applicability_range.first_frame ||
            segment->downbeat_frame >= segment->applicability_range.end_frame ||
            !apta_builder_state_valid(segment->state) ||
            !apta_builder_confidence_valid(segment->confidence) ||
            segment->reserved8 != 0u || segment->reserved16 != 0u ||
            segment->flags != 0u ||
            !apta_builder_bytes_zero(
                segment->reserved32, sizeof(segment->reserved32))) {
            return segment->flags != 0u ? APTA_ERROR_UNSUPPORTED
                                       : APTA_ERROR_INVALID_ARGUMENT;
        }
        previous = &segment->applicability_range;
    }
    if (view->downbeat_frame != view->segments[0].downbeat_frame ||
        view->downbeat_ordinal != view->segments[0].downbeat_ordinal ||
        view->numerator != view->segments[0].numerator ||
        view->denominator != view->segments[0].denominator) {
        return APTA_ERROR_CONFLICT;
    }
    return APTA_STATUS_OK;
}

apta_status_t apta_builder_validate_quality(const apta_quality_view_t *view)
{
    if (view == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    if (!apta_internal_validate_struct(
            view, sizeof(*view), view->struct_size, view->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if (view->feature == 0u ||
        (view->feature & (view->feature - 1u)) != 0u ||
        (view->feature & APTA_BUILDER_QUALITY_TARGETS) == 0u ||
        view->feature == APTA_FEATURE_CALIBRATED_QUALITY ||
        !apta_builder_state_valid(view->state) ||
        !apta_builder_confidence_valid(view->confidence) ||
        (view->evidence_coverage_permille > APTA_EVIDENCE_COVERAGE_MAX &&
         view->evidence_coverage_permille != APTA_EVIDENCE_COVERAGE_UNKNOWN) ||
        view->reserved8 != 0u ||
        !apta_builder_bytes_zero(view->reserved32, sizeof(view->reserved32))) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    return (view->flags & ~APTA_BUILDER_QUALITY_FLAGS) == 0u
               ? APTA_STATUS_OK
               : APTA_ERROR_UNSUPPORTED;
}
