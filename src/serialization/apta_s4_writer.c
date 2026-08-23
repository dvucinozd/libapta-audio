// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <limits.h>
#include <string.h>

#define APTA_S4_DIRECTORY_ENTRY_SIZE 40u
#define APTA_S4_DIRECTORY_OFFSET 96u
#define APTA_TEMP_HEADER_SIZE 56u
#define APTA_TEMP_CANDIDATE_SIZE 16u
#define APTA_LGRD_PAYLOAD_SIZE 144u

APTA_API apta_status_t APTA_CALL
apta_result_query_serialized_size_meta(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out);

APTA_API apta_status_t APTA_CALL
apta_result_serialize_meta(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written_out);

typedef struct {
    uint64_t base_size;
    uint64_t temp_size;
    uint64_t temp_offset;
    uint64_t grid_size;
    uint64_t grid_offset;
    uint64_t total_size;
    uint32_t has_temp;
    uint32_t has_grid;
    uint32_t entry_count;
} apta_s4_write_layout_t;

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

static void apta_s4_put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void apta_s4_put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static void apta_s4_put_u64(uint8_t *p, uint64_t value)
{
    apta_s4_put_u32(p, (uint32_t)value);
    apta_s4_put_u32(p + 4u, (uint32_t)(value >> 32u));
}

static apta_status_t apta_s4_align8(uint64_t value, uint64_t *out)
{
    if (out == NULL || value > UINT64_MAX - 7u) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    *out = (value + 7u) & ~UINT64_C(7);
    return APTA_STATUS_OK;
}

static int apta_s4_tempo_is_valid(const apta_result_t *result)
{
    const apta_tempo_view_t *tempo = &result->tempo;
    uint32_t index;

    if ((result->info.available_features & APTA_FEATURE_BPM) == 0u) {
        return 0;
    }
    if (tempo->candidate_count == 0u ||
        tempo->candidate_count > APTA_INTERNAL_MAX_TEMPO_CANDIDATES ||
        tempo->candidates == NULL ||
        tempo->selected.tempo_millibpm < APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
        tempo->selected.tempo_millibpm > APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
        tempo->selected.confidence > APTA_CONFIDENCE_MAX ||
        tempo->selected.state < APTA_FEATURE_PROVISIONAL ||
        tempo->selected.state > APTA_FEATURE_FINAL ||
        tempo->selected.evidence_range.first_frame >=
            tempo->selected.evidence_range.end_frame ||
        tempo->selected.applicability_range.first_frame >=
            tempo->selected.applicability_range.end_frame) {
        return 0;
    }
    for (index = 0u; index < tempo->candidate_count; ++index) {
        if (tempo->candidates[index].tempo_millibpm <
                APTA_REFERENCE_TEMPO_MIN_MILLIBPM ||
            tempo->candidates[index].tempo_millibpm >
                APTA_REFERENCE_TEMPO_MAX_MILLIBPM ||
            tempo->candidates[index].confidence > APTA_CONFIDENCE_MAX ||
            tempo->candidates[index].relation_to_selected >
                APTA_TEMPO_RELATION_QUADRUPLE ||
            (index != 0u && tempo->candidates[index].score >
                                tempo->candidates[index - 1u].score)) {
            return 0;
        }
    }
    return 1;
}

