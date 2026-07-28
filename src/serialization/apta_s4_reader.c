// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#define APTA_S4_DIRECTORY_ENTRY_SIZE 40u
#define APTA_TEMP_HEADER_SIZE 56u
#define APTA_TEMP_CANDIDATE_SIZE 16u
#define APTA_LGRD_PAYLOAD_SIZE 144u

APTA_API apta_status_t APTA_CALL apta_result_parse_meta_hardened(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out);

typedef struct {
    const uint8_t *temp;
    size_t temp_size;
    const uint8_t *grid;
    size_t grid_size;
} apta_s4_sections_t;

static uint16_t apta_s4_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t apta_s4_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static uint64_t apta_s4_get_u64(const uint8_t *p)
{
    return (uint64_t)apta_s4_get_u32(p) |
           ((uint64_t)apta_s4_get_u32(p + 4u) << 32u);
}

static int apta_s4_state_valid(uint32_t state)
{
    return state >= APTA_FEATURE_PROVISIONAL &&
           state <= APTA_FEATURE_FINAL;
}

static int apta_s4_relation_valid(uint32_t relation)
{
    return relation <= APTA_TEMPO_RELATION_TWO_THIRDS;
}

static int apta_s4_range_valid(uint64_t first, uint64_t end)
{
    return first < end;
}

