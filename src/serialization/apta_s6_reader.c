// SPDX-License-Identifier: Apache-2.0
#include "../beatgrid/apta_s6_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#define APTA_S6_DIRECTORY_ENTRY_SIZE 40u
#define APTA_GGRD_HEADER_SIZE 96u
#define APTA_GGRD_SEGMENT_SIZE 80u
#define APTA_GGRD_BEAT_SIZE 40u
#define APTA_REVN_PAYLOAD_SIZE 80u
#define APTA_CONTAINER_FLAG_PARTIAL_RESULT (1u << 0)

APTA_API apta_status_t APTA_CALL apta_result_parse_s4(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out);

typedef struct {
    const uint8_t *grid;
    size_t grid_size;
    const uint8_t *revision;
    size_t revision_size;
    uint32_t grid_index;
    uint32_t revision_index;
} apta_s6_sections_t;

static uint16_t apta_s6_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t apta_s6_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static uint64_t apta_s6_get_u64(const uint8_t *p)
{
    return (uint64_t)apta_s6_get_u32(p) |
           ((uint64_t)apta_s6_get_u32(p + 4u) << 32u);
}

static int apta_s6_all_zero(const uint8_t *p, size_t size)
{
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (p[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int apta_s6_is_strict(const apta_parse_options_t *options)
{
    return options == NULL || (options->flags & APTA_PARSE_STRICT) != 0u;
}

static int apta_s6_range_valid(uint64_t first, uint64_t end)
{
    return first < end;
}

static int apta_s6_state_valid(uint32_t state)
{
    return state >= APTA_FEATURE_PROVISIONAL &&
           state <= APTA_FEATURE_FINAL;
}

static int apta_s6_state_allowed(uint32_t state, int partial)
{
    return apta_s6_state_valid(state) &&
           (partial || state == APTA_FEATURE_FINAL);
}

static int apta_s6_representation_valid(uint32_t representation)
{
    return representation >= APTA_GRID_REPRESENTATION_SEGMENTS &&
           representation <= APTA_GRID_REPRESENTATION_HYBRID;
}

static apta_status_t apta_s6_find_sections(
    const uint8_t *bytes,
    size_t size,
    int strict,
    apta_s6_sections_t *sections)
{
    uint32_t count;
    uint64_t directory_offset;
    uint32_t index;

    if (bytes == NULL || size < 96u || sections == NULL) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    memset(sections, 0, sizeof(*sections));
    sections->grid_index = UINT32_MAX;
    sections->revision_index = UINT32_MAX;
    count = apta_s6_get_u32(bytes + 20u);
    directory_offset = apta_s6_get_u64(bytes + 24u);
    if (directory_offset > size ||
        (uint64_t)count * APTA_S6_DIRECTORY_ENTRY_SIZE >
            size - directory_offset) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    for (index = 0u; index < count; ++index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)index * APTA_S6_DIRECTORY_ENTRY_SIZE;
        const uint8_t **payload_out = NULL;
        size_t *size_out = NULL;
        uint32_t *index_out = NULL;
        uint64_t offset;
        uint64_t section_size;

        if (memcmp(entry, "GGRD", 4u) == 0) {
            if (sections->grid != NULL) {
                return APTA_ERROR_CORRUPT_DATA;
            }
            payload_out = &sections->grid;
            size_out = &sections->grid_size;
            index_out = &sections->grid_index;
        } else if (memcmp(entry, "REVN", 4u) == 0) {
            if (sections->revision != NULL) {
                return APTA_ERROR_CORRUPT_DATA;
            }
            payload_out = &sections->revision;
            size_out = &sections->revision_size;
            index_out = &sections->revision_index;
        } else {
            continue;
        }

        if (apta_s6_get_u16(entry + 4u) != 1u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        if (apta_s6_get_u16(entry + 6u) != 0u ||
            (strict && apta_s6_get_u32(entry + 36u) != 0u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        offset = apta_s6_get_u64(entry + 8u);
        section_size = apta_s6_get_u64(entry + 16u);
        if (apta_s6_get_u64(entry + 24u) != section_size ||
            offset > size || section_size > size - offset ||
            section_size > SIZE_MAX ||
            apta_internal_crc32c(
                bytes + (size_t)offset,
                (size_t)section_size) != apta_s6_get_u32(entry + 32u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        *payload_out = bytes + (size_t)offset;
        *size_out = (size_t)section_size;
        *index_out = index;
    }

    if ((sections->grid == NULL) != (sections->revision == NULL)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (sections->grid != NULL &&
        sections->revision_index != sections->grid_index + 1u) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_s6_parse_grid(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const uint8_t *payload,
    size_t size,
    int partial,
    apta_result_t *result)
{
    apta_internal_s6_result_state_t *state;
    apta_grid_view_t *grid;
    uint32_t representation;
    uint32_t segment_count;
    uint32_t beat_count;
    size_t expected_size;
    size_t segment_bytes;
    size_t beat_bytes;
    uint64_t allocation_bytes;
    const int strict = apta_s6_is_strict(options);
    uint64_t limit = options != NULL &&
                             options->maximum_allocation_bytes != 0u
                         ? options->maximum_allocation_bytes
                         : UINT64_C(268435456);
    const uint8_t *cursor;
    uint32_t index;

    if (size < APTA_GGRD_HEADER_SIZE || apta_s6_get_u16(payload) != 1u ||
        !apta_s6_state_allowed(payload[2], partial) ||
        payload[3] > APTA_CONFIDENCE_MAX ||
        !apta_s6_representation_valid(apta_s6_get_u32(payload + 8u)) ||
        apta_s6_get_u32(payload + 12u) != 1u ||
        !apta_s6_range_valid(apta_s6_get_u64(payload + 24u),
                             apta_s6_get_u64(payload + 32u)) ||
        !apta_s6_range_valid(apta_s6_get_u64(payload + 40u),
                             apta_s6_get_u64(payload + 48u)) ||
        !apta_s6_range_valid(apta_s6_get_u64(payload + 56u),
                             apta_s6_get_u64(payload + 64u)) ||
        !apta_s6_range_valid(apta_s6_get_u64(payload + 72u),
                             apta_s6_get_u64(payload + 80u)) ||
        (strict && !apta_s6_all_zero(payload + 88u, 8u))) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    representation = apta_s6_get_u32(payload + 8u);
    segment_count = apta_s6_get_u32(payload + 16u);
    beat_count = apta_s6_get_u32(payload + 20u);
    if (segment_count > APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS ||
        beat_count > APTA_REFERENCE_GLOBAL_GRID_MAX_BEATS ||
        (representation == APTA_GRID_REPRESENTATION_SEGMENTS &&
         (segment_count == 0u || beat_count != 0u)) ||
        (representation == APTA_GRID_REPRESENTATION_EXPLICIT &&
         (segment_count != 0u || beat_count == 0u)) ||
        (representation == APTA_GRID_REPRESENTATION_HYBRID &&
         (segment_count == 0u || beat_count == 0u))) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    segment_bytes = (size_t)segment_count * sizeof(apta_grid_segment_t);
    beat_bytes = (size_t)beat_count * sizeof(apta_beat_t);
    expected_size = APTA_GGRD_HEADER_SIZE +
                    (size_t)segment_count * APTA_GGRD_SEGMENT_SIZE +
                    (size_t)beat_count * APTA_GGRD_BEAT_SIZE;
    if (size != expected_size) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    allocation_bytes = sizeof(apta_internal_s6_result_state_t) +
                       sizeof(apta_frame_range_t) +
                       (uint64_t)segment_bytes + (uint64_t)beat_bytes;
    if (!apta_internal_result_allocation_fits(
            result,
            allocation_bytes,
            limit)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    state = (apta_internal_s6_result_state_t *)apta_internal_context_allocate(
        context,
        sizeof(*state),
        alignof(apta_internal_s6_result_state_t),
        APTA_MEMORY_PERSISTENT);
    if (state == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(state, 0, sizeof(*state));
    result->s6 = state;
    state->coverage_ranges =
        (apta_frame_range_t *)apta_internal_context_allocate(
            context,
            sizeof(apta_frame_range_t),
            alignof(apta_frame_range_t),
            APTA_MEMORY_PERSISTENT);
    state->segments = segment_count != 0u
                          ? (apta_grid_segment_t *)
                                apta_internal_context_allocate(
                                    context,
                                    segment_bytes,
                                    alignof(apta_grid_segment_t),
                                    APTA_MEMORY_PERSISTENT)
                          : NULL;
    state->beats = beat_count != 0u
                       ? (apta_beat_t *)apta_internal_context_allocate(
                             context,
                             beat_bytes,
                             alignof(apta_beat_t),
                             APTA_MEMORY_PERSISTENT)
                       : NULL;
    if (state->coverage_ranges == NULL ||
        (segment_count != 0u && state->segments == NULL) ||
        (beat_count != 0u && state->beats == NULL)) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    apta_frame_range_init(state->coverage_ranges);
    state->coverage_ranges->first_frame = apta_s6_get_u64(payload + 72u);
    state->coverage_ranges->end_frame = apta_s6_get_u64(payload + 80u);

    grid = &state->global_grid;
    apta_grid_view_init(grid);
    grid->requested_range.first_frame = apta_s6_get_u64(payload + 24u);
    grid->requested_range.end_frame = apta_s6_get_u64(payload + 32u);
    grid->evidence_range.first_frame = apta_s6_get_u64(payload + 40u);
    grid->evidence_range.end_frame = apta_s6_get_u64(payload + 48u);
    grid->applicability_range.first_frame = apta_s6_get_u64(payload + 56u);
    grid->applicability_range.end_frame = apta_s6_get_u64(payload + 64u);
    grid->representation = representation;
    grid->state = payload[2];
    grid->confidence = payload[3];
    grid->coverage_range_count = 1u;
    grid->coverage_ranges = state->coverage_ranges;
    grid->segment_count = segment_count;
    grid->segments = state->segments;
    grid->beat_count = beat_count;
    grid->beats = state->beats;
    grid->flags = apta_s6_get_u32(payload + 4u);

    cursor = payload + APTA_GGRD_HEADER_SIZE;
    for (index = 0u; index < segment_count; ++index) {
        apta_grid_segment_t *segment = &state->segments[index];
        uint64_t first = apta_s6_get_u64(cursor);
        uint64_t end = apta_s6_get_u64(cursor + 8u);
        uint32_t tempo = apta_s6_get_u32(cursor + 56u);

        if (!apta_s6_range_valid(first, end) ||
            apta_s6_get_u64(cursor + 40u) == 0u ||
            tempo < APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
            tempo > APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
            !apta_s6_state_allowed(cursor[72], partial) ||
            cursor[73] > APTA_CONFIDENCE_MAX ||
            (strict && !apta_s6_all_zero(cursor + 28u, 4u)) ||
            (strict && !apta_s6_all_zero(cursor + 74u, 6u)) ||
            first < grid->applicability_range.first_frame ||
            end > grid->applicability_range.end_frame ||
            (index != 0u &&
             state->segments[index - 1u].applicability_range.end_frame >
                 first)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        memset(segment, 0, sizeof(*segment));
        segment->struct_size = (uint32_t)sizeof(*segment);
        segment->api_version = APTA_API_VERSION;
        apta_frame_range_init(&segment->applicability_range);
        segment->applicability_range.first_frame = first;
        segment->applicability_range.end_frame = end;
        segment->anchor_position.whole_frame = apta_s6_get_u64(cursor + 16u);
        segment->anchor_position.fraction_q32 = apta_s6_get_u32(cursor + 24u);
        segment->anchor_ordinal = (int64_t)apta_s6_get_u64(cursor + 32u);
        segment->frames_per_beat.whole_frames =
            apta_s6_get_u64(cursor + 40u);
        segment->frames_per_beat.fraction_q32 =
            apta_s6_get_u32(cursor + 48u);
        segment->beat_count = apta_s6_get_u32(cursor + 52u);
        segment->nominal_tempo_millibpm = tempo;
        segment->segment_id = apta_s6_get_u32(cursor + 60u);
        segment->revision = apta_s6_get_u32(cursor + 64u);
        segment->flags = apta_s6_get_u32(cursor + 68u);
        segment->state = cursor[72];
        segment->confidence = cursor[73];
        cursor += APTA_GGRD_SEGMENT_SIZE;
    }

    for (index = 0u; index < beat_count; ++index) {
        apta_beat_t *beat = &state->beats[index];
        const uint64_t whole = apta_s6_get_u64(cursor);
        const int64_t ordinal = (int64_t)apta_s6_get_u64(cursor + 16u);

        if ((strict && !apta_s6_all_zero(cursor + 12u, 4u)) ||
            (strict && !apta_s6_all_zero(cursor + 33u, 7u)) ||
            cursor[32] > APTA_CONFIDENCE_MAX ||
            whole < grid->applicability_range.first_frame ||
            whole >= grid->applicability_range.end_frame ||
            (index != 0u &&
             (state->beats[index - 1u].position.whole_frame > whole ||
              (state->beats[index - 1u].position.whole_frame == whole &&
               state->beats[index - 1u].position.fraction_q32 >
                   apta_s6_get_u32(cursor + 8u)) ||
              state->beats[index - 1u].ordinal >= ordinal))) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        memset(beat, 0, sizeof(*beat));
        beat->position.whole_frame = whole;
        beat->position.fraction_q32 = apta_s6_get_u32(cursor + 8u);
        beat->ordinal = ordinal;
        beat->revision = apta_s6_get_u32(cursor + 24u);
        beat->flags = apta_s6_get_u32(cursor + 28u);
        beat->confidence = cursor[32];
        cursor += APTA_GGRD_BEAT_SIZE;
    }

    result->info.available_features |= APTA_FEATURE_GLOBAL_BEATGRID;
    if ((grid->flags & APTA_GRID_FLAG_DYNAMIC_TEMPO) != 0u) {
        result->info.available_features |= APTA_FEATURE_DYNAMIC_TEMPO;
    }
    if (grid->confidence != APTA_CONFIDENCE_UNKNOWN) {
        result->info.available_features |= APTA_FEATURE_CONFIDENCE;
    }
    return APTA_STATUS_OK;
}

static apta_status_t apta_s6_parse_revision(
    const apta_parse_options_t *options,
    const uint8_t *payload,
    size_t size,
    int partial,
    apta_result_t *result)
{
    apta_grid_revision_view_t *revision;
    const apta_grid_view_t *grid;
    uint32_t index;
    const int strict = apta_s6_is_strict(options);
    const uint32_t allowed_flags =
        APTA_GRID_REVISION_FLAG_CONFLICTS_LOCKED_RANGE |
        APTA_GRID_REVISION_FLAG_DYNAMIC_TEMPO |
        APTA_GRID_REVISION_FLAG_DEGRADED;

    if (result == NULL || result->s6 == NULL ||
        size != APTA_REVN_PAYLOAD_SIZE || apta_s6_get_u16(payload) != 1u ||
        (payload[2] != APTA_GRID_REVISION_PENDING &&
         payload[2] != APTA_GRID_REVISION_APPLIED) ||
        (!partial && payload[2] != APTA_GRID_REVISION_APPLIED) ||
        payload[3] > APTA_CONFIDENCE_MAX ||
        (apta_s6_get_u32(payload + 4u) & ~allowed_flags) != 0u ||
        apta_s6_get_u32(payload + 8u) == 0u ||
        !apta_s6_representation_valid(apta_s6_get_u32(payload + 16u)) ||
        !apta_s6_range_valid(apta_s6_get_u64(payload + 32u),
                             apta_s6_get_u64(payload + 40u)) ||
        (strict && !apta_s6_all_zero(payload + 28u, 4u)) ||
        (strict && !apta_s6_all_zero(payload + 48u, 32u))) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    grid = &result->s6->global_grid;
    if ((payload[2] == APTA_GRID_REVISION_PENDING &&
         grid->state == APTA_FEATURE_FINAL) ||
        apta_s6_get_u32(payload + 16u) != grid->representation ||
        apta_s6_get_u32(payload + 20u) != grid->segment_count ||
        apta_s6_get_u32(payload + 24u) != grid->beat_count) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    for (index = 0u; index < grid->segment_count; ++index) {
        if (grid->segments[index].revision !=
            apta_s6_get_u32(payload + 8u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    }
    for (index = 0u; index < grid->beat_count; ++index) {
        if (grid->beats[index].revision !=
            apta_s6_get_u32(payload + 8u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    }

    revision = &result->s6->revision;
    apta_grid_revision_view_init(revision);
    revision->state = payload[2];
    revision->confidence = payload[3];
    revision->flags = apta_s6_get_u32(payload + 4u);
    revision->revision_id = apta_s6_get_u32(payload + 8u);
    revision->previous_revision_id = apta_s6_get_u32(payload + 12u);
    revision->proposed_representation = apta_s6_get_u32(payload + 16u);
    revision->proposed_segment_count = apta_s6_get_u32(payload + 20u);
    revision->proposed_beat_count = apta_s6_get_u32(payload + 24u);
    revision->affected_range.first_frame = apta_s6_get_u64(payload + 32u);
    revision->affected_range.end_frame = apta_s6_get_u64(payload + 40u);
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
    apta_s6_sections_t sections;
    apta_status_t status;
    int strict;
    int partial;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;
    if (context == NULL || buffer == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_result_parse_s4(
        context,
        options,
        buffer,
        buffer_size,
        &base_result);
    if (status < 0) {
        return status;
    }
    strict = apta_s6_is_strict(options);
    partial = (apta_s6_get_u32((const uint8_t *)buffer + 16u) &
               APTA_CONTAINER_FLAG_PARTIAL_RESULT) != 0u;
    status = apta_s6_find_sections(
        (const uint8_t *)buffer,
        buffer_size,
        strict,
        &sections);
    if (status < 0) {
        apta_result_release(base_result);
        return status;
    }
    if (sections.grid == NULL) {
        *result_out = base_result;
        return APTA_STATUS_OK;
    }

    result = (apta_result_t *)base_result;
    if ((result->info.available_features & APTA_FEATURE_BPM) == 0u) {
        apta_result_release(base_result);
        return APTA_ERROR_CORRUPT_DATA;
    }
    status = apta_s6_parse_grid(
        context,
        options,
        sections.grid,
        sections.grid_size,
        partial,
        result);
    if (status >= 0) {
        status = apta_s6_parse_revision(
            options,
            sections.revision,
            sections.revision_size,
            partial,
            result);
    }
    if (status < 0) {
        apta_result_release(base_result);
        return status;
    }

    *result_out = base_result;
    return APTA_STATUS_OK;
}