static int apta_s4_grid_is_valid(const apta_result_t *result)
{
    const apta_grid_view_t *grid = &result->local_grid;
    const apta_grid_segment_t *segment;

    if ((result->info.available_features & APTA_FEATURE_LOCAL_BEATGRID) == 0u) {
        return 0;
    }
    if (grid->representation != APTA_GRID_REPRESENTATION_SEGMENTS ||
        grid->coverage_range_count != 1u || grid->coverage_ranges == NULL ||
        grid->segment_count != 1u || grid->segments == NULL ||
        grid->confidence > APTA_CONFIDENCE_MAX ||
        grid->state < APTA_FEATURE_PROVISIONAL ||
        grid->state > APTA_FEATURE_FINAL ||
        grid->requested_range.first_frame >= grid->requested_range.end_frame ||
        grid->evidence_range.first_frame >= grid->evidence_range.end_frame ||
        grid->applicability_range.first_frame >=
            grid->applicability_range.end_frame ||
        grid->coverage_ranges[0].first_frame >=
            grid->coverage_ranges[0].end_frame) {
        return 0;
    }
    segment = &grid->segments[0];
    return segment->frames_per_beat.whole_frames != 0u &&
           segment->nominal_tempo_millibpm >=
               APTA_REFERENCE_TEMPO_MIN_MILLIBPM &&
           segment->nominal_tempo_millibpm <=
               APTA_REFERENCE_TEMPO_MAX_MILLIBPM &&
           segment->confidence <= APTA_CONFIDENCE_MAX &&
           segment->state >= APTA_FEATURE_PROVISIONAL &&
           segment->state <= APTA_FEATURE_FINAL;
}

