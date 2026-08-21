// SPDX-License-Identifier: Apache-2.0
#ifndef LIBAPTA_TEST_STREAM_EQUIVALENCE_H
#define LIBAPTA_TEST_STREAM_EQUIVALENCE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

typedef struct {
    uint8_t *bytes;
    uint64_t capacity;
    uint64_t position;
    uint64_t size;
    uint32_t flush_count;
} apta_test_output_t;

typedef struct {
    const uint8_t *bytes;
    uint64_t size;
    uint64_t maximum_request;
} apta_test_input_t;

static apta_status_t APTA_CALL apta_test_output_write(void *user_data,
                                                      const void *data,
                                                      uint64_t requested,
                                                      uint64_t *written_out)
{
    apta_test_output_t *output = (apta_test_output_t *)user_data;
    *written_out = 0u;
    if (output->position > output->capacity ||
        requested > output->capacity - output->position) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    memcpy(output->bytes + (size_t)output->position, data, (size_t)requested);
    output->position += requested;
    if (output->position > output->size)
        output->size = output->position;
    *written_out = requested;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL apta_test_output_seek(void *user_data,
                                                     uint64_t position)
{
    apta_test_output_t *output = (apta_test_output_t *)user_data;
    if (position > output->capacity)
        return APTA_ERROR_LIMIT_EXCEEDED;
    output->position = position;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL apta_test_output_flush(void *user_data)
{
    apta_test_output_t *output = (apta_test_output_t *)user_data;
    ++output->flush_count;
    return APTA_STATUS_OK;
}

static int apta_test_stream_matches_buffer(const apta_result_t *result,
                                           const uint8_t *expected,
                                           uint64_t expected_size)
{
    apta_test_output_t sink;
    apta_output_stream_t stream;
    uint64_t written = 0u;
    apta_status_t status;
    if (expected_size > SIZE_MAX)
        return 0;
    memset(&sink, 0, sizeof(sink));
    sink.bytes = (uint8_t *)malloc((size_t)expected_size);
    if (sink.bytes == NULL)
        return 0;
    sink.capacity = expected_size;
    apta_output_stream_init(&stream);
    stream.user_data = &sink;
    stream.write = apta_test_output_write;
    stream.seek = apta_test_output_seek;
    stream.flush = apta_test_output_flush;
    status = apta_result_serialize_to_stream(result, NULL, &stream, &written);
    if (status != APTA_STATUS_OK || written != expected_size ||
        sink.size != expected_size || sink.flush_count != 1u ||
        memcmp(sink.bytes, expected, (size_t)expected_size) != 0) {
        free(sink.bytes);
        return 0;
    }
    free(sink.bytes);
    return 1;
}

static apta_status_t APTA_CALL apta_test_input_read_at(void *user_data,
                                                       uint64_t offset,
                                                       void *data,
                                                       uint64_t requested,
                                                       uint64_t *read_out)
{
    apta_test_input_t *input = (apta_test_input_t *)user_data;
    uint64_t amount = requested;
    *read_out = 0u;
    if (offset >= input->size)
        return APTA_STATUS_END_OF_INPUT;
    if (amount > input->maximum_request)
        amount = input->maximum_request;
    if (amount > input->size - offset)
        amount = input->size - offset;
    memcpy(data, input->bytes + (size_t)offset, (size_t)amount);
    *read_out = amount;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL apta_test_input_get_size(void *user_data,
                                                        uint64_t *size_out)
{
    apta_test_input_t *input = (apta_test_input_t *)user_data;
    *size_out = input->size;
    return APTA_STATUS_OK;
}

static int apta_test_stream_parse_matches_buffer(apta_context_t *context,
                                                 const uint8_t *expected,
                                                 uint64_t expected_size)
{
    uint8_t scratch[13];
    apta_test_input_t source;
    apta_input_stream_t stream;
    apta_stream_parse_options_t options;
    const apta_result_t *result = NULL;
    uint8_t *roundtrip = NULL;
    uint64_t required = 0u;
    size_t written = 0u;
    apta_status_t status;
    source.bytes = expected;
    source.size = expected_size;
    source.maximum_request = 5u;
    apta_input_stream_init(&stream);
    stream.user_data = &source;
    stream.read_at = apta_test_input_read_at;
    stream.get_size = apta_test_input_get_size;
    apta_stream_parse_options_init(&options);
    options.scratch_buffer = scratch;
    options.scratch_buffer_size = sizeof(scratch);
    options.maximum_scratch_bytes = sizeof(scratch);
    status = apta_result_parse_from_stream(context, &options, &stream, &result);
    if (status != APTA_STATUS_OK || result == NULL ||
        apta_result_query_serialized_size(result, NULL, &required) !=
            APTA_STATUS_OK ||
        required != expected_size || required > SIZE_MAX) {
        if (result != NULL)
            apta_result_release(result);
        return 0;
    }
    roundtrip = (uint8_t *)malloc((size_t)required);
    if (roundtrip != NULL)
        status = apta_result_serialize(result, NULL, roundtrip,
                                       (size_t)required, &written);
    apta_result_release(result);
    if (roundtrip == NULL || status != APTA_STATUS_OK ||
        written != (size_t)required ||
        memcmp(roundtrip, expected, (size_t)required) != 0) {
        free(roundtrip);
        return 0;
    }
    free(roundtrip);
    return 1;
}

static int apta_test_stream_selects_features(apta_context_t *context,
                                             const uint8_t *bytes,
                                             uint64_t size,
                                             apta_feature_mask_t requested,
                                             apta_feature_mask_t expected)
{
    uint8_t scratch[11];
    apta_test_input_t source;
    apta_input_stream_t stream;
    apta_stream_parse_options_t options;
    const apta_result_t *result = NULL;
    apta_status_t status;
    source.bytes = bytes;
    source.size = size;
    source.maximum_request = 3u;
    apta_input_stream_init(&stream);
    stream.user_data = &source;
    stream.read_at = apta_test_input_read_at;
    stream.get_size = apta_test_input_get_size;
    apta_stream_parse_options_init(&options);
    options.requested_features = requested;
    options.scratch_buffer = scratch;
    options.scratch_buffer_size = sizeof(scratch);
    options.maximum_scratch_bytes = sizeof(scratch);
    status = apta_result_parse_from_stream(context, &options, &stream, &result);
    if (status != APTA_STATUS_OK || result == NULL ||
        apta_result_get_available_features(result) != expected) {
        if (result != NULL)
            apta_result_release(result);
        return 0;
    }
    apta_result_release(result);
    return 1;
}

#endif
