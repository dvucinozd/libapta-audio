// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_grid_match_internal.h"
#include "../beatgrid/apta_s6_internal.h"

#include <limits.h>
#include <string.h>

#if defined(APTA_ENABLE_TEST_HOOKS)
uint64_t apta_test_writer_grid_match_work;
#define APTA_TEST_WRITER_GRID_WORK_PTR (&apta_test_writer_grid_match_work)
#else
#define APTA_TEST_WRITER_GRID_WORK_PTR NULL
#endif

#define APTA_DJ_DIRECTORY_OFFSET 96u
#define APTA_DJ_DIRECTORY_ENTRY_SIZE 40u
#define APTA_MKEY_HEADER_SIZE 40u
#define APTA_MKEY_CANDIDATE_SIZE 16u
#define APTA_MTRD_HEADER_SIZE 48u
#define APTA_MTRD_SEGMENT_SIZE 56u
#define APTA_CONF_HEADER_SIZE 16u
#define APTA_CONF_RECORD_SIZE 32u
#define APTA_DJ_MAX_KEY_CANDIDATES 24u
#define APTA_DJ_MAX_METER_SEGMENTS 65536u
#define APTA_DJ_MAX_QUALITY_RECORDS 11u
#define APTA_CONTAINER_FLAG_PARTIAL_RESULT (1u << 0)

#define APTA_DJ_QUALITY_FLAGS (                                           \
    APTA_QUALITY_FLAG_AMBIGUOUS | APTA_QUALITY_FLAG_DEGRADED |            \
    APTA_QUALITY_FLAG_OUT_OF_DOMAIN |                                     \
    APTA_QUALITY_FLAG_DETECTOR_DISAGREEMENT)
#define APTA_DJ_QUALITY_TARGETS (                                         \
    APTA_FEATURE_WAVEFORM_OVERVIEW | APTA_FEATURE_WAVEFORM_DETAIL |       \
    APTA_FEATURE_WAVEFORM_3BAND | APTA_FEATURE_BPM |                      \
    APTA_FEATURE_LOCAL_BEATGRID | APTA_FEATURE_GLOBAL_BEATGRID |          \
    APTA_FEATURE_DYNAMIC_TEMPO | APTA_FEATURE_CONFIDENCE |                \
    APTA_FEATURE_GRID_LOCKING | APTA_FEATURE_MUSICAL_KEY |                \
    APTA_FEATURE_METER_DOWNBEAT)

apta_status_t APTA_CALL apta_result_query_serialized_size_s6(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out);

apta_status_t APTA_CALL apta_result_serialize_s6(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written_out);

typedef struct {
    uint64_t base_size;
    uint64_t key_size;
    uint64_t meter_size;
    uint64_t quality_size;
    uint64_t key_offset;
    uint64_t meter_offset;
    uint64_t quality_offset;
    uint64_t total_size;
    uint32_t section_count;
    uint32_t has_key;
    uint32_t has_meter;
    uint32_t has_quality;
} apta_dj_write_layout_t;

static uint32_t apta_dj_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t apta_dj_get_u64(const uint8_t *p)
{
    return (uint64_t)apta_dj_get_u32(p) |
           ((uint64_t)apta_dj_get_u32(p + 4u) << 32u);
}

static void apta_dj_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void apta_dj_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static void apta_dj_put_u64(uint8_t *p, uint64_t value)
{
    apta_dj_put_u32(p, (uint32_t)value);
    apta_dj_put_u32(p + 4u, (uint32_t)(value >> 32u));
}