static apta_status_t apta_s4_calculate_layout(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    apta_s4_write_layout_t *layout)
{
    uint64_t cursor;
    uint64_t entry_bytes;
    apta_status_t status;

    if (result == NULL || layout == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    memset(layout, 0, sizeof(*layout));
    status = apta_result_query_serialized_size_meta(
        result,
        options,
        &layout->base_size);
    if (status < 0 || status == APTA_STATUS_NOT_AVAILABLE) {
        return status;
    }

    layout->has_temp = apta_s4_tempo_is_valid(result) ? 1u : 0u;
    layout->has_grid = apta_s4_grid_is_valid(result) ? 1u : 0u;
    layout->entry_count = layout->has_temp + layout->has_grid;
    if (layout->entry_count == 0u) {
        layout->total_size = layout->base_size;
        return APTA_STATUS_OK;
    }
    if (layout->has_grid && !layout->has_temp) {
        return APTA_ERROR_INTERNAL;
    }

    layout->temp_size = layout->has_temp
                            ? APTA_TEMP_HEADER_SIZE +
                                  (uint64_t)result->tempo.candidate_count *
                                      APTA_TEMP_CANDIDATE_SIZE
                            : 0u;
    layout->grid_size = layout->has_grid ? APTA_LGRD_PAYLOAD_SIZE : 0u;
    entry_bytes = (uint64_t)layout->entry_count *
                  APTA_S4_DIRECTORY_ENTRY_SIZE;
    if (layout->base_size > UINT64_MAX - entry_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    status = apta_s4_align8(layout->base_size + entry_bytes, &cursor);
    if (status < 0) {
        return status;
    }
    if (layout->has_temp) {
        layout->temp_offset = cursor;
        if (cursor > UINT64_MAX - layout->temp_size) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        cursor += layout->temp_size;
    }
    if (layout->has_grid) {
        status = apta_s4_align8(cursor, &cursor);
        if (status < 0 || cursor > UINT64_MAX - layout->grid_size) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        layout->grid_offset = cursor;
        cursor += layout->grid_size;
    }
    layout->total_size = cursor;
    if (options != NULL && options->maximum_output_bytes != 0u &&
        layout->total_size > options->maximum_output_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    return APTA_STATUS_OK;
}

static void apta_s4_write_temp(
    uint8_t *payload,
    const apta_tempo_view_t *tempo)
{
    uint32_t index;

    memset(payload, 0, (size_t)(APTA_TEMP_HEADER_SIZE +
           (uint64_t)tempo->candidate_count * APTA_TEMP_CANDIDATE_SIZE));
    apta_s4_put_u16(payload, 1u);
    payload[2] = (uint8_t)tempo->selected.state;
    payload[3] = tempo->selected.confidence;
    apta_s4_put_u32(payload + 4u, tempo->selected.flags);
    apta_s4_put_u32(payload + 8u, tempo->selected.tempo_millibpm);
    apta_s4_put_u32(payload + 12u, tempo->selected.candidate_set_id);
    apta_s4_put_u64(payload + 16u, tempo->selected.evidence_range.first_frame);
    apta_s4_put_u64(payload + 24u, tempo->selected.evidence_range.end_frame);
    apta_s4_put_u64(
        payload + 32u,
        tempo->selected.applicability_range.first_frame);
    apta_s4_put_u64(
        payload + 40u,
        tempo->selected.applicability_range.end_frame);
    apta_s4_put_u32(payload + 48u, tempo->candidate_count);

    for (index = 0u; index < tempo->candidate_count; ++index) {
        const apta_tempo_candidate_t *candidate = &tempo->candidates[index];
        uint8_t *entry = payload + APTA_TEMP_HEADER_SIZE +
                         (size_t)index * APTA_TEMP_CANDIDATE_SIZE;
        apta_s4_put_u32(entry, candidate->tempo_millibpm);
        apta_s4_put_u16(entry + 4u, candidate->score);
        entry[6] = candidate->confidence;
        entry[7] = (uint8_t)candidate->relation_to_selected;
        apta_s4_put_u32(entry + 8u, candidate->flags);
    }
}

static void apta_s4_write_grid(
    uint8_t *payload,
    const apta_grid_view_t *grid)
{
    const apta_grid_segment_t *segment = &grid->segments[0];

    memset(payload, 0, APTA_LGRD_PAYLOAD_SIZE);
    apta_s4_put_u16(payload, 1u);
    payload[2] = (uint8_t)grid->state;
    payload[3] = grid->confidence;
    apta_s4_put_u32(payload + 4u, grid->flags);
    apta_s4_put_u32(payload + 8u, grid->representation);
    apta_s4_put_u32(payload + 12u, 1u);
    apta_s4_put_u64(payload + 16u, grid->requested_range.first_frame);
    apta_s4_put_u64(payload + 24u, grid->requested_range.end_frame);
    apta_s4_put_u64(payload + 32u, grid->evidence_range.first_frame);
    apta_s4_put_u64(payload + 40u, grid->evidence_range.end_frame);
    apta_s4_put_u64(payload + 48u, grid->applicability_range.first_frame);
    apta_s4_put_u64(payload + 56u, grid->applicability_range.end_frame);
    apta_s4_put_u64(payload + 64u, grid->coverage_ranges[0].first_frame);
    apta_s4_put_u64(payload + 72u, grid->coverage_ranges[0].end_frame);
    apta_s4_put_u64(payload + 80u, segment->anchor_position.whole_frame);
    apta_s4_put_u32(payload + 88u, segment->anchor_position.fraction_q32);
    apta_s4_put_u64(payload + 96u, (uint64_t)segment->anchor_ordinal);
    apta_s4_put_u64(payload + 104u, segment->frames_per_beat.whole_frames);
    apta_s4_put_u32(payload + 112u, segment->frames_per_beat.fraction_q32);
    apta_s4_put_u32(payload + 116u, segment->beat_count);
    apta_s4_put_u32(payload + 120u, segment->nominal_tempo_millibpm);
    apta_s4_put_u32(payload + 124u, segment->segment_id);
    apta_s4_put_u32(payload + 128u, segment->revision);
    apta_s4_put_u32(payload + 132u, segment->flags);
    payload[136] = (uint8_t)segment->state;
    payload[137] = segment->confidence;
}

static void apta_s4_write_directory(
    uint8_t *entry,
    const char id[4],
    uint64_t offset,
    uint64_t size,
    const uint8_t *payload)
{
    memset(entry, 0, APTA_S4_DIRECTORY_ENTRY_SIZE);
    memcpy(entry, id, 4u);
    apta_s4_put_u16(entry + 4u, 1u);
    apta_s4_put_u64(entry + 8u, offset);
    apta_s4_put_u64(entry + 16u, size);
    apta_s4_put_u64(entry + 24u, size);
    apta_s4_put_u32(
        entry + 32u,
        apta_internal_crc32c(payload, (size_t)size));
}

apta_status_t APTA_CALL apta_result_query_serialized_size(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out)
{
    apta_s4_write_layout_t layout;
    apta_status_t status;

    if (size_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *size_out = 0u;
    status = apta_s4_calculate_layout(result, options, &layout);
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
    apta_s4_write_layout_t layout;
    uint8_t *bytes;
    uint8_t *directory;
    uint64_t first_payload = UINT64_MAX;
    uint64_t entry_bytes;
    uint32_t old_count;
    uint32_t index;
    uint32_t new_index;
    size_t base_written;
    apta_status_t status;

    if (bytes_written_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *bytes_written_out = 0u;
    status = apta_s4_calculate_layout(result, options, &layout);
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

    status = apta_result_serialize_meta(
        result,
        options,
        buffer,
        buffer_size,
        &base_written);
    if (status < 0) {
        return status;
    }
    if (layout.entry_count == 0u) {
        *bytes_written_out = base_written;
        return APTA_STATUS_OK;
    }
    if (base_written != (size_t)layout.base_size) {
        return APTA_ERROR_INTERNAL;
    }

    bytes = (uint8_t *)buffer;
    old_count = apta_s4_get_u32(bytes + 20u);
    if (old_count == 0u ||
        apta_s4_get_u64(bytes + 24u) != APTA_S4_DIRECTORY_OFFSET ||
        old_count > UINT32_MAX - layout.entry_count) {
        return APTA_ERROR_INTERNAL;
    }
    directory = bytes + APTA_S4_DIRECTORY_OFFSET;
    for (index = 0u; index < old_count; ++index) {
        uint8_t *entry = directory +
                         (size_t)index * APTA_S4_DIRECTORY_ENTRY_SIZE;
        uint64_t offset = apta_s4_get_u64(entry + 8u);
        if (offset < first_payload) {
            first_payload = offset;
        }
    }
    if (first_payload == UINT64_MAX || first_payload > layout.base_size) {
        return APTA_ERROR_INTERNAL;
    }

    entry_bytes = (uint64_t)layout.entry_count *
                  APTA_S4_DIRECTORY_ENTRY_SIZE;
    memmove(
        bytes + (size_t)(first_payload + entry_bytes),
        bytes + (size_t)first_payload,
        (size_t)(layout.base_size - first_payload));
    for (index = 0u; index < old_count; ++index) {
        uint8_t *entry = directory +
                         (size_t)index * APTA_S4_DIRECTORY_ENTRY_SIZE;
        apta_s4_put_u64(
            entry + 8u,
            apta_s4_get_u64(entry + 8u) + entry_bytes);
    }

    if (layout.temp_offset > layout.base_size + entry_bytes) {
        memset(
            bytes + (size_t)(layout.base_size + entry_bytes),
            0,
            (size_t)(layout.temp_offset -
                     (layout.base_size + entry_bytes)));
    }
    if (layout.has_temp) {
        apta_s4_write_temp(bytes + (size_t)layout.temp_offset, &result->tempo);
    }
    if (layout.has_grid) {
        uint64_t temp_end = layout.temp_offset + layout.temp_size;
        if (layout.grid_offset > temp_end) {
            memset(
                bytes + (size_t)temp_end,
                0,
                (size_t)(layout.grid_offset - temp_end));
        }
        apta_s4_write_grid(
            bytes + (size_t)layout.grid_offset,
            &result->local_grid);
    }

    new_index = old_count;
    if (layout.has_temp) {
        apta_s4_write_directory(
            directory + (size_t)new_index * APTA_S4_DIRECTORY_ENTRY_SIZE,
            "TEMP",
            layout.temp_offset,
            layout.temp_size,
            bytes + (size_t)layout.temp_offset);
        new_index += 1u;
    }
    if (layout.has_grid) {
        apta_s4_write_directory(
            directory + (size_t)new_index * APTA_S4_DIRECTORY_ENTRY_SIZE,
            "LGRD",
            layout.grid_offset,
            layout.grid_size,
            bytes + (size_t)layout.grid_offset);
        new_index += 1u;
    }

    apta_s4_put_u32(bytes + 20u, old_count + layout.entry_count);
    apta_s4_put_u64(bytes + 32u, layout.total_size);
    apta_s4_put_u32(bytes + 92u, apta_internal_crc32c(bytes, 92u));
    *bytes_written_out = (size_t)layout.total_size;
    (void)apta_s4_get_u16;
    return APTA_STATUS_OK;
}
