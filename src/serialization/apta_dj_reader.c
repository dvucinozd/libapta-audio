// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"
#include "../core/apta_grid_match_internal.h"
#include "../beatgrid/apta_s6_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#if defined(APTA_ENABLE_TEST_HOOKS)
uint64_t apta_test_reader_grid_match_work;
#define APTA_TEST_READER_GRID_WORK_PTR (&apta_test_reader_grid_match_work)
#else
#define APTA_TEST_READER_GRID_WORK_PTR NULL
#endif

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

apta_status_t APTA_CALL apta_result_parse_s6(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out);

typedef struct {
    const uint8_t *key;
    size_t key_size;
    const uint8_t *meter;
    size_t meter_size;
    const uint8_t *quality;
    size_t quality_size;
} apta_dj_sections_t;

static uint16_t apta_dj_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static int16_t apta_dj_get_i16(const uint8_t *p)
{
    return (int16_t)apta_dj_get_u16(p);
}

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

static int64_t apta_dj_get_i64(const uint8_t *p)
{
    return (int64_t)apta_dj_get_u64(p);
}

static int apta_dj_all_zero(const uint8_t *p, size_t size)
{
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (p[index] != 0u) return 0;
    }
    return 1;
}

static int apta_dj_state_allowed(uint32_t state, int partial)
{
    return state >= APTA_FEATURE_PARTIAL && state <= APTA_FEATURE_FINAL &&
           (partial || state == APTA_FEATURE_FINAL);
}

static int apta_dj_confidence_valid(uint8_t confidence)
{
    return confidence <= APTA_CONFIDENCE_MAX ||
           confidence == APTA_CONFIDENCE_UNKNOWN;
}

static int apta_dj_range_valid(
    const apta_result_t *result,
    uint64_t first,
    uint64_t end)
{
    return first < end &&
           (result->total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN ||
            end <= result->total_source_frames);
}

static int apta_dj_key_value_valid(
    uint8_t tonic,
    uint8_t mode,
    int16_t tuning)
{
    return tonic <= 11u &&
           (mode == APTA_KEY_MODE_MAJOR || mode == APTA_KEY_MODE_MINOR) &&
           tuning >= -100 && tuning <= 100;
}

static int apta_dj_meter_value_valid(uint16_t numerator, uint16_t denominator)
{
    return numerator >= 1u && numerator <= 32u && denominator >= 1u &&
           denominator <= 32u && (denominator & (denominator - 1u)) == 0u;
}

static uint64_t apta_dj_allocation_limit(const apta_parse_options_t *options)
{
    return options != NULL && options->maximum_allocation_bytes != 0u
               ? options->maximum_allocation_bytes
               : UINT64_C(268435456);
}

