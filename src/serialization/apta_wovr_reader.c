// SPDX-License-Identifier: Apache-2.0
#include "../core/apta_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#define APTA_CONTAINER_HEADER_SIZE 96u
#define APTA_DIRECTORY_ENTRY_SIZE 40u
#define APTA_WOVR_HEADER_SIZE 48u
#define APTA_WOVR_SPAN_SIZE 32u
#define APTA_PACKED_COLUMN_SIZE 10u

#define APTA_CONTAINER_FLAG_PARTIAL_RESULT          (1u << 0)
#define APTA_CONTAINER_FLAG_SOURCE_DURATION_UNKNOWN (1u << 1)
#define APTA_CONTAINER_FLAG_HAS_REQUIRED_EXTENSIONS (1u << 2)
#define APTA_CONTAINER_FLAG_MASK                    0x7u

#define APTA_SECTION_FLAG_REQUIRED   (1u << 0)
#define APTA_SECTION_FLAG_COMPRESSED (1u << 1)
#define APTA_SECTION_FLAG_ENCRYPTED  (1u << 2)
#define APTA_SECTION_FLAG_MASK       0x7u

#define APTA_WOVR_STATE_MASK 0x7u
#define APTA_WAVEFORM_COLUMN_FLAG_MASK 0x1Fu

#define APTA_PARSE_DEFAULT_MAX_FILE_BYTES       UINT64_C(268435456)
#define APTA_PARSE_DEFAULT_MAX_SECTION_COUNT    64u
#define APTA_PARSE_DEFAULT_MAX_OVERVIEW_SPANS   65536u
#define APTA_PARSE_DEFAULT_MAX_WAVEFORM_COLUMNS 16777216u
#define APTA_PARSE_DEFAULT_MAX_ALLOCATION_BYTES UINT64_C(268435456)

typedef struct {
    uint32_t flags;
    uint32_t maximum_section_count;
    uint32_t maximum_overview_spans;
    uint32_t maximum_waveform_columns;
    uint64_t maximum_file_bytes;
    uint64_t maximum_allocation_bytes;
} apta_effective_parse_options_t;

typedef struct {
    const uint8_t *payload;
    size_t payload_size;
    uint32_t container_flags;
    uint64_t total_source_frames;
    uint32_t source_sample_rate;
    uint32_t source_channel_count;
    uint32_t source_channel_layout;
    apta_source_fingerprint_kind_t source_fingerprint_kind;
    uint8_t source_fingerprint[APTA_SOURCE_FINGERPRINT_SIZE];
    uint32_t specification_major;
    uint32_t specification_minor;
    uint32_t producer_api_version;
} apta_wovr_input_t;

static uint16_t apta_get_u16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8u));
}

static uint32_t apta_get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8u) |
           ((uint32_t)source[2] << 16u) |
           ((uint32_t)source[3] << 24u);
}

static uint64_t apta_get_u64(const uint8_t *source)
{
    return (uint64_t)apta_get_u32(source) |
           ((uint64_t)apta_get_u32(source + 4u) << 32u);
}

static int16_t apta_get_i16(const uint8_t *source)
{
    return (int16_t)apta_get_u16(source);
}

