// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

#define APTA_DIRECTORY_ENTRY_SIZE 40u
#define APTA_WOVR_SPAN_SIZE 32u
#define APTA_WOVR_STATE_MASK 0x7u

typedef struct {
    uint64_t first_column;
    uint64_t end_column;
} apta_packed_interval_t;

APTA_API apta_status_t APTA_CALL apta_result_parse_base(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out);

static uint32_t apta_harden_get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8u) |
           ((uint32_t)source[2] << 16u) |
           ((uint32_t)source[3] << 24u);
}

static uint64_t apta_harden_get_u64(const uint8_t *source)
{
    return (uint64_t)apta_harden_get_u32(source) |
           ((uint64_t)apta_harden_get_u32(source + 4u) << 32u);
}

static int apta_compare_packed_intervals(const void *left, const void *right)
{
    const apta_packed_interval_t *left_interval =
        (const apta_packed_interval_t *)left;
    const apta_packed_interval_t *right_interval =
        (const apta_packed_interval_t *)right;

    if (left_interval->first_column < right_interval->first_column) {
        return -1;
    }
    if (left_interval->first_column > right_interval->first_column) {
        return 1;
    }
    if (left_interval->end_column < right_interval->end_column) {
        return -1;
    }
    if (left_interval->end_column > right_interval->end_column) {
        return 1;
    }
    return 0;
}

static const uint8_t *apta_find_wovr_payload(const uint8_t *bytes)
{
    uint32_t section_count = apta_harden_get_u32(bytes + 20u);
    uint64_t directory_offset = apta_harden_get_u64(bytes + 24u);
    uint32_t index;

    for (index = 0u; index < section_count; ++index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)index * APTA_DIRECTORY_ENTRY_SIZE;
        if (entry[0] == 'W' && entry[1] == 'O' &&
            entry[2] == 'V' && entry[3] == 'R') {
            return bytes + (size_t)apta_harden_get_u64(entry + 8u);
        }
    }

    return NULL;
}

static apta_status_t apta_harden_validated_wovr(
    apta_context_t *context,
    const uint8_t *bytes)
{
    const uint8_t *payload = apta_find_wovr_payload(bytes);
    uint64_t total_source_frames = apta_harden_get_u64(bytes + 40u);
    uint64_t origin_frame;
    uint64_t span_directory_offset;
    uint32_t logical_column_count;
    uint32_t span_count;
    apta_feature_state_t state;
    apta_packed_interval_t *intervals;
    uint64_t previous_end_frame;
    uint64_t previous_end_column;
    uint32_t span_index;
    apta_status_t status;

    if (payload == NULL) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    origin_frame = apta_harden_get_u64(payload + 8u);
    logical_column_count = apta_harden_get_u32(payload + 16u);
    span_count = apta_harden_get_u32(payload + 20u);
    span_directory_offset = apta_harden_get_u64(payload + 24u);
    state = apta_harden_get_u32(payload + 40u) & APTA_WOVR_STATE_MASK;

    if ((size_t)span_count > SIZE_MAX / sizeof(*intervals)) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    intervals = (apta_packed_interval_t *)apta_internal_context_allocate(
        context,
        (size_t)span_count * sizeof(*intervals),
        alignof(apta_packed_interval_t),
        APTA_MEMORY_TEMPORARY);
    if (intervals == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    status = APTA_STATUS_OK;
    previous_end_frame = origin_frame;
    previous_end_column = 0u;

    for (span_index = 0u; span_index < span_count; ++span_index) {
        const uint8_t *span = payload + (size_t)span_directory_offset +
                              (size_t)span_index * APTA_WOVR_SPAN_SIZE;
        uint64_t first_frame = apta_harden_get_u64(span + 0u);
        uint64_t end_frame = apta_harden_get_u64(span + 8u);
        uint64_t first_column = apta_harden_get_u32(span + 16u);
        uint64_t column_count = apta_harden_get_u32(span + 20u);
        uint64_t data_column_offset = apta_harden_get_u32(span + 24u);

        if (state == APTA_FEATURE_FINAL &&
            (first_frame != previous_end_frame ||
             first_column != previous_end_column)) {
            status = APTA_ERROR_CORRUPT_DATA;
            break;
        }

        intervals[span_index].first_column = data_column_offset;
        intervals[span_index].end_column = data_column_offset + column_count;
        previous_end_frame = end_frame;
        previous_end_column = first_column + column_count;
    }

    if (status == APTA_STATUS_OK && state == APTA_FEATURE_FINAL &&
        (previous_end_column != logical_column_count ||
         previous_end_frame != total_source_frames)) {
        status = APTA_ERROR_CORRUPT_DATA;
    }

    if (status == APTA_STATUS_OK) {
        qsort(
            intervals,
            span_count,
            sizeof(*intervals),
            apta_compare_packed_intervals);

        for (span_index = 1u; span_index < span_count; ++span_index) {
            if (intervals[span_index].first_column <
                intervals[span_index - 1u].end_column) {
                status = APTA_ERROR_CORRUPT_DATA;
                break;
            }
        }
    }

    apta_internal_context_deallocate(context, intervals);
    return status;
}

apta_status_t APTA_CALL apta_result_parse(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out)
{
    const apta_result_t *parsed = NULL;
    apta_status_t status;

    status = apta_result_parse_base(
        context,
        options,
        buffer,
        buffer_size,
        &parsed);
    if (status < 0) {
        return status;
    }

    status = apta_harden_validated_wovr(
        context,
        (const uint8_t *)buffer);
    if (status < 0) {
        apta_result_release(parsed);
        if (result_out != NULL) {
            *result_out = NULL;
        }
        return status;
    }

    *result_out = parsed;
    return APTA_STATUS_OK;
}