static apta_status_t apta_dj_find_sections(
    const uint8_t *bytes,
    size_t size,
    apta_dj_sections_t *sections)
{
    uint32_t count;
    uint64_t directory_offset;
    uint32_t index;

    if (bytes == NULL || sections == NULL || size < 96u) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    memset(sections, 0, sizeof(*sections));
    count = apta_dj_get_u32(bytes + 20u);
    directory_offset = apta_dj_get_u64(bytes + 24u);
    if (directory_offset > size ||
        count > (size - (size_t)directory_offset) /
                    APTA_DJ_DIRECTORY_ENTRY_SIZE) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    for (index = 0u; index < count; ++index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)index * APTA_DJ_DIRECTORY_ENTRY_SIZE;
        const uint8_t **payload_out;
        size_t *size_out;
        uint64_t offset;
        uint64_t stored_size;

        if (memcmp(entry, "MKEY", 4u) == 0) {
            payload_out = &sections->key;
            size_out = &sections->key_size;
        } else if (memcmp(entry, "MTRD", 4u) == 0) {
            payload_out = &sections->meter;
            size_out = &sections->meter_size;
        } else if (memcmp(entry, "CONF", 4u) == 0) {
            payload_out = &sections->quality;
            size_out = &sections->quality_size;
        } else {
            continue;
        }
        if (*payload_out != NULL) return APTA_ERROR_CORRUPT_DATA;
        if (apta_dj_get_u16(entry + 4u) != 1u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        if (apta_dj_get_u16(entry + 6u) != 0u ||
            apta_dj_get_u32(entry + 36u) != 0u) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        offset = apta_dj_get_u64(entry + 8u);
        stored_size = apta_dj_get_u64(entry + 16u);
        if (apta_dj_get_u64(entry + 24u) != stored_size ||
            offset > size || stored_size > size - (size_t)offset ||
            stored_size > SIZE_MAX ||
            apta_internal_crc32c(bytes + (size_t)offset,
                                 (size_t)stored_size) !=
                apta_dj_get_u32(entry + 32u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        *payload_out = bytes + (size_t)offset;
        *size_out = (size_t)stored_size;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_dj_parse_key(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *payload,
    size_t size,
    int partial,
    apta_result_t *result)
{
    uint32_t count;
    size_t expected_size;
    uint16_t previous_score = UINT16_MAX;
    uint32_t index;
    int selected_found = 0;

    if (size < APTA_MKEY_HEADER_SIZE || apta_dj_get_u16(payload) != 1u ||
        !apta_dj_state_allowed(payload[2], partial) ||
        !apta_dj_confidence_valid(payload[3]) ||
        !apta_dj_key_value_valid(
            payload[4], payload[5], apta_dj_get_i16(payload + 6u)) ||
        apta_dj_get_u32(payload + 8u) != 0u ||
        !apta_dj_range_valid(
            result, apta_dj_get_u64(payload + 16u),
            apta_dj_get_u64(payload + 24u)) ||
        apta_dj_get_u32(payload + 32u) != APTA_MKEY_HEADER_SIZE ||
        !apta_dj_all_zero(payload + 36u, 4u)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    count = apta_dj_get_u32(payload + 12u);
    if (count > APTA_DJ_MAX_KEY_CANDIDATES ||
        !apta_internal_size_array_fits(
            APTA_MKEY_HEADER_SIZE, count, APTA_MKEY_CANDIDATE_SIZE)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    expected_size = APTA_MKEY_HEADER_SIZE +
                    (size_t)count * APTA_MKEY_CANDIDATE_SIZE;
    if (size != expected_size) return APTA_ERROR_CORRUPT_DATA;
    for (index = 0u; index < count; ++index) {
        const uint8_t *record = payload + APTA_MKEY_HEADER_SIZE +
                                (size_t)index * APTA_MKEY_CANDIDATE_SIZE;
        uint32_t prior;
        if (!apta_dj_key_value_valid(
                record[0], record[1], apta_dj_get_i16(record + 2u)) ||
            !apta_dj_confidence_valid(record[6]) || record[7] != 0u ||
            apta_dj_get_u32(record + 8u) != 0u ||
            apta_dj_get_u32(record + 12u) != 0u ||
            (index != 0u &&
             apta_dj_get_u16(record + 4u) >= previous_score)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        for (prior = 0u; prior < index; ++prior) {
            const uint8_t *previous = payload + APTA_MKEY_HEADER_SIZE +
                (size_t)prior * APTA_MKEY_CANDIDATE_SIZE;
            if (previous[0] == record[0] && previous[1] == record[1] &&
                apta_dj_get_i16(previous + 2u) ==
                    apta_dj_get_i16(record + 2u)) {
                return APTA_ERROR_CORRUPT_DATA;
            }
        }
        if (record[0] == payload[4] && record[1] == payload[5] &&
            apta_dj_get_i16(record + 2u) ==
                apta_dj_get_i16(payload + 6u)) {
            selected_found = 1;
        }
        previous_score = apta_dj_get_u16(record + 4u);
    }
    if (count != 0u && !selected_found) return APTA_ERROR_CORRUPT_DATA;
    if (count != 0u) {
        const size_t bytes = (size_t)count * sizeof(apta_key_candidate_t);
        if (!apta_internal_result_allocation_fits(
                result, bytes, apta_dj_allocation_limit(options))) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        result->key_candidates =
            (apta_key_candidate_t *)apta_internal_context_allocate(
                context, bytes, alignof(apta_key_candidate_t),
                APTA_MEMORY_PERSISTENT);
        if (result->key_candidates == NULL) return APTA_ERROR_OUT_OF_MEMORY;
        memset(result->key_candidates, 0, bytes);
    }
    apta_key_view_init(&result->key);
    result->key.state = payload[2];
    result->key.confidence = payload[3];
    result->key.tonic = payload[4];
    result->key.mode = payload[5];
    result->key.tuning_offset_cents = apta_dj_get_i16(payload + 6u);
    result->key.applicability_range.first_frame =
        apta_dj_get_u64(payload + 16u);
    result->key.applicability_range.end_frame =
        apta_dj_get_u64(payload + 24u);
    result->key.candidate_count = count;
    result->key.candidates = result->key_candidates;
    for (index = 0u; index < count; ++index) {
        const uint8_t *record = payload + APTA_MKEY_HEADER_SIZE +
                                (size_t)index * APTA_MKEY_CANDIDATE_SIZE;
        apta_key_candidate_t *candidate = &result->key_candidates[index];
        candidate->tonic = record[0];
        candidate->mode = record[1];
        candidate->tuning_offset_cents = apta_dj_get_i16(record + 2u);
        candidate->score = apta_dj_get_u16(record + 4u);
        candidate->confidence = record[6];
    }
    result->info.available_features |= APTA_FEATURE_MUSICAL_KEY;
    result->info.changed_features |= APTA_FEATURE_MUSICAL_KEY;
    if (result->key.confidence != APTA_CONFIDENCE_UNKNOWN) {
        result->info.available_features |= APTA_FEATURE_CONFIDENCE;
        result->info.changed_features |= APTA_FEATURE_CONFIDENCE;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_dj_parse_meter(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *payload,
    size_t size,
    int partial,
    apta_result_t *result)
{
    uint32_t count;
    size_t expected_size;
    uint32_t index;
    apta_internal_grid_match_set_t grids;

    if (size < APTA_MTRD_HEADER_SIZE || apta_dj_get_u16(payload) != 1u ||
        !apta_dj_state_allowed(payload[2], partial) ||
        !apta_dj_confidence_valid(payload[3]) ||
        !apta_dj_meter_value_valid(
            apta_dj_get_u16(payload + 4u),
            apta_dj_get_u16(payload + 6u)) ||
        apta_dj_get_u32(payload + 8u) != 0u ||
        apta_dj_get_u32(payload + 32u) != APTA_MTRD_HEADER_SIZE ||
        !apta_dj_all_zero(payload + 36u, 12u)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    count = apta_dj_get_u32(payload + 12u);
    if (count == 0u) return APTA_ERROR_CORRUPT_DATA;
    if (count > APTA_DJ_MAX_METER_SEGMENTS ||
        !apta_internal_size_array_fits(
            APTA_MTRD_HEADER_SIZE, count, APTA_MTRD_SEGMENT_SIZE)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    expected_size = APTA_MTRD_HEADER_SIZE +
                    (size_t)count * APTA_MTRD_SEGMENT_SIZE;
    if (size != expected_size) return APTA_ERROR_CORRUPT_DATA;
    if ((result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) != 0u &&
        result->s6 == NULL) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    apta_internal_grid_match_set_init(
        &grids,
        (result->info.available_features & APTA_FEATURE_LOCAL_BEATGRID) != 0u
            ? &result->local_grid : NULL,
        (result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) != 0u
            ? &result->s6->global_grid : NULL,
        APTA_TEST_READER_GRID_WORK_PTR);
    for (index = 0u; index < count; ++index) {
        const uint8_t *record = payload + APTA_MTRD_HEADER_SIZE +
                                (size_t)index * APTA_MTRD_SEGMENT_SIZE;
        const uint64_t first = apta_dj_get_u64(record);
        const uint64_t end = apta_dj_get_u64(record + 8u);
        const uint64_t downbeat = apta_dj_get_u64(record + 16u);
        if (!apta_dj_range_valid(result, first, end) ||
            downbeat < first || downbeat >= end ||
            !apta_dj_meter_value_valid(
                apta_dj_get_u16(record + 32u),
                apta_dj_get_u16(record + 34u)) ||
            !apta_dj_state_allowed(record[36], partial) ||
            record[36] < payload[2] ||
            !apta_dj_confidence_valid(record[37]) ||
            !apta_dj_all_zero(record + 38u, 2u) ||
            apta_dj_get_u32(record + 40u) != 0u ||
            apta_dj_get_u32(record + 44u) == 0u ||
            !apta_dj_all_zero(record + 48u, 8u) ||
            !apta_internal_grid_match_set_next(
                &grids, downbeat, apta_dj_get_i64(record + 24u)) ||
            (index != 0u &&
             (apta_dj_get_u64(record - APTA_MTRD_SEGMENT_SIZE + 8u) >
                  first ||
              apta_dj_get_i64(record - APTA_MTRD_SEGMENT_SIZE + 24u) >=
                  apta_dj_get_i64(record + 24u) ||
              apta_dj_get_u32(record - APTA_MTRD_SEGMENT_SIZE + 44u) >=
                  apta_dj_get_u32(record + 44u)))) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    }
    if (apta_dj_get_u64(payload + 16u) !=
            apta_dj_get_u64(payload + APTA_MTRD_HEADER_SIZE + 16u) ||
        apta_dj_get_i64(payload + 24u) !=
            apta_dj_get_i64(payload + APTA_MTRD_HEADER_SIZE + 24u) ||
        apta_dj_get_u16(payload + 4u) !=
            apta_dj_get_u16(payload + APTA_MTRD_HEADER_SIZE + 32u) ||
        apta_dj_get_u16(payload + 6u) !=
            apta_dj_get_u16(payload + APTA_MTRD_HEADER_SIZE + 34u)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    {
        const size_t bytes = (size_t)count * sizeof(apta_meter_segment_t);
        if (!apta_internal_result_allocation_fits(
                result, bytes, apta_dj_allocation_limit(options))) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        result->meter_segments =
            (apta_meter_segment_t *)apta_internal_context_allocate(
                context, bytes, alignof(apta_meter_segment_t),
                APTA_MEMORY_PERSISTENT);
        if (result->meter_segments == NULL) return APTA_ERROR_OUT_OF_MEMORY;
        memset(result->meter_segments, 0, bytes);
    }
    apta_meter_view_init(&result->meter);
    result->meter.state = payload[2];
    result->meter.confidence = payload[3];
    result->meter.numerator = apta_dj_get_u16(payload + 4u);
    result->meter.denominator = apta_dj_get_u16(payload + 6u);
    result->meter.downbeat_frame = apta_dj_get_u64(payload + 16u);
    result->meter.downbeat_ordinal = apta_dj_get_i64(payload + 24u);
    result->meter.segment_count = count;
    result->meter.segments = result->meter_segments;
    for (index = 0u; index < count; ++index) {
        const uint8_t *record = payload + APTA_MTRD_HEADER_SIZE +
                                (size_t)index * APTA_MTRD_SEGMENT_SIZE;
        apta_meter_segment_t *segment = &result->meter_segments[index];
        segment->struct_size = (uint32_t)sizeof(*segment);
        segment->api_version = APTA_API_VERSION;
        apta_frame_range_init(&segment->applicability_range);
        segment->applicability_range.first_frame = apta_dj_get_u64(record);
        segment->applicability_range.end_frame = apta_dj_get_u64(record + 8u);
        segment->downbeat_frame = apta_dj_get_u64(record + 16u);
        segment->downbeat_ordinal = apta_dj_get_i64(record + 24u);
        segment->numerator = apta_dj_get_u16(record + 32u);
        segment->denominator = apta_dj_get_u16(record + 34u);
        segment->state = record[36];
        segment->confidence = record[37];
        segment->segment_id = apta_dj_get_u32(record + 44u);
    }
    result->info.available_features |= APTA_FEATURE_METER_DOWNBEAT;
    result->info.changed_features |= APTA_FEATURE_METER_DOWNBEAT;
    if (result->meter.confidence != APTA_CONFIDENCE_UNKNOWN) {
        result->info.available_features |= APTA_FEATURE_CONFIDENCE;
        result->info.changed_features |= APTA_FEATURE_CONFIDENCE;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_dj_parse_quality(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *payload,
    size_t size,
    int partial,
    apta_result_t *result)
{
    uint32_t count;
    size_t expected_size;
    apta_feature_mask_t previous_feature = 0u;
    uint32_t index;

    if (size < APTA_CONF_HEADER_SIZE || apta_dj_get_u16(payload) != 1u ||
        apta_dj_get_u16(payload + 2u) != APTA_CONF_RECORD_SIZE ||
        apta_dj_get_u32(payload + 8u) != APTA_CONF_HEADER_SIZE ||
        apta_dj_get_u32(payload + 12u) != 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    count = apta_dj_get_u32(payload + 4u);
    if (count == 0u) return APTA_ERROR_CORRUPT_DATA;
    if (count > APTA_DJ_MAX_QUALITY_RECORDS ||
        !apta_internal_size_array_fits(
            APTA_CONF_HEADER_SIZE, count, APTA_CONF_RECORD_SIZE)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    expected_size = APTA_CONF_HEADER_SIZE +
                    (size_t)count * APTA_CONF_RECORD_SIZE;
    if (size != expected_size) return APTA_ERROR_CORRUPT_DATA;
    for (index = 0u; index < count; ++index) {
        const uint8_t *record = payload + APTA_CONF_HEADER_SIZE +
                                (size_t)index * APTA_CONF_RECORD_SIZE;
        const apta_feature_mask_t feature = apta_dj_get_u64(record);
        const uint16_t coverage = apta_dj_get_u16(record + 12u);
        if (feature == 0u || (feature & (feature - 1u)) != 0u ||
            (feature & APTA_DJ_QUALITY_TARGETS) == 0u ||
            (result->info.available_features & feature) == 0u ||
            feature <= previous_feature ||
            (coverage > APTA_EVIDENCE_COVERAGE_MAX &&
             coverage != APTA_EVIDENCE_COVERAGE_UNKNOWN) ||
            !apta_dj_confidence_valid(record[14]) ||
            !apta_dj_state_allowed(record[15], partial) ||
            (apta_dj_get_u32(record + 16u) &
             ~APTA_DJ_QUALITY_FLAGS) != 0u ||
            !apta_dj_all_zero(record + 20u, 12u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        previous_feature = feature;
    }
    {
        const size_t bytes = (size_t)count * sizeof(apta_quality_view_t);
        if (!apta_internal_result_allocation_fits(
                result, bytes, apta_dj_allocation_limit(options))) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        result->quality =
            (apta_quality_view_t *)apta_internal_context_allocate(
                context, bytes, alignof(apta_quality_view_t),
                APTA_MEMORY_PERSISTENT);
        if (result->quality == NULL) return APTA_ERROR_OUT_OF_MEMORY;
        memset(result->quality, 0, bytes);
    }
    result->quality_count = count;
    for (index = 0u; index < count; ++index) {
        const uint8_t *record = payload + APTA_CONF_HEADER_SIZE +
                                (size_t)index * APTA_CONF_RECORD_SIZE;
        apta_quality_view_t *quality = &result->quality[index];
        apta_quality_view_init(quality);
        quality->feature = apta_dj_get_u64(record);
        quality->calibration_model_id = apta_dj_get_u32(record + 8u);
        quality->evidence_coverage_permille = apta_dj_get_u16(record + 12u);
        quality->confidence = record[14];
        quality->state = record[15];
        quality->flags = apta_dj_get_u32(record + 16u);
    }
    result->info.available_features |= APTA_FEATURE_CALIBRATED_QUALITY;
    result->info.changed_features |= APTA_FEATURE_CALIBRATED_QUALITY;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_parse(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out)
{
    const apta_result_t *base_result = NULL;
    apta_result_t *result;
    apta_dj_sections_t sections;
    apta_status_t status;
    int partial;

    if (result_out == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    *result_out = NULL;
    if (context == NULL || buffer == NULL) return APTA_ERROR_INVALID_ARGUMENT;
    status = apta_result_parse_s6(
        context, options, buffer, buffer_size, &base_result);
    if (status < 0) return status;
    status = apta_dj_find_sections(
        (const uint8_t *)buffer, buffer_size, &sections);
    if (status < 0) {
        apta_result_release(base_result);
        return status;
    }
    if (sections.key == NULL && sections.meter == NULL &&
        sections.quality == NULL) {
        *result_out = base_result;
        return APTA_STATUS_OK;
    }
    result = (apta_result_t *)base_result;
    partial = (apta_dj_get_u32((const uint8_t *)buffer + 16u) &
               APTA_CONTAINER_FLAG_PARTIAL_RESULT) != 0u;
    if (sections.key != NULL) {
        status = apta_dj_parse_key(
            context, options, sections.key, sections.key_size,
            partial, result);
        if (status < 0) goto failure;
    }
    if (sections.meter != NULL) {
        status = apta_dj_parse_meter(
            context, options, sections.meter, sections.meter_size,
            partial, result);
        if (status < 0) goto failure;
    }
    if (sections.quality != NULL) {
        status = apta_dj_parse_quality(
            context, options, sections.quality, sections.quality_size,
            partial, result);
        if (status < 0) goto failure;
    }
    *result_out = base_result;
    return APTA_STATUS_OK;

failure:
    apta_result_release(base_result);
    return status;
}