static apta_status_t apta_dj_align8(uint64_t value, uint64_t *out)
{
    if (out == NULL || value > UINT64_MAX - 7u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    *out = (value + 7u) & ~UINT64_C(7);
    return APTA_STATUS_OK;
}

static int apta_dj_state_valid(apta_feature_state_t state)
{
    return state >= APTA_FEATURE_PARTIAL && state <= APTA_FEATURE_FINAL;
}

static int apta_dj_confidence_valid(apta_confidence_value_t confidence)
{
    return confidence <= APTA_CONFIDENCE_MAX ||
           confidence == APTA_CONFIDENCE_UNKNOWN;
}

static int apta_dj_range_valid(
    const apta_result_t *result,
    const apta_frame_range_t *range)
{
    return range != NULL && range->first_frame < range->end_frame &&
           (result->total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN ||
            range->end_frame <= result->total_source_frames);
}

static int apta_dj_key_value_valid(
    uint8_t tonic,
    apta_key_mode_t mode,
    int16_t tuning)
{
    return tonic <= 11u &&
           (mode == APTA_KEY_MODE_MAJOR || mode == APTA_KEY_MODE_MINOR) &&
           tuning >= -100 && tuning <= 100;
}

static int apta_dj_key_valid(const apta_result_t *result)
{
    const apta_key_view_t *key = &result->key;
    uint16_t previous_score = UINT16_MAX;
    uint32_t index;
    int selected_found = 0;

    if (!apta_dj_range_valid(result, &key->applicability_range) ||
        !apta_dj_key_value_valid(key->tonic, key->mode,
                                 key->tuning_offset_cents) ||
        !apta_dj_state_valid(key->state) ||
        !apta_dj_confidence_valid(key->confidence) || key->flags != 0u ||
        key->reserved32[0] != 0u || key->reserved32[1] != 0u ||
        key->reserved32[2] != 0u ||
        key->candidate_count > APTA_DJ_MAX_KEY_CANDIDATES ||
        ((key->candidate_count == 0u) != (key->candidates == NULL))) {
        return 0;
    }
    for (index = 0u; index < key->candidate_count; ++index) {
        const apta_key_candidate_t *candidate = &key->candidates[index];
        uint32_t prior;
        if (!apta_dj_key_value_valid(
                candidate->tonic, candidate->mode,
                candidate->tuning_offset_cents) ||
            !apta_dj_confidence_valid(candidate->confidence) ||
            candidate->reserved8 != 0u || candidate->reserved8_2 != 0u ||
            candidate->flags != 0u ||
            (index != 0u && candidate->score >= previous_score)) {
            return 0;
        }
        for (prior = 0u; prior < index; ++prior) {
            if (key->candidates[prior].tonic == candidate->tonic &&
                key->candidates[prior].mode == candidate->mode &&
                key->candidates[prior].tuning_offset_cents ==
                    candidate->tuning_offset_cents) {
                return 0;
            }
        }
        if (candidate->tonic == key->tonic &&
            candidate->mode == key->mode &&
            candidate->tuning_offset_cents == key->tuning_offset_cents) {
            selected_found = 1;
        }
        previous_score = candidate->score;
    }
    return key->candidate_count == 0u || selected_found;
}

static int apta_dj_meter_value_valid(uint16_t numerator, uint16_t denominator)
{
    return numerator >= 1u && numerator <= 32u && denominator >= 1u &&
           denominator <= 32u && (denominator & (denominator - 1u)) == 0u;
}

static int apta_dj_meter_valid(const apta_result_t *result)
{
    const apta_meter_view_t *meter = &result->meter;
    apta_internal_grid_match_set_t grids;
    uint32_t index;

    if (!apta_dj_meter_value_valid(meter->numerator, meter->denominator) ||
        !apta_dj_state_valid(meter->state) ||
        !apta_dj_confidence_valid(meter->confidence) || meter->flags != 0u ||
        meter->reserved8 != 0u || meter->reserved16 != 0u ||
        meter->reserved32[0] != 0u || meter->reserved32[1] != 0u ||
        meter->reserved32[2] != 0u ||
        meter->segment_count == 0u || meter->segments == NULL ||
        meter->segment_count > APTA_DJ_MAX_METER_SEGMENTS ||
        ((result->info.available_features &
          APTA_FEATURE_GLOBAL_BEATGRID) != 0u && result->s6 == NULL)) {
        return 0;
    }
    apta_internal_grid_match_set_init(
        &grids,
        (result->info.available_features & APTA_FEATURE_LOCAL_BEATGRID) != 0u
            ? &result->local_grid : NULL,
        (result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) != 0u &&
                result->s6 != NULL
            ? &result->s6->global_grid : NULL,
        APTA_TEST_WRITER_GRID_WORK_PTR);
    for (index = 0u; index < meter->segment_count; ++index) {
        const apta_meter_segment_t *segment = &meter->segments[index];
        if (!apta_dj_range_valid(result, &segment->applicability_range) ||
            !apta_dj_meter_value_valid(
                segment->numerator, segment->denominator) ||
            segment->downbeat_frame <
                segment->applicability_range.first_frame ||
            segment->downbeat_frame >= segment->applicability_range.end_frame ||
            !apta_dj_state_valid(segment->state) ||
            segment->state < meter->state ||
            !apta_dj_confidence_valid(segment->confidence) ||
            segment->reserved8 != 0u || segment->reserved16 != 0u ||
            segment->flags != 0u || segment->segment_id == 0u ||
            segment->reserved32[0] != 0u ||
            segment->reserved32[1] != 0u ||
            !apta_internal_grid_match_set_next(
                &grids, segment->downbeat_frame,
                segment->downbeat_ordinal) ||
            (index != 0u &&
             (meter->segments[index - 1u].applicability_range.end_frame >
                  segment->applicability_range.first_frame ||
              meter->segments[index - 1u].downbeat_ordinal >=
                  segment->downbeat_ordinal ||
              meter->segments[index - 1u].segment_id >=
                  segment->segment_id))) {
            return 0;
        }
    }
    return meter->downbeat_frame == meter->segments[0].downbeat_frame &&
           meter->downbeat_ordinal == meter->segments[0].downbeat_ordinal &&
           meter->numerator == meter->segments[0].numerator &&
           meter->denominator == meter->segments[0].denominator;
}

static int apta_dj_quality_valid(const apta_result_t *result)
{
    uint32_t index;

    if (result->quality_count == 0u || result->quality == NULL ||
        result->quality_count > APTA_DJ_MAX_QUALITY_RECORDS) {
        return 0;
    }
    for (index = 0u; index < result->quality_count; ++index) {
        const apta_quality_view_t *quality = &result->quality[index];
        uint32_t prior;
        if (quality->feature == 0u ||
            (quality->feature & (quality->feature - 1u)) != 0u ||
            (quality->feature & APTA_DJ_QUALITY_TARGETS) == 0u ||
            (result->info.available_features & quality->feature) == 0u ||
            !apta_dj_state_valid(quality->state) ||
            !apta_dj_confidence_valid(quality->confidence) ||
            (quality->evidence_coverage_permille >
                 APTA_EVIDENCE_COVERAGE_MAX &&
             quality->evidence_coverage_permille !=
                 APTA_EVIDENCE_COVERAGE_UNKNOWN) ||
            quality->reserved8 != 0u ||
            (quality->flags & ~APTA_DJ_QUALITY_FLAGS) != 0u ||
            quality->reserved32[0] != 0u ||
            quality->reserved32[1] != 0u ||
            quality->reserved32[2] != 0u) {
            return 0;
        }
        for (prior = 0u; prior < index; ++prior) {
            if (result->quality[prior].feature == quality->feature) return 0;
        }
    }
    return 1;
}

static apta_status_t apta_dj_calculate_layout(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    apta_dj_write_layout_t *layout)
{
    uint64_t cursor;
    uint64_t directory_growth;
    apta_status_t status;

    if (result == NULL || layout == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    memset(layout, 0, sizeof(*layout));
    status = apta_result_query_serialized_size_s6(
        result, options, &layout->base_size);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) return status;

    layout->has_key =
        (result->info.available_features & APTA_FEATURE_MUSICAL_KEY) != 0u;
    layout->has_meter =
        (result->info.available_features & APTA_FEATURE_METER_DOWNBEAT) != 0u;
    layout->has_quality =
        (result->info.available_features &
         APTA_FEATURE_CALIBRATED_QUALITY) != 0u;
    if ((layout->has_key && !apta_dj_key_valid(result)) ||
        (layout->has_meter && !apta_dj_meter_valid(result)) ||
        (layout->has_quality && !apta_dj_quality_valid(result)) ||
        (!layout->has_key &&
         (result->key.state != APTA_FEATURE_ABSENT ||
          result->key.candidate_count != 0u)) ||
        (!layout->has_meter && result->meter.segment_count != 0u) ||
        (!layout->has_quality && result->quality_count != 0u)) {
        return APTA_ERROR_INVALID_STATE;
    }
    layout->section_count = layout->has_key + layout->has_meter +
                            layout->has_quality;
    if (layout->section_count == 0u) {
        layout->total_size = layout->base_size;
        return APTA_STATUS_OK;
    }

    if (layout->has_key) {
        layout->key_size = APTA_MKEY_HEADER_SIZE +
            (uint64_t)result->key.candidate_count * APTA_MKEY_CANDIDATE_SIZE;
    }
    if (layout->has_meter) {
        layout->meter_size = APTA_MTRD_HEADER_SIZE +
            (uint64_t)result->meter.segment_count * APTA_MTRD_SEGMENT_SIZE;
    }
    if (layout->has_quality) {
        layout->quality_size = APTA_CONF_HEADER_SIZE +
            (uint64_t)result->quality_count * APTA_CONF_RECORD_SIZE;
    }
    directory_growth =
        (uint64_t)layout->section_count * APTA_DJ_DIRECTORY_ENTRY_SIZE;
    if (layout->base_size > UINT64_MAX - directory_growth) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    status = apta_dj_align8(layout->base_size + directory_growth, &cursor);
    if (status < 0) return status;
    if (layout->has_key) {
        layout->key_offset = cursor;
        if (cursor > UINT64_MAX - layout->key_size) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        cursor += layout->key_size;
        status = apta_dj_align8(cursor, &cursor);
        if (status < 0) return status;
    }
    if (layout->has_meter) {
        layout->meter_offset = cursor;
        if (cursor > UINT64_MAX - layout->meter_size) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        cursor += layout->meter_size;
        status = apta_dj_align8(cursor, &cursor);
        if (status < 0) return status;
    }
    if (layout->has_quality) {
        layout->quality_offset = cursor;
        if (cursor > UINT64_MAX - layout->quality_size) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        cursor += layout->quality_size;
    }
    layout->total_size = cursor;
    if (options != NULL && options->maximum_output_bytes != 0u &&
        layout->total_size > options->maximum_output_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    return APTA_STATUS_OK;
}

static void apta_dj_write_key(uint8_t *payload, const apta_key_view_t *key)
{
    uint32_t index;
    memset(payload, 0, (size_t)(APTA_MKEY_HEADER_SIZE +
           (uint64_t)key->candidate_count * APTA_MKEY_CANDIDATE_SIZE));
    apta_dj_put_u16(payload, 1u);
    payload[2] = (uint8_t)key->state;
    payload[3] = key->confidence;
    payload[4] = key->tonic;
    payload[5] = (uint8_t)key->mode;
    apta_dj_put_u16(payload + 6u, (uint16_t)key->tuning_offset_cents);
    apta_dj_put_u32(payload + 8u, key->flags);
    apta_dj_put_u32(payload + 12u, key->candidate_count);
    apta_dj_put_u64(payload + 16u, key->applicability_range.first_frame);
    apta_dj_put_u64(payload + 24u, key->applicability_range.end_frame);
    apta_dj_put_u32(payload + 32u, APTA_MKEY_HEADER_SIZE);
    for (index = 0u; index < key->candidate_count; ++index) {
        const apta_key_candidate_t *candidate = &key->candidates[index];
        uint8_t *record = payload + APTA_MKEY_HEADER_SIZE +
                          (size_t)index * APTA_MKEY_CANDIDATE_SIZE;
        record[0] = candidate->tonic;
        record[1] = (uint8_t)candidate->mode;
        apta_dj_put_u16(record + 2u,
                        (uint16_t)candidate->tuning_offset_cents);
        apta_dj_put_u16(record + 4u, candidate->score);
        record[6] = candidate->confidence;
        apta_dj_put_u32(record + 8u, candidate->flags);
    }
}

static void apta_dj_write_meter(
    uint8_t *payload,
    const apta_meter_view_t *meter)
{
    uint32_t index;
    memset(payload, 0, (size_t)(APTA_MTRD_HEADER_SIZE +
           (uint64_t)meter->segment_count * APTA_MTRD_SEGMENT_SIZE));
    apta_dj_put_u16(payload, 1u);
    payload[2] = (uint8_t)meter->state;
    payload[3] = meter->confidence;
    apta_dj_put_u16(payload + 4u, meter->numerator);
    apta_dj_put_u16(payload + 6u, meter->denominator);
    apta_dj_put_u32(payload + 8u, meter->flags);
    apta_dj_put_u32(payload + 12u, meter->segment_count);
    apta_dj_put_u64(payload + 16u, meter->downbeat_frame);
    apta_dj_put_u64(payload + 24u, (uint64_t)meter->downbeat_ordinal);
    apta_dj_put_u32(payload + 32u, APTA_MTRD_HEADER_SIZE);
    for (index = 0u; index < meter->segment_count; ++index) {
        const apta_meter_segment_t *segment = &meter->segments[index];
        uint8_t *record = payload + APTA_MTRD_HEADER_SIZE +
                          (size_t)index * APTA_MTRD_SEGMENT_SIZE;
        apta_dj_put_u64(record, segment->applicability_range.first_frame);
        apta_dj_put_u64(record + 8u, segment->applicability_range.end_frame);
        apta_dj_put_u64(record + 16u, segment->downbeat_frame);
        apta_dj_put_u64(record + 24u, (uint64_t)segment->downbeat_ordinal);
        apta_dj_put_u16(record + 32u, segment->numerator);
        apta_dj_put_u16(record + 34u, segment->denominator);
        record[36] = (uint8_t)segment->state;
        record[37] = segment->confidence;
        apta_dj_put_u32(record + 40u, segment->flags);
        apta_dj_put_u32(record + 44u, segment->segment_id);
    }
}

static const apta_quality_view_t *apta_dj_quality_by_feature(
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

static void apta_dj_write_quality(
    uint8_t *payload,
    const apta_result_t *result)
{
    apta_feature_mask_t feature;
    uint32_t record_index = 0u;
    memset(payload, 0, (size_t)(APTA_CONF_HEADER_SIZE +
           (uint64_t)result->quality_count * APTA_CONF_RECORD_SIZE));
    apta_dj_put_u16(payload, 1u);
    apta_dj_put_u16(payload + 2u, APTA_CONF_RECORD_SIZE);
    apta_dj_put_u32(payload + 4u, result->quality_count);
    apta_dj_put_u32(payload + 8u, APTA_CONF_HEADER_SIZE);
    for (feature = 1u; feature <= APTA_FEATURE_METER_DOWNBEAT;
         feature <<= 1u) {
        const apta_quality_view_t *quality =
            apta_dj_quality_by_feature(result, feature);
        uint8_t *record;
        if (quality == NULL) continue;
        record = payload + APTA_CONF_HEADER_SIZE +
                 (size_t)record_index * APTA_CONF_RECORD_SIZE;
        apta_dj_put_u64(record, quality->feature);
        apta_dj_put_u32(record + 8u, quality->calibration_model_id);
        apta_dj_put_u16(record + 12u,
                        quality->evidence_coverage_permille);
        record[14] = quality->confidence;
        record[15] = (uint8_t)quality->state;
        apta_dj_put_u32(record + 16u, quality->flags);
        ++record_index;
    }
}

static void apta_dj_write_directory(
    uint8_t *entry,
    const char id[4],
    uint64_t offset,
    uint64_t size,
    const uint8_t *payload)
{
    memset(entry, 0, APTA_DJ_DIRECTORY_ENTRY_SIZE);
    memcpy(entry, id, 4u);
    apta_dj_put_u16(entry + 4u, 1u);
    apta_dj_put_u64(entry + 8u, offset);
    apta_dj_put_u64(entry + 16u, size);
    apta_dj_put_u64(entry + 24u, size);
    apta_dj_put_u32(entry + 32u,
                    apta_internal_crc32c(payload, (size_t)size));
}

static int apta_dj_requires_partial(
    const apta_result_t *result,
    const apta_dj_write_layout_t *layout)
{
    uint32_t index;
    int partial = layout->has_key &&
                  result->key.state != APTA_FEATURE_FINAL;
    if (layout->has_meter && result->meter.state != APTA_FEATURE_FINAL) {
        partial = 1;
    }
    if (layout->has_meter) {
        for (index = 0u; index < result->meter.segment_count; ++index) {
            if (result->meter.segments[index].state != APTA_FEATURE_FINAL) {
                partial = 1;
            }
        }
    }
    if (layout->has_quality) {
        for (index = 0u; index < result->quality_count; ++index) {
            if (result->quality[index].state != APTA_FEATURE_FINAL) {
                partial = 1;
            }
        }
    }
    return partial;
}

apta_status_t APTA_CALL apta_result_query_serialized_size(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out)
{
    apta_dj_write_layout_t layout;
    apta_status_t status;
    if (size_out == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    *size_out = 0u;
    status = apta_dj_calculate_layout(result, options, &layout);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) return status;
    *size_out = layout.total_size;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_serialize(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written_out)
{
    apta_dj_write_layout_t layout;
    uint8_t *bytes;
    uint8_t *directory;
    uint64_t first_payload = UINT64_MAX;
    uint64_t directory_growth;
    uint32_t old_count;
    uint32_t new_index;
    uint32_t index;
    size_t base_written;
    apta_status_t status;

    if (bytes_written_out == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    *bytes_written_out = 0u;
    status = apta_dj_calculate_layout(result, options, &layout);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) return status;
    if (layout.total_size > SIZE_MAX) return APTA_ERROR_LIMIT_EXCEEDED;
    if (buffer == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    if (buffer_size < (size_t)layout.total_size) {
        return APTA_ERROR_BUFFER_TOO_SMALL;
    }
    status = apta_result_serialize_s6(
        result, options, buffer, buffer_size, &base_written);
    if (status < 0) return status;
    if (layout.section_count == 0u) {
        *bytes_written_out = base_written;
        return APTA_STATUS_OK;
    }
    if (base_written != (size_t)layout.base_size) return APTA_ERROR_INTERNAL;

    bytes = (uint8_t *)buffer;
    old_count = apta_dj_get_u32(bytes + 20u);
    if (old_count == 0u ||
        apta_dj_get_u64(bytes + 24u) != APTA_DJ_DIRECTORY_OFFSET ||
        old_count > UINT32_MAX - layout.section_count) {
        return APTA_ERROR_INTERNAL;
    }
    directory = bytes + APTA_DJ_DIRECTORY_OFFSET;
    for (index = 0u; index < old_count; ++index) {
        const uint64_t offset = apta_dj_get_u64(
            directory + (size_t)index * APTA_DJ_DIRECTORY_ENTRY_SIZE + 8u);
        if (offset < first_payload) first_payload = offset;
    }
    if (first_payload == UINT64_MAX || first_payload > layout.base_size) {
        return APTA_ERROR_INTERNAL;
    }
    directory_growth =
        (uint64_t)layout.section_count * APTA_DJ_DIRECTORY_ENTRY_SIZE;
    memmove(bytes + (size_t)(first_payload + directory_growth),
            bytes + (size_t)first_payload,
            (size_t)(layout.base_size - first_payload));
    for (index = 0u; index < old_count; ++index) {
        uint8_t *entry = directory +
                         (size_t)index * APTA_DJ_DIRECTORY_ENTRY_SIZE;
        apta_dj_put_u64(entry + 8u,
                        apta_dj_get_u64(entry + 8u) + directory_growth);
    }
    memset(bytes + (size_t)(layout.base_size + directory_growth), 0,
           (size_t)(layout.total_size -
                    (layout.base_size + directory_growth)));

    new_index = old_count;
    if (layout.has_key) {
        apta_dj_write_key(bytes + (size_t)layout.key_offset, &result->key);
        apta_dj_write_directory(
            directory + (size_t)new_index++ * APTA_DJ_DIRECTORY_ENTRY_SIZE,
            "MKEY", layout.key_offset, layout.key_size,
            bytes + (size_t)layout.key_offset);
    }
    if (layout.has_meter) {
        apta_dj_write_meter(
            bytes + (size_t)layout.meter_offset, &result->meter);
        apta_dj_write_directory(
            directory + (size_t)new_index++ * APTA_DJ_DIRECTORY_ENTRY_SIZE,
            "MTRD", layout.meter_offset, layout.meter_size,
            bytes + (size_t)layout.meter_offset);
    }
    if (layout.has_quality) {
        apta_dj_write_quality(
            bytes + (size_t)layout.quality_offset, result);
        apta_dj_write_directory(
            directory + (size_t)new_index++ * APTA_DJ_DIRECTORY_ENTRY_SIZE,
            "CONF", layout.quality_offset, layout.quality_size,
            bytes + (size_t)layout.quality_offset);
    }
    apta_dj_put_u32(bytes + 20u, old_count + layout.section_count);
    apta_dj_put_u64(bytes + 32u, layout.total_size);
    if (apta_dj_requires_partial(result, &layout)) {
        apta_dj_put_u32(
            bytes + 16u,
            apta_dj_get_u32(bytes + 16u) |
                APTA_CONTAINER_FLAG_PARTIAL_RESULT);
    }
    apta_dj_put_u32(bytes + 92u, apta_internal_crc32c(bytes, 92u));
    *bytes_written_out = (size_t)layout.total_size;
    return APTA_STATUS_OK;
}