static int apta_range_fits(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static int apta_ranges_overlap(
    uint64_t first_offset,
    uint64_t first_size,
    uint64_t second_offset,
    uint64_t second_size)
{
    uint64_t first_end;
    uint64_t second_end;

    if (first_size == 0u || second_size == 0u) {
        return 0;
    }

    first_end = first_offset + first_size;
    second_end = second_offset + second_size;
    return first_offset < second_end && second_offset < first_end;
}

static int apta_bytes_are_zero(const uint8_t *data, size_t size)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (data[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int apta_is_standard_section(const uint8_t *entry)
{
    return memcmp(entry, "META", 4u) == 0 ||
           memcmp(entry, "WOVR", 4u) == 0 ||
           memcmp(entry, "WDTL", 4u) == 0 ||
           memcmp(entry, "TEMP", 4u) == 0 ||
           memcmp(entry, "LGRD", 4u) == 0 ||
           memcmp(entry, "GGRD", 4u) == 0 ||
           memcmp(entry, "REVN", 4u) == 0 ||
           memcmp(entry, "MKEY", 4u) == 0 ||
           memcmp(entry, "MTRD", 4u) == 0 ||
           memcmp(entry, "CONF", 4u) == 0;
}

void APTA_CALL apta_parse_options_init(apta_parse_options_t *options)
{
    if (options == NULL) {
        return;
    }

    memset(options, 0, sizeof(*options));
    options->struct_size = (uint32_t)sizeof(*options);
    options->api_version = APTA_API_VERSION;
    options->flags = APTA_PARSE_STRICT;
    options->maximum_section_count = APTA_PARSE_DEFAULT_MAX_SECTION_COUNT;
    options->maximum_overview_spans = APTA_PARSE_DEFAULT_MAX_OVERVIEW_SPANS;
    options->maximum_waveform_columns =
        APTA_PARSE_DEFAULT_MAX_WAVEFORM_COLUMNS;
    options->maximum_file_bytes = APTA_PARSE_DEFAULT_MAX_FILE_BYTES;
    options->maximum_allocation_bytes =
        APTA_PARSE_DEFAULT_MAX_ALLOCATION_BYTES;
}

static apta_status_t apta_resolve_parse_options(
    const apta_parse_options_t *options,
    apta_effective_parse_options_t *effective)
{
    uint32_t index;

    if (effective == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    effective->flags = APTA_PARSE_STRICT;
    effective->maximum_section_count = APTA_PARSE_DEFAULT_MAX_SECTION_COUNT;
    effective->maximum_overview_spans = APTA_PARSE_DEFAULT_MAX_OVERVIEW_SPANS;
    effective->maximum_waveform_columns =
        APTA_PARSE_DEFAULT_MAX_WAVEFORM_COLUMNS;
    effective->maximum_file_bytes = APTA_PARSE_DEFAULT_MAX_FILE_BYTES;
    effective->maximum_allocation_bytes =
        APTA_PARSE_DEFAULT_MAX_ALLOCATION_BYTES;

    if (options == NULL) {
        return APTA_STATUS_OK;
    }

    if (!apta_internal_validate_struct(
            options,
            sizeof(*options),
            options->struct_size,
            options->api_version) ||
        (options->flags & ~APTA_PARSE_STRICT) != 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    for (index = 0u; index < 4u; ++index) {
        if (options->reserved64[index] != 0u) {
            return APTA_ERROR_INVALID_ARGUMENT;
        }
    }

    effective->flags = options->flags;
    if (options->maximum_section_count != 0u) {
        effective->maximum_section_count = options->maximum_section_count;
    }
    if (options->maximum_overview_spans != 0u) {
        effective->maximum_overview_spans = options->maximum_overview_spans;
    }
    if (options->maximum_waveform_columns != 0u) {
        effective->maximum_waveform_columns = options->maximum_waveform_columns;
    }
    if (options->maximum_file_bytes != 0u) {
        effective->maximum_file_bytes = options->maximum_file_bytes;
    }
    if (options->maximum_allocation_bytes != 0u) {
        effective->maximum_allocation_bytes = options->maximum_allocation_bytes;
    }

    return APTA_STATUS_OK;
}

static apta_status_t apta_validate_container(
    const uint8_t *bytes,
    size_t buffer_size,
    const apta_effective_parse_options_t *options,
    apta_wovr_input_t *input)
{
    uint16_t header_size;
    uint16_t container_version;
    uint32_t container_flags;
    uint32_t section_count;
    uint64_t directory_offset;
    uint64_t directory_size;
    uint64_t directory_end;
    uint64_t total_file_size;
    uint64_t total_source_frames;
    uint32_t source_fingerprint_kind;
    uint32_t index;
    int found_wovr;

    if (buffer_size < APTA_CONTAINER_HEADER_SIZE) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if ((uint64_t)buffer_size > options->maximum_file_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (memcmp(bytes, "APTA", 4u) != 0) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    header_size = apta_get_u16(bytes + 4u);
    container_version = apta_get_u16(bytes + 6u);
    input->specification_major = apta_get_u16(bytes + 8u);
    input->specification_minor = apta_get_u16(bytes + 10u);
    input->producer_api_version = apta_get_u32(bytes + 12u);
    container_flags = apta_get_u32(bytes + 16u);
    section_count = apta_get_u32(bytes + 20u);
    directory_offset = apta_get_u64(bytes + 24u);
    total_file_size = apta_get_u64(bytes + 32u);
    total_source_frames = apta_get_u64(bytes + 40u);
    input->source_sample_rate = apta_get_u32(bytes + 48u);
    input->source_channel_count = apta_get_u16(bytes + 52u);
    input->source_channel_layout = apta_get_u16(bytes + 54u);
    source_fingerprint_kind = apta_get_u32(bytes + 88u);
    input->source_fingerprint_kind = source_fingerprint_kind;
    memcpy(
        input->source_fingerprint,
        bytes + 56u,
        APTA_SOURCE_FINGERPRINT_SIZE);

    if (header_size < APTA_CONTAINER_HEADER_SIZE ||
        header_size > buffer_size ||
        container_version != 1u ||
        input->specification_major != APTA_SPEC_VERSION_MAJOR ||
        input->specification_minor > APTA_SPEC_VERSION_MINOR) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if ((container_flags & ~APTA_CONTAINER_FLAG_MASK) != 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (section_count == 0u ||
        section_count > options->maximum_section_count) {
        return section_count == 0u
                   ? APTA_ERROR_CORRUPT_DATA
                   : APTA_ERROR_LIMIT_EXCEEDED;
    }
    if (directory_offset < header_size ||
        (directory_offset & 7u) != 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    directory_size = (uint64_t)section_count * APTA_DIRECTORY_ENTRY_SIZE;
    if (!apta_range_fits(directory_offset, directory_size, buffer_size)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    directory_end = directory_offset + directory_size;

    if (total_file_size != buffer_size ||
        input->source_sample_rate == 0u ||
        input->source_channel_count == 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (apta_internal_crc32c(bytes, 92u) != apta_get_u32(bytes + 92u)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (source_fingerprint_kind != APTA_SOURCE_FINGERPRINT_NONE &&
        source_fingerprint_kind !=
            APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256 &&
        source_fingerprint_kind !=
            APTA_SOURCE_FINGERPRINT_SHA256_SOURCE_OBJECT_BYTES) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (source_fingerprint_kind == APTA_SOURCE_FINGERPRINT_NONE &&
        !apta_bytes_are_zero(
            bytes + 56u,
            APTA_SOURCE_FINGERPRINT_SIZE)) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    if (total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN) {
        if ((container_flags & APTA_CONTAINER_FLAG_SOURCE_DURATION_UNKNOWN) == 0u ||
            (container_flags & APTA_CONTAINER_FLAG_PARTIAL_RESULT) == 0u) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    } else if ((container_flags &
                APTA_CONTAINER_FLAG_SOURCE_DURATION_UNKNOWN) != 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    found_wovr = 0;
    for (index = 0u; index < section_count; ++index) {
        const uint8_t *entry = bytes + (size_t)directory_offset +
                               (size_t)index * APTA_DIRECTORY_ENTRY_SIZE;
        uint16_t section_version = apta_get_u16(entry + 4u);
        uint16_t section_flags = apta_get_u16(entry + 6u);
        uint64_t section_offset = apta_get_u64(entry + 8u);
        uint64_t stored_size = apta_get_u64(entry + 16u);
        uint64_t logical_size = apta_get_u64(entry + 24u);
        uint32_t section_crc = apta_get_u32(entry + 32u);
        uint32_t reserved = apta_get_u32(entry + 36u);
        uint32_t earlier;
        int is_wovr = memcmp(entry, "WOVR", 4u) == 0;
        int is_standard = apta_is_standard_section(entry);

        if ((section_flags &
             (APTA_SECTION_FLAG_COMPRESSED | APTA_SECTION_FLAG_ENCRYPTED)) != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        if ((section_flags & ~APTA_SECTION_FLAG_MASK) != 0u ||
            (((options->flags & APTA_PARSE_STRICT) != 0u) && reserved != 0u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        if (is_standard && section_version != 1u) {
            return APTA_ERROR_UNSUPPORTED;
        }
        if ((is_wovr &&
             (section_flags & APTA_SECTION_FLAG_REQUIRED) == 0u) ||
            (!is_wovr && is_standard &&
             (section_flags & APTA_SECTION_FLAG_REQUIRED) != 0u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        if ((section_offset & 7u) != 0u ||
            stored_size != logical_size ||
            !apta_range_fits(section_offset, stored_size, buffer_size)) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        if (apta_ranges_overlap(section_offset, stored_size, 0u, header_size) ||
            apta_ranges_overlap(
                section_offset,
                stored_size,
                directory_offset,
                directory_size)) {
            return APTA_ERROR_CORRUPT_DATA;
        }

        for (earlier = 0u; earlier < index; ++earlier) {
            const uint8_t *prior = bytes + (size_t)directory_offset +
                                   (size_t)earlier * APTA_DIRECTORY_ENTRY_SIZE;
            uint64_t prior_offset = apta_get_u64(prior + 8u);
            uint64_t prior_size = apta_get_u64(prior + 16u);
            if (apta_ranges_overlap(
                    section_offset,
                    stored_size,
                    prior_offset,
                    prior_size)) {
                return APTA_ERROR_CORRUPT_DATA;
            }
        }

        if (stored_size > SIZE_MAX ||
            apta_internal_crc32c(
                bytes + (size_t)section_offset,
                (size_t)stored_size) != section_crc) {
            return APTA_ERROR_CORRUPT_DATA;
        }

        if (is_wovr) {
            if (found_wovr) {
                return APTA_ERROR_CORRUPT_DATA;
            }
            found_wovr = 1;
            input->payload = bytes + (size_t)section_offset;
            input->payload_size = (size_t)stored_size;
        } else if (!is_standard &&
                   (section_flags & APTA_SECTION_FLAG_REQUIRED) != 0u) {
            return APTA_ERROR_UNSUPPORTED;
        }
    }

    if (!found_wovr || directory_end > buffer_size) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    input->container_flags = container_flags;
    input->total_source_frames = total_source_frames;
    return APTA_STATUS_OK;
}

static apta_status_t apta_validate_wovr_geometry(
    const apta_wovr_input_t *input,
    const apta_effective_parse_options_t *options,
    uint32_t *packed_column_count_out)
{
    const uint8_t *payload = input->payload;
    uint32_t frames_per_column;
    uint64_t origin_frame;
    uint32_t logical_column_count;
    uint32_t span_count;
    uint64_t span_directory_offset;
    uint64_t column_data_offset;
    uint32_t wovr_flags;
    apta_feature_state_t state;
    uint64_t span_bytes;
    uint64_t packed_columns;
    uint64_t previous_end_frame;
    uint64_t previous_end_column;
    uint32_t span_index;

    if (input->payload_size < APTA_WOVR_HEADER_SIZE) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    frames_per_column = apta_get_u32(payload + 4u);
    origin_frame = apta_get_u64(payload + 8u);
    logical_column_count = apta_get_u32(payload + 16u);
    span_count = apta_get_u32(payload + 20u);
    span_directory_offset = apta_get_u64(payload + 24u);
    column_data_offset = apta_get_u64(payload + 32u);
    wovr_flags = apta_get_u32(payload + 40u);
    state = wovr_flags & APTA_WOVR_STATE_MASK;

    if (frames_per_column == 0u || logical_column_count == 0u ||
        span_count == 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (span_count > options->maximum_overview_spans ||
        logical_column_count > options->maximum_waveform_columns) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    if ((wovr_flags & ~APTA_WOVR_STATE_MASK) != 0u ||
        state < APTA_FEATURE_PARTIAL || state > APTA_FEATURE_FINAL ||
        (((options->flags & APTA_PARSE_STRICT) != 0u) &&
         apta_get_u32(payload + 44u) != 0u)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
    if (state == APTA_FEATURE_FINAL) {
        if (input->total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    } else if ((input->container_flags &
                APTA_CONTAINER_FLAG_PARTIAL_RESULT) == 0u) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    span_bytes = (uint64_t)span_count * APTA_WOVR_SPAN_SIZE;
    if (span_directory_offset < APTA_WOVR_HEADER_SIZE ||
        column_data_offset < APTA_WOVR_HEADER_SIZE ||
        !apta_range_fits(
            span_directory_offset,
            span_bytes,
            input->payload_size)) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    if (input->total_source_frames != APTA_TOTAL_FRAMES_UNKNOWN) {
        uint64_t relative_frames;
        uint64_t required_columns;

        if (input->total_source_frames < origin_frame) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        relative_frames = input->total_source_frames - origin_frame;
        required_columns = relative_frames / frames_per_column;
        if ((relative_frames % frames_per_column) != 0u) {
            required_columns += 1u;
        }
        if (required_columns != logical_column_count) {
            return APTA_ERROR_CORRUPT_DATA;
        }
    }

    packed_columns = 0u;
    previous_end_frame = 0u;
    previous_end_column = 0u;

    for (span_index = 0u; span_index < span_count; ++span_index) {
        const uint8_t *span = payload + (size_t)span_directory_offset +
                              (size_t)span_index * APTA_WOVR_SPAN_SIZE;
        uint64_t first_frame = apta_get_u64(span + 0u);
        uint64_t end_frame = apta_get_u64(span + 8u);
        uint32_t first_column = apta_get_u32(span + 16u);
        uint32_t column_count = apta_get_u32(span + 20u);
        uint32_t data_column_offset = apta_get_u32(span + 24u);
        uint32_t reserved = apta_get_u32(span + 28u);
        uint64_t end_column;
        uint64_t first_offset;
        uint64_t end_offset;
        uint64_t expected_first;
        uint64_t expected_end;
        uint64_t data_first;
        uint64_t data_bytes;
        uint32_t column_index;

        if (first_frame >= end_frame || column_count == 0u ||
            (((options->flags & APTA_PARSE_STRICT) != 0u) && reserved != 0u)) {
            return APTA_ERROR_CORRUPT_DATA;
        }

        end_column = (uint64_t)first_column + column_count;
        if (end_column > logical_column_count ||
            (span_index != 0u &&
             (first_frame < previous_end_frame ||
              first_column < previous_end_column))) {
            return APTA_ERROR_CORRUPT_DATA;
        }

        first_offset = (uint64_t)first_column * frames_per_column;
        end_offset = end_column * frames_per_column;
        if (origin_frame > UINT64_MAX - first_offset ||
            origin_frame > UINT64_MAX - end_offset) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        expected_first = origin_frame + first_offset;
        expected_end = origin_frame + end_offset;
        if (first_frame != expected_first) {
            return APTA_ERROR_CORRUPT_DATA;
        }
        if (end_frame != expected_end) {
            if (input->total_source_frames == APTA_TOTAL_FRAMES_UNKNOWN ||
                end_frame != input->total_source_frames ||
                end_column != logical_column_count ||
                expected_end <= input->total_source_frames) {
                return APTA_ERROR_CORRUPT_DATA;
            }
        }
        if (input->total_source_frames != APTA_TOTAL_FRAMES_UNKNOWN &&
            end_frame > input->total_source_frames) {
            return APTA_ERROR_CORRUPT_DATA;
        }

        data_first = column_data_offset +
                     (uint64_t)data_column_offset * APTA_PACKED_COLUMN_SIZE;
        data_bytes = (uint64_t)column_count * APTA_PACKED_COLUMN_SIZE;
        if (data_first < column_data_offset ||
            !apta_range_fits(data_first, data_bytes, input->payload_size) ||
            apta_ranges_overlap(
                data_first,
                data_bytes,
                0u,
                APTA_WOVR_HEADER_SIZE) ||
            apta_ranges_overlap(
                data_first,
                data_bytes,
                span_directory_offset,
                span_bytes)) {
            return APTA_ERROR_CORRUPT_DATA;
        }

        for (column_index = 0u; column_index < column_count; ++column_index) {
            const uint8_t *column = payload + (size_t)data_first +
                                    (size_t)column_index *
                                        APTA_PACKED_COLUMN_SIZE;
            int16_t minimum = apta_get_i16(column + 0u);
            int16_t maximum = apta_get_i16(column + 2u);
            uint8_t flags = column[9];

            if ((flags & APTA_WAVEFORM_COLUMN_VALID) == 0u ||
                minimum > maximum ||
                (((options->flags & APTA_PARSE_STRICT) != 0u) &&
                 (flags & ~APTA_WAVEFORM_COLUMN_FLAG_MASK) != 0u) ||
                (((options->flags & APTA_PARSE_STRICT) != 0u) &&
                 (flags & APTA_WAVEFORM_COLUMN_HAS_3BAND) == 0u &&
                 (column[6] != 0u || column[7] != 0u || column[8] != 0u))) {
                return APTA_ERROR_CORRUPT_DATA;
            }
        }

        if (packed_columns > UINT32_MAX - column_count) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }
        packed_columns += column_count;
        if (packed_columns > options->maximum_waveform_columns) {
            return APTA_ERROR_LIMIT_EXCEEDED;
        }

        previous_end_frame = end_frame;
        previous_end_column = end_column;
    }

    if (state == APTA_FEATURE_FINAL &&
        (previous_end_column != logical_column_count ||
         apta_get_u32(
             payload + (size_t)span_directory_offset + 16u) != 0u)) {
        return APTA_ERROR_CORRUPT_DATA;
    }

    *packed_column_count_out = (uint32_t)packed_columns;
    return APTA_STATUS_OK;
}

static apta_status_t apta_build_parsed_result(
    apta_context_t *context,
    const apta_wovr_input_t *input,
    const apta_effective_parse_options_t *options,
    uint32_t packed_column_count,
    const apta_result_t **result_out)
{
    const uint8_t *payload = input->payload;
    uint32_t span_count = apta_get_u32(payload + 20u);
    uint64_t span_directory_offset = apta_get_u64(payload + 24u);
    uint64_t column_data_offset = apta_get_u64(payload + 32u);
    uint64_t allocation_bytes;
    apta_result_t *result;
    uint32_t span_index;
    uint32_t packed_output_offset;

    allocation_bytes = sizeof(*result) +
                       (uint64_t)span_count * sizeof(apta_waveform_span_t) +
                       (uint64_t)packed_column_count *
                           sizeof(apta_waveform_column_t);
    if (allocation_bytes > options->maximum_allocation_bytes) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }

    result = (apta_result_t *)apta_internal_context_allocate(
        context,
        sizeof(*result),
        alignof(apta_result_t),
        APTA_MEMORY_PERSISTENT);
    if (result == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }
    memset(result, 0, sizeof(*result));
    result->context = context;
    atomic_init(&result->reference_count, 1u);
    apta_internal_result_init_absent_views(result);

    result->overview_spans =
        (apta_waveform_span_t *)apta_internal_context_allocate(
            context,
            (size_t)span_count * sizeof(*result->overview_spans),
            alignof(apta_waveform_span_t),
            APTA_MEMORY_PERSISTENT);
    if (result->overview_spans == NULL) {
        apta_internal_context_deallocate(context, result);
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    result->overview_columns =
        (apta_waveform_column_t *)apta_internal_context_allocate(
            context,
            (size_t)packed_column_count * sizeof(*result->overview_columns),
            alignof(apta_waveform_column_t),
            APTA_MEMORY_PERSISTENT);
    if (result->overview_columns == NULL) {
        apta_internal_context_deallocate(context, result->overview_spans);
        apta_internal_context_deallocate(context, result);
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(
        result->overview_spans,
        0,
        (size_t)span_count * sizeof(*result->overview_spans));
    memset(
        result->overview_columns,
        0,
        (size_t)packed_column_count * sizeof(*result->overview_columns));

    result->info.struct_size = (uint32_t)sizeof(result->info);
    result->info.api_version = APTA_API_VERSION;
    result->info.specification_major = input->specification_major;
    result->info.specification_minor = input->specification_minor;
    result->info.producer_api_version = input->producer_api_version;
    result->info.container_version = 1u;
    result->info.generation = 1u;
    result->info.available_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    result->info.changed_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    result->info.session_state =
        (input->container_flags & APTA_CONTAINER_FLAG_PARTIAL_RESULT) != 0u
            ? APTA_SESSION_ACTIVE
            : APTA_SESSION_COMPLETED;

    apta_source_info_init(&result->source_info);
    result->source_info.total_frames = input->total_source_frames;
    result->source_info.sample_rate = input->source_sample_rate;
    result->source_info.channel_count =
        (uint16_t)input->source_channel_count;
    result->source_info.channel_layout = input->source_channel_layout;
    result->source_info.fingerprint_kind =
        input->source_fingerprint_kind;
    memcpy(
        result->source_info.fingerprint,
        input->source_fingerprint,
        APTA_SOURCE_FINGERPRINT_SIZE);

    result->total_source_frames = result->source_info.total_frames;
    result->source_sample_rate = result->source_info.sample_rate;
    result->source_channel_count = result->source_info.channel_count;
    result->source_channel_layout = result->source_info.channel_layout;

    result->overview.struct_size = (uint32_t)sizeof(result->overview);
    result->overview.api_version = APTA_API_VERSION;
    result->overview.level.struct_size =
        (uint32_t)sizeof(result->overview.level);
    result->overview.level.api_version = APTA_API_VERSION;
    result->overview.level.level_id = apta_get_u32(payload + 0u);
    result->overview.level.frames_per_column = apta_get_u32(payload + 4u);
    result->overview.level.origin_frame = apta_get_u64(payload + 8u);
    result->overview.state =
        apta_get_u32(payload + 40u) & APTA_WOVR_STATE_MASK;
    result->overview.confidence = APTA_CONFIDENCE_UNKNOWN;
    result->overview.span_count = span_count;
    result->overview.spans = result->overview_spans;

    packed_output_offset = 0u;
    for (span_index = 0u; span_index < span_count; ++span_index) {
        const uint8_t *span_input = payload + (size_t)span_directory_offset +
                                    (size_t)span_index * APTA_WOVR_SPAN_SIZE;
        apta_waveform_span_t *span_output =
            &result->overview_spans[span_index];
        uint32_t column_count = apta_get_u32(span_input + 20u);
        uint32_t data_column_offset = apta_get_u32(span_input + 24u);
        uint64_t data_first = column_data_offset +
                              (uint64_t)data_column_offset *
                                  APTA_PACKED_COLUMN_SIZE;
        uint32_t column_index;

        span_output->source_range.struct_size =
            (uint32_t)sizeof(span_output->source_range);
        span_output->source_range.api_version = APTA_API_VERSION;
        span_output->source_range.first_frame = apta_get_u64(span_input + 0u);
        span_output->source_range.end_frame = apta_get_u64(span_input + 8u);
        span_output->first_column_index = apta_get_u32(span_input + 16u);
        span_output->column_count = column_count;
        span_output->columns =
            &result->overview_columns[packed_output_offset];

        for (column_index = 0u; column_index < column_count; ++column_index) {
            const uint8_t *column_input = payload + (size_t)data_first +
                                          (size_t)column_index *
                                              APTA_PACKED_COLUMN_SIZE;
            apta_waveform_column_t *column_output =
                &result->overview_columns[packed_output_offset + column_index];

            column_output->minimum = apta_get_i16(column_input + 0u);
            column_output->maximum = apta_get_i16(column_input + 2u);
            column_output->rms = apta_get_u16(column_input + 4u);
            column_output->low = column_input[6];
            column_output->mid = column_input[7];
            column_output->high = column_input[8];
            column_output->flags = column_input[9];
            if ((column_output->flags &
                 APTA_WAVEFORM_COLUMN_HAS_3BAND) != 0u) {
                result->info.available_features |=
                    APTA_FEATURE_WAVEFORM_3BAND;
            }
        }

        packed_output_offset += column_count;
    }

    (void)atomic_fetch_add_explicit(
        &context->result_count,
        1u,
        memory_order_acq_rel);
    *result_out = result;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_parse(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out)
{
    apta_effective_parse_options_t effective;
    apta_wovr_input_t input;
    uint32_t packed_column_count;
    apta_status_t status;

    if (result_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *result_out = NULL;

    if (context == NULL || buffer == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    status = apta_resolve_parse_options(options, &effective);
    if (status < 0) {
        return status;
    }

    memset(&input, 0, sizeof(input));
    status = apta_validate_container(
        (const uint8_t *)buffer,
        buffer_size,
        &effective,
        &input);
    if (status < 0) {
        return status;
    }

    status = apta_validate_wovr_geometry(
        &input,
        &effective,
        &packed_column_count);
    if (status < 0) {
        return status;
    }

    return apta_build_parsed_result(
        context,
        &input,
        &effective,
        packed_column_count,
        result_out);
}