static apta_status_t apta_s4_find_sections(
    const uint8_t *bytes,
    size_t size,
    apta_s4_sections_t *sections)
{
    uint32_t count;
    uint64_t directory_offset;
    uint32_t index;

    if (size < 96u || sections == NULL) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    memset(sections, 0, sizeof(*sections));
    count = apta_s4_get_u32(bytes + 20u);
    directory_offset = apta_s4_get_u64(bytes + 24u);
    if (directory_offset > size ||
        (uint64_t)count * APTA_S4_DIRECTORY_ENTRY_SIZE >
            size - directory_offset) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    for (index = 0u; index < count; ++index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)index * APTA_S4_DIRECTORY_ENTRY_SIZE;
        uint64_t offset;
        uint64_t section_size;
        const uint8_t **payload_out = NULL;
        size_t *payload_size_out = NULL;

        if (memcmp(entry, "TEMP", 4u) == 0) {
            if (sections->temp != NULL) {
                return APTA_ERROR_CORRUPT_DATA;
            }
            payload_out = &sections->temp;
            payload_size_out = &sections->temp_size;
        } else if (memcmp(entry, "LGRD", 4u) == 0) {
            if (sections->grid != NULL) {
                return APTA_ERROR_CORRUPT_DATA;
            }
            payload_out = &sections->grid;
            payload_size_out = &sections->grid_size;
        } else {
            continue;
        }

        if (apta_s4_get_u16(entry + 4u) != 1u ||
            apta_s4_get_u16(entry + 6u) != 0u ||
            apta_s4_get_u32(entry + 36u) != 0u) {
            return apta_s4_get_u16(entry + 4u) != 1u
                       ? APTA_ERROR_UNSUPPORTED
                       : APTA_ERROR_CORRUPT_DATA;
        }
        offset = apta_s4_get_u64(entry + 8u);
        section_size = apta_s4_get_u64(entry + 16u);
        if (apta_s4_get_u64(entry + 24u) != section_size ||
            offset > size || section_size > size - offset ||
            section_size > SIZE_MAX ||
            apta_internal_crc32c(bytes + (size_t)offset,
                                 (size_t)section_size) !=
                apta_s4_get_u32(entry + 32u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        *payload_out = bytes + (size_t)offset;
        *payload_size_out = (size_t)section_size;
    }

    if (sections->grid != NULL && sections->temp == NULL) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_s4_parse_temp(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *payload,
    size_t size,
    apta_result_t *result)
{
    uint32_t count;
    size_t expected_size;
    size_t bytes;
    uint32_t index;
    uint64_t limit = options != NULL &&
                             options->maximum_allocation_bytes != 0u
                         ? options->maximum_allocation_bytes
                         : UINT64_C(268435456);

    if (size < APTA_TEMP_HEADER_SIZE || apta_s4_get_u16(payload) != 1u ||
        payload[2] < APTA_FEATURE_PROVISIONAL ||
        payload[2] > APTA_FEATURE_FINAL ||
        payload[3] > APTA_CONFIDENCE_MAX ||
        apta_s4_get_u32(payload + 8u) <
            APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
        apta_s4_get_u32(payload + 8u) >
            APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
        !apta_s4_range_valid(apta_s4_get_u64(payload + 16u),
                             apta_s4_get_u64(payload + 24u)) ||
        !apta_s4_range_valid(apta_s4_get_u64(payload + 32u),
                             apta_s4_get_u64(payload + 40u)) ||
        apta_s4_get_u32(payload + 52u) != 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    count = apta_s4_get_u32(payload + 48u);
    if (count == 0u || count > APTA_INTERNAL_MAX_TEMPO_CANDIDATES) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    expected_size = APTA_TEMP_HEADER_SIZE +
                    (size_t)count * APTA_TEMP_CANDIDATE_SIZE;
    if (size != expected_size) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    bytes = (size_t)count * sizeof(apta_tempo_candidate_t);
    if ((uint64_t)bytes > limit) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    result->tempo_candidates =
        (apta_tempo_candidate_t *)apta_internal_context_allocate(
            context,
            bytes,
            alignof(apta_tempo_candidate_t),
            APTA_MEMORY_PERSISTENT);
    if (result->tempo_candidates == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(result->tempo_candidates, 0, bytes);

    apta_tempo_view_init(&result->tempo);
    result->tempo.selected.state = payload[2];
    result->tempo.selected.confidence = payload[3];
    result->tempo.selected.flags = apta_s4_get_u32(payload + 4u);
    result->tempo.selected.tempo_millibpm = apta_s4_get_u32(payload + 8u);
    result->tempo.selected.candidate_set_id = apta_s4_get_u32(payload + 12u);
    result->tempo.selected.evidence_range.first_frame =
        apta_s4_get_u64(payload + 16u);
    result->tempo.selected.evidence_range.end_frame =
        apta_s4_get_u64(payload + 24u);
    result->tempo.selected.applicability_range.first_frame =
        apta_s4_get_u64(payload + 32u);
    result->tempo.selected.applicability_range.end_frame =
        apta_s4_get_u64(payload + 40u);

    for (index = 0u; index < count; ++index) {
        const uint8_t *entry = payload + APTA_TEMP_HEADER_SIZE +
                               (size_t)index * APTA_TEMP_CANDIDATE_SIZE;
        apta_tempo_candidate_t *candidate = &result->tempo_candidates[index];
        uint32_t relation = entry[7];

        if (apta_s4_get_u32(entry) < APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
            apta_s4_get_u32(entry) > APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
            entry[6] > APTA_CONFIDENCE_MAX ||
            !apta_s4_relation_valid(relation) ||
            apta_s4_get_u32(entry + 12u) != 0u ||
            (index != 0u &&
             apta_s4_get_u16(entry + 4u) >
                 result->tempo_candidates[index - 1u].score)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        candidate->tempo_millibpm = apta_s4_get_u32(entry);
        candidate->score = apta_s4_get_u16(entry + 4u);
        candidate->confidence = entry[6];
        candidate->relation_to_selected = relation;
        candidate->flags = apta_s4_get_u32(entry + 8u);
    }

    result->tempo.candidate_count = count;
    result->tempo.candidates = result->tempo_candidates;
    result->info.available_features |=
        APTA_FEATURE_BPM | APTA_FEATURE_CONFIDENCE;
    return APTA_STATUS_OK;
}

static apta_status_t apta_s4_parse_grid(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *payload,
    size_t size,
    apta_result_t *result)
{
    apta_grid_segment_t *segment;
    uint64_t limit = options != NULL &&
                             options->maximum_allocation_bytes != 0u
                         ? options->maximum_allocation_bytes
                         : UINT64_C(268435456);
    const uint64_t needed =
        sizeof(apta_frame_range_t) + sizeof(apta_grid_segment_t);

    if (size != APTA_LGRD_PAYLOAD_SIZE || apta_s4_get_u16(payload) != 1u ||
        !apta_s4_state_valid(payload[2]) ||
        payload[3] > APTA_CONFIDENCE_MAX ||
        apta_s4_get_u32(payload + 8u) !=
            APTA_GRID_REPRESENTATION_SEGMENTS ||
        apta_s4_get_u32(payload + 12u) != 1u ||
        !apta_s4_range_valid(apta_s4_get_u64(payload + 16u),
                             apta_s4_get_u64(payload + 24u)) ||
        !apta_s4_range_valid(apta_s4_get_u64(payload + 32u),
                             apta_s4_get_u64(payload + 40u)) ||
        !apta_s4_range_valid(apta_s4_get_u64(payload + 48u),
                             apta_s4_get_u64(payload + 56u)) ||
        !apta_s4_range_valid(apta_s4_get_u64(payload + 64u),
                             apta_s4_get_u64(payload + 72u)) ||
        apta_s4_get_u64(payload + 104u) == 0u ||
        apta_s4_get_u32(payload + 120u) <
            APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
        apta_s4_get_u32(payload + 120u) >
            APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
        !apta_s4_state_valid(payload[136]) ||
        payload[137] > APTA_CONFIDENCE_MAX ||
        payload[138] != 0u || payload[139] != 0u ||
        apta_s4_get_u32(payload + 140u) != 0u ||
        needed > limit) {
        return needed > limit
                   ? APTA_ERROR_LIMIT_EXCEEDED
                   : APTA_ERROR_CORRUPT_DATA;
    }
    if (apta_s4_get_u32(payload + 120u) !=
        result->tempo.selected.tempo_millibpm) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    result->local_grid_coverage =
        (apta_frame_range_t *)apta_internal_context_allocate(
            context,
            sizeof(apta_frame_range_t),
            alignof(apta_frame_range_t),
            APTA_MEMORY_PERSISTENT);
    result->local_grid_segments =
        (apta_grid_segment_t *)apta_internal_context_allocate(
            context,
            sizeof(apta_grid_segment_t),
            alignof(apta_grid_segment_t),
            APTA_MEMORY_PERSISTENT);
    if (result->local_grid_coverage == NULL ||
        result->local_grid_segments == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    apta_frame_range_init(result->local_grid_coverage);
    result->local_grid_coverage->first_frame = apta_s4_get_u64(payload + 64u);
    result->local_grid_coverage->end_frame = apta_s4_get_u64(payload + 72u);

    segment = result->local_grid_segments;
    memset(segment, 0, sizeof(*segment));
    segment->struct_size = (uint32_t)sizeof(*segment);
    segment->api_version = APTA_API_VERSION;
    apta_frame_range_init(&segment->applicability_range);
    segment->applicability_range.first_frame = apta_s4_get_u64(payload + 48u);
    segment->applicability_range.end_frame = apta_s4_get_u64(payload + 56u);
    segment->anchor_position.whole_frame = apta_s4_get_u64(payload + 80u);
    segment->anchor_position.fraction_q32 = apta_s4_get_u32(payload + 88u);
    segment->anchor_ordinal = (int64_t)apta_s4_get_u64(payload + 96u);
    segment->frames_per_beat.whole_frames = apta_s4_get_u64(payload + 104u);
    segment->frames_per_beat.fraction_q32 = apta_s4_get_u32(payload + 112u);
    segment->beat_count = apta_s4_get_u32(payload + 116u);
    segment->nominal_tempo_millibpm = apta_s4_get_u32(payload + 120u);
    segment->segment_id = apta_s4_get_u32(payload + 124u);
    segment->revision = apta_s4_get_u32(payload + 128u);
    segment->flags = apta_s4_get_u32(payload + 132u);
    segment->state = payload[136];
    segment->confidence = payload[137];

    apta_grid_view_init(&result->local_grid);
    result->local_grid.requested_range.first_frame =
        apta_s4_get_u64(payload + 16u);
    result->local_grid.requested_range.end_frame =
        apta_s4_get_u64(payload + 24u);
    result->local_grid.evidence_range.first_frame =
        apta_s4_get_u64(payload + 32u);
    result->local_grid.evidence_range.end_frame =
        apta_s4_get_u64(payload + 40u);
    result->local_grid.applicability_range.first_frame =
        apta_s4_get_u64(payload + 48u);
    result->local_grid.applicability_range.end_frame =
        apta_s4_get_u64(payload + 56u);
    result->local_grid.representation = APTA_GRID_REPRESENTATION_SEGMENTS;
    result->local_grid.state = payload[2];
    result->local_grid.confidence = payload[3];
    result->local_grid.coverage_range_count = 1u;
    result->local_grid.coverage_ranges = result->local_grid_coverage;
    result->local_grid.segment_count = 1u;
    result->local_grid.segments = result->local_grid_segments;
    result->local_grid.flags = apta_s4_get_u32(payload + 4u);
    result->info.available_features |= APTA_FEATURE_LOCAL_BEATGRID;
    if ((result->local_grid.flags & APTA_GRID_FLAG_LOCKED) != 0u) {
        result->info.available_features |= APTA_FEATURE_GRID_LOCKING;
    }
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
    apta_s4_sections_t sections;
    apta_status_t status;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;
    if (context == NULL || buffer == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_result_parse_meta_hardened(
        context,
        options,
        buffer,
        buffer_size,
        &base_result);
    if (status < 0) {
        return status;
    }

    status = apta_s4_find_sections(
        (const uint8_t *)buffer,
        buffer_size,
        &sections);
    if (status < 0) {
        apta_result_release(base_result);
        return status;
    }
    if (sections.temp == NULL) {
        *result_out = base_result;
        return APTA_STATUS_OK;
    }

    result = (apta_result_t *)base_result;
    status = apta_s4_parse_temp(
        context,
        options,
        sections.temp,
        sections.temp_size,
        result);
    if (status >= 0 && sections.grid != NULL) {
        status = apta_s4_parse_grid(
            context,
            options,
            sections.grid,
            sections.grid_size,
            result);
    }
    if (status < 0) {
        apta_result_release(base_result);
        return status;
    }

    *result_out = base_result;
    return APTA_STATUS_OK;
}
