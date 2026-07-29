// SPDX-License-Identifier: Apache-2.0
#include "../beatgrid/apta_s6_internal.h"

#include <limits.h>
#include <string.h>

#define APTA_S6_DIRECTORY_ENTRY_SIZE 40u
#define APTA_S6_DIRECTORY_OFFSET 96u
#define APTA_GGRD_HEADER_SIZE 96u
#define APTA_GGRD_SEGMENT_SIZE 80u
#define APTA_GGRD_BEAT_SIZE 40u
#define APTA_REVN_PAYLOAD_SIZE 80u

APTA_API apta_status_t APTA_CALL
apta_result_query_serialized_size_s4(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out);

APTA_API apta_status_t APTA_CALL
apta_result_serialize_s4(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written_out);

typedef struct {
    uint64_t base_size;
    uint64_t grid_size;
    uint64_t grid_offset;
    uint64_t revision_offset;
    uint64_t total_size;
    uint32_t has_global;
} apta_s6_write_layout_t;

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

static void apta_s6_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void apta_s6_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static void apta_s6_put_u64(uint8_t *p, uint64_t value)
{
    apta_s6_put_u32(p, (uint32_t)value);
    apta_s6_put_u32(p + 4u, (uint32_t)(value >> 32u));
}

static apta_status_t apta_s6_align8(uint64_t value, uint64_t *out)
{
    if (out == NULL || value > UINT64_MAX - 7u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    *out = (value + 7u) & ~UINT64_C(7);
    return APTA_STATUS_OK;
}

static int apta_s6_range_valid(const apta_frame_range_t *range)
{
    return range != NULL && range->first_frame < range->end_frame;
}

static int apta_s6_global_is_valid(const apta_result_t *result)
{
    const apta_internal_s6_result_state_t *state;
    const apta_grid_view_t *grid;
    uint32_t index;

    if (result == NULL || result->s6 == NULL ||
        (result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) == 0u ||
        (result->info.available_features & APTA_FEATURE_BPM) == 0u) {
        return 0;
    }
    state = result->s6;
    grid = &state->global_grid;
    if (grid->representation < APTA_GRID_REPRESENTATION_SEGMENTS ||
        grid->representation > APTA_GRID_REPRESENTATION_HYBRID ||
        grid->state < APTA_FEATURE_PROVISIONAL ||
        grid->state > APTA_FEATURE_FINAL ||
        grid->confidence > APTA_CONFIDENCE_MAX ||
        grid->coverage_range_count != 1u || grid->coverage_ranges == NULL ||
        grid->segment_count == 0u ||
        grid->segment_count > APTA_REFERENCE_GLOBAL_GRID_MAX_SEGMENTS ||
        grid->segments == NULL ||
        grid->beat_count > APTA_REFERENCE_GLOBAL_GRID_MAX_BEATS ||
        (grid->beat_count != 0u && grid->beats == NULL) ||
        (grid->representation == APTA_GRID_REPRESENTATION_SEGMENTS &&
         grid->beat_count != 0u) ||
        (grid->representation != APTA_GRID_REPRESENTATION_SEGMENTS &&
         grid->beat_count == 0u) ||
        !apta_s6_range_valid(&grid->requested_range) ||
        !apta_s6_range_valid(&grid->evidence_range) ||
        !apta_s6_range_valid(&grid->applicability_range) ||
        !apta_s6_range_valid(&grid->coverage_ranges[0]) ||
        state->revision.revision_id == 0u ||
        (state->revision.state != APTA_GRID_REVISION_PENDING &&
         state->revision.state != APTA_GRID_REVISION_APPLIED) ||
        state->revision.confidence > APTA_CONFIDENCE_MAX ||
        !apta_s6_range_valid(&state->revision.affected_range) ||
        state->revision.proposed_representation != grid->representation ||
        state->revision.proposed_segment_count != grid->segment_count ||
        state->revision.proposed_beat_count != grid->beat_count) {
        return 0;
    }

    for (index = 0u; index < grid->segment_count; ++index) {
        const apta_grid_segment_t *segment = &grid->segments[index];
        if (!apta_s6_range_valid(&segment->applicability_range) ||
            segment->frames_per_beat.whole_frames == 0u ||
            segment->nominal_tempo_millibpm <
                APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
            segment->nominal_tempo_millibpm >
                APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
            segment->state < APTA_FEATURE_PROVISIONAL ||
            segment->state > APTA_FEATURE_FINAL ||
            segment->confidence > APTA_CONFIDENCE_MAX ||
            segment->revision != state->revision.revision_id ||
            (index != 0u &&
             grid->segments[index - 1u].applicability_range.end_frame >
                 segment->applicability_range.first_frame)) {
            return 0;
        }
    }
    for (index = 0u; index < grid->beat_count; ++index) {
        const apta_beat_t *beat = &grid->beats[index];
        if (beat->confidence > APTA_CONFIDENCE_MAX ||
            beat->revision != state->revision.revision_id ||
            (index != 0u &&
             (grid->beats[index - 1u].position.whole_frame >
                  beat->position.whole_frame ||
              grid->beats[index - 1u].ordinal >= beat->ordinal))) {
            return 0;
        }
    }
    return 1;
}

static apta_status_t apta_s6_calculate_layout(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    apta_s6_write_layout_t *layout)
{
    uint64_t cursor;
    uint64_t segment_bytes;
    uint64_t beat_bytes;
    apta_status_t status;

    if (result == NULL || layout == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    memset(layout, 0, sizeof(*layout));
    status = apta_result_query_serialized_size_s4(
        result,
        options,
        &layout->base_size);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) {
        return status;
    }
    if ((result->info.available_features & APTA_FEATURE_GLOBAL_BEATGRID) == 0u) {
        layout->total_size = layout->base_size;
        return APTA_STATUS_OK;
    }
    if (!apta_s6_global_is_valid(result)) {
        return APTA_ERROR_INVALID_STATE;
    }

    layout->has_global = 1u;
    segment_bytes = (uint64_t)result->s6->global_grid.segment_count *
                    APTA_GGRD_SEGMENT_SIZE;
    beat_bytes = (uint64_t)result->s6->global_grid.beat_count *
                 APTA_GGRD_BEAT_SIZE;
    if (segment_bytes > UINT64_MAX - APTA_GGRD_HEADER_SIZE ||
        beat_bytes > UINT64_MAX - APTA_GGRD_HEADER_SIZE - segment_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    layout->grid_size = APTA_GGRD_HEADER_SIZE + segment_bytes + beat_bytes;
    if (layout->base_size > UINT64_MAX - 2u * APTA_S6_DIRECTORY_ENTRY_SIZE) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    status = apta_s6_align8(
        layout->base_size + 2u * APTA_S6_DIRECTORY_ENTRY_SIZE,
        &cursor);
    if (status < 0) {
        return status;
    }
    layout->grid_offset = cursor;
    if (cursor > UINT64_MAX - layout->grid_size) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    cursor += layout->grid_size;
    status = apta_s6_align8(cursor, &layout->revision_offset);
    if (status < 0 || layout->revision_offset >
                        UINT64_MAX - APTA_REVN_PAYLOAD_SIZE) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    layout->total_size = layout->revision_offset + APTA_REVN_PAYLOAD_SIZE;
    if (options != NULL && options->maximum_output_bytes != 0u &&
        layout->total_size > options->maximum_output_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    return APTA_STATUS_OK;
}

static void apta_s6_write_grid(
    uint8_t *payload,
    const apta_grid_view_t *grid)
{
    uint32_t index;
    uint8_t *cursor;

    memset(payload, 0,
           (size_t)(APTA_GGRD_HEADER_SIZE +
                    (uint64_t)grid->segment_count * APTA_GGRD_SEGMENT_SIZE +
                    (uint64_t)grid->beat_count * APTA_GGRD_BEAT_SIZE));
    apta_s6_put_u16(payload, 1u);
    payload[2] = (uint8_t)grid->state;
    payload[3] = grid->confidence;
    apta_s6_put_u32(payload + 4u, grid->flags);
    apta_s6_put_u32(payload + 8u, grid->representation);
    apta_s6_put_u32(payload + 12u, 1u);
    apta_s6_put_u32(payload + 16u, grid->segment_count);
    apta_s6_put_u32(payload + 20u, grid->beat_count);
    apta_s6_put_u64(payload + 24u, grid->requested_range.first_frame);
    apta_s6_put_u64(payload + 32u, grid->requested_range.end_frame);
    apta_s6_put_u64(payload + 40u, grid->evidence_range.first_frame);
    apta_s6_put_u64(payload + 48u, grid->evidence_range.end_frame);
    apta_s6_put_u64(payload + 56u, grid->applicability_range.first_frame);
    apta_s6_put_u64(payload + 64u, grid->applicability_range.end_frame);
    apta_s6_put_u64(payload + 72u, grid->coverage_ranges[0].first_frame);
    apta_s6_put_u64(payload + 80u, grid->coverage_ranges[0].end_frame);

    cursor = payload + APTA_GGRD_HEADER_SIZE;
    for (index = 0u; index < grid->segment_count; ++index) {
        const apta_grid_segment_t *segment = &grid->segments[index];
        apta_s6_put_u64(cursor, segment->applicability_range.first_frame);
        apta_s6_put_u64(cursor + 8u, segment->applicability_range.end_frame);
        apta_s6_put_u64(cursor + 16u, segment->anchor_position.whole_frame);
        apta_s6_put_u32(cursor + 24u, segment->anchor_position.fraction_q32);
        apta_s6_put_u64(cursor + 32u, (uint64_t)segment->anchor_ordinal);
        apta_s6_put_u64(cursor + 40u, segment->frames_per_beat.whole_frames);
        apta_s6_put_u32(cursor + 48u, segment->frames_per_beat.fraction_q32);
        apta_s6_put_u32(cursor + 52u, segment->beat_count);
        apta_s6_put_u32(cursor + 56u, segment->nominal_tempo_millibpm);
        apta_s6_put_u32(cursor + 60u, segment->segment_id);
        apta_s6_put_u32(cursor + 64u, segment->revision);
        apta_s6_put_u32(cursor + 68u, segment->flags);
        cursor[72] = (uint8_t)segment->state;
        cursor[73] = segment->confidence;
        cursor += APTA_GGRD_SEGMENT_SIZE;
    }
    for (index = 0u; index < grid->beat_count; ++index) {
        const apta_beat_t *beat = &grid->beats[index];
        apta_s6_put_u64(cursor, beat->position.whole_frame);
        apta_s6_put_u32(cursor + 8u, beat->position.fraction_q32);
        apta_s6_put_u64(cursor + 16u, (uint64_t)beat->ordinal);
        apta_s6_put_u32(cursor + 24u, beat->revision);
        apta_s6_put_u32(cursor + 28u, beat->flags);
        cursor[32] = beat->confidence;
        cursor += APTA_GGRD_BEAT_SIZE;
    }
}

static void apta_s6_write_revision(
    uint8_t *payload,
    const apta_grid_revision_view_t *revision)
{
    memset(payload, 0, APTA_REVN_PAYLOAD_SIZE);
    apta_s6_put_u16(payload, 1u);
    payload[2] = (uint8_t)revision->state;
    payload[3] = revision->confidence;
    apta_s6_put_u32(payload + 4u, revision->flags);
    apta_s6_put_u32(payload + 8u, revision->revision_id);
    apta_s6_put_u32(payload + 12u, revision->previous_revision_id);
    apta_s6_put_u32(payload + 16u, revision->proposed_representation);
    apta_s6_put_u32(payload + 20u, revision->proposed_segment_count);
    apta_s6_put_u32(payload + 24u, revision->proposed_beat_count);
    apta_s6_put_u64(payload + 32u, revision->affected_range.first_frame);
    apta_s6_put_u64(payload + 40u, revision->affected_range.end_frame);
}

static void apta_s6_write_directory(
    uint8_t *entry,
    const char id[4],
    uint64_t offset,
    uint64_t size,
    const uint8_t *payload)
{
    memset(entry, 0, APTA_S6_DIRECTORY_ENTRY_SIZE);
    memcpy(entry, id, 4u);
    apta_s6_put_u16(entry + 4u, 1u);
    apta_s6_put_u64(entry + 8u, offset);
    apta_s6_put_u64(entry + 16u, size);
    apta_s6_put_u64(entry + 24u, size);
    apta_s6_put_u32(
        entry + 32u,
        apta_internal_crc32c(payload, (size_t)size));
}

apta_status_t APTA_CALL apta_result_query_serialized_size(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out)
{
    apta_s6_write_layout_t layout;
    apta_status_t status;

    if (size_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *size_out = 0u;
    status = apta_s6_calculate_layout(result, options, &layout);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) {
        return status;
    }
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
    apta_s6_write_layout_t layout;
    uint8_t *bytes;
    uint8_t *directory;
    uint64_t first_payload = UINT64_MAX;
    uint32_t old_count;
    uint32_t index;
    size_t base_written;
    apta_status_t status;

    if (bytes_written_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *bytes_written_out = 0u;
    status = apta_s6_calculate_layout(result, options, &layout);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) {
        return status;
    }
    if (layout.total_size > SIZE_MAX || buffer == NULL) {
        return layout.total_size > SIZE_MAX
                   ? APTA_ERROR_LIMIT_EXCEEDED
                   : APTA_ERROR_INVALID_ARGUMENT;
    }
    if (buffer_size < (size_t)layout.total_size) {
        return APTA_ERROR_BUFFER_TOO_SMALL;
    }

    status = apta_result_serialize_s4(
        result,
        options,
        buffer,
        buffer_size,
        &base_written);
    if (status < 0) {
        return status;
    }
    if (!layout.has_global) {
        *bytes_written_out = base_written;
        return APTA_STATUS_OK;
    }
    if (base_written != (size_t)layout.base_size) {
        return APTA_ERROR_INTERNAL;
    }

    bytes = (uint8_t *)buffer;
    old_count = apta_s6_get_u32(bytes + 20u);
    if (old_count == 0u ||
        apta_s6_get_u64(bytes + 24u) != APTA_S6_DIRECTORY_OFFSET ||
        old_count > UINT32_MAX - 2u) {
        return APTA_ERROR_INTERNAL;
    }
    directory = bytes + APTA_S6_DIRECTORY_OFFSET;
    for (index = 0u; index < old_count; ++index) {
        uint8_t *entry = directory +
                         (size_t)index * APTA_S6_DIRECTORY_ENTRY_SIZE;
        const uint64_t offset = apta_s6_get_u64(entry + 8u);
        if (offset < first_payload) {
            first_payload = offset;
        }
    }
    if (first_payload == UINT64_MAX || first_payload > layout.base_size) {
        return APTA_ERROR_INTERNAL;
    }

    memmove(
        bytes + (size_t)(first_payload + 2u * APTA_S6_DIRECTORY_ENTRY_SIZE),
        bytes + (size_t)first_payload,
        (size_t)(layout.base_size - first_payload));
    for (index = 0u; index < old_count; ++index) {
        uint8_t *entry = directory +
                         (size_t)index * APTA_S6_DIRECTORY_ENTRY_SIZE;
        apta_s6_put_u64(
            entry + 8u,
            apta_s6_get_u64(entry + 8u) +
                2u * APTA_S6_DIRECTORY_ENTRY_SIZE);
    }

    if (layout.grid_offset >
        layout.base_size + 2u * APTA_S6_DIRECTORY_ENTRY_SIZE) {
        memset(
            bytes + (size_t)(layout.base_size +
                             2u * APTA_S6_DIRECTORY_ENTRY_SIZE),
            0,
            (size_t)(layout.grid_offset -
                     (layout.base_size +
                      2u * APTA_S6_DIRECTORY_ENTRY_SIZE)));
    }
    apta_s6_write_grid(
        bytes + (size_t)layout.grid_offset,
        &result->s6->global_grid);
    if (layout.revision_offset > layout.grid_offset + layout.grid_size) {
        memset(
            bytes + (size_t)(layout.grid_offset + layout.grid_size),
            0,
            (size_t)(layout.revision_offset -
                     (layout.grid_offset + layout.grid_size)));
    }
    apta_s6_write_revision(
        bytes + (size_t)layout.revision_offset,
        &result->s6->revision);

    apta_s6_write_directory(
        directory + (size_t)old_count * APTA_S6_DIRECTORY_ENTRY_SIZE,
        "GGRD",
        layout.grid_offset,
        layout.grid_size,
        bytes + (size_t)layout.grid_offset);
    apta_s6_write_directory(
        directory + (size_t)(old_count + 1u) * APTA_S6_DIRECTORY_ENTRY_SIZE,
        "REVN",
        layout.revision_offset,
        APTA_REVN_PAYLOAD_SIZE,
        bytes + (size_t)layout.revision_offset);

    apta_s6_put_u32(bytes + 20u, old_count + 2u);
    apta_s6_put_u64(bytes + 32u, layout.total_size);
    apta_s6_put_u32(bytes + 92u, apta_internal_crc32c(bytes, 92u));
    *bytes_written_out = (size_t)layout.total_size;
    (void)apta_s6_get_u16;
    return APTA_STATUS_OK;
}
