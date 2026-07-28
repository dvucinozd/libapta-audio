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

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t flags;
    uint32_t reserved32[5];

    uint64_t maximum_output_bytes;
    uint64_t reserved64[3];
} apta_serialize_options_t;

APTA_API void APTA_CALL
apta_serialize_options_init(apta_serialize_options_t *options);

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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_SERIALIZATION_H */