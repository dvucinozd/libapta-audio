// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_SERIALIZATION_H
#define APTA_SERIALIZATION_H

#include <stddef.h>
#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APTA_SERIALIZE_CANONICAL (1u << 0)
#define APTA_PARSE_STRICT        (1u << 0)

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t flags;
    uint32_t reserved32[5];

    uint64_t maximum_output_bytes;
    uint64_t reserved64[3];
} apta_serialize_options_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t flags;
    uint32_t maximum_section_count;
    uint32_t maximum_overview_spans;
    uint32_t maximum_waveform_columns;

    uint64_t maximum_file_bytes;
    /*
     * Maximum aggregate logical bytes allocated while constructing the
     * parsed result. Zero selects the library default.
     */
    uint64_t maximum_allocation_bytes;
    uint64_t reserved64[4];
} apta_parse_options_t;

/*
 * Stream callbacks report progress through their final uint64_t output.
 * A successful callback may make partial progress and is retried. Returning
 * success with zero progress for a non-zero request is a source error.
 * seek positions and read_at offsets are absolute 64-bit byte positions.
 * Callbacks must not retain library-owned data pointers after returning.
 *
 * The output target must be empty or truncated before serialization; the
 * callback set intentionally has no truncate operation. The input size must
 * remain stable for the complete parse operation.
 */
typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    void *user_data;

    apta_status_t (APTA_CALL *write)(
        void *user_data,
        const void *data,
        uint64_t requested_bytes,
        uint64_t *written_bytes_out);
    apta_status_t (APTA_CALL *seek)(
        void *user_data,
        uint64_t absolute_position);
    apta_status_t (APTA_CALL *flush)(void *user_data);

    uint64_t reserved64[4];
} apta_output_stream_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    void *user_data;

    apta_status_t (APTA_CALL *read_at)(
        void *user_data,
        uint64_t absolute_offset,
        void *data,
        uint64_t requested_bytes,
        uint64_t *read_bytes_out);
    apta_status_t (APTA_CALL *get_size)(
        void *user_data,
        uint64_t *size_out);

    uint64_t reserved64[4];
} apta_input_stream_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t flags;
    uint32_t maximum_section_count;
    uint32_t maximum_overview_spans;
    uint32_t maximum_waveform_columns;

    /* Only selected result features are materialized. Known unselected
     * sections are still framed, bounded and CRC-validated. */
    apta_feature_mask_t requested_features;
    /* Zero selects the corresponding library default. */
    uint64_t maximum_input_bytes;
    uint64_t maximum_section_bytes;
    uint64_t maximum_allocation_bytes;

    /* A caller buffer avoids the temporary scratch allocation. When present,
     * scratch_buffer_size must be at least maximum_scratch_bytes. */
    void *scratch_buffer;
    uint64_t scratch_buffer_size;
    uint64_t maximum_scratch_bytes;
    uint64_t reserved64[3];
} apta_stream_parse_options_t;

APTA_API void APTA_CALL
apta_serialize_options_init(apta_serialize_options_t *options);

APTA_API void APTA_CALL
apta_parse_options_init(apta_parse_options_t *options);

APTA_API void APTA_CALL
apta_output_stream_init(apta_output_stream_t *stream);

APTA_API void APTA_CALL
apta_input_stream_init(apta_input_stream_t *stream);

APTA_API void APTA_CALL
apta_stream_parse_options_init(apta_stream_parse_options_t *options);

APTA_API apta_status_t APTA_CALL
apta_result_query_serialized_size(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    uint64_t *size_out);

APTA_API apta_status_t APTA_CALL
apta_result_serialize(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    void *buffer,
    size_t buffer_size,
    size_t *bytes_written_out);

APTA_API apta_status_t APTA_CALL
apta_result_parse(
    apta_context_t *context,
    const apta_parse_options_t *options,
    const void *buffer,
    size_t buffer_size,
    const apta_result_t **result_out);

APTA_API apta_status_t APTA_CALL
apta_result_serialize_to_stream(
    const apta_result_t *result,
    const apta_serialize_options_t *options,
    const apta_output_stream_t *stream,
    uint64_t *bytes_written_out);

APTA_API apta_status_t APTA_CALL
apta_result_parse_from_stream(
    apta_context_t *context,
    const apta_stream_parse_options_t *options,
    const apta_input_stream_t *stream,
    const apta_result_t **result_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_SERIALIZATION_H */
