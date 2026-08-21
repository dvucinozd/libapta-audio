// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>

#define FILE_CAPACITY 4096u

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

typedef struct {
    uint8_t bytes[FILE_CAPACITY];
    uint64_t size;
    uint64_t position;
    uint64_t maximum_transfer;
    uint64_t maximum_request_seen;
    uint32_t write_calls;
    uint32_t seek_calls;
    uint32_t flush_calls;
    uint32_t read_calls;
    uint32_t size_calls;
    uint32_t fail_write_call;
    uint32_t fail_seek_call;
    uint32_t fail_flush_call;
    uint32_t fail_read_call;
    uint32_t fail_size_call;
    uint32_t zero_write_call;
    uint32_t zero_read_call;
    uint32_t change_size_after_first;
} memory_stream_t;

typedef struct {
    uint32_t allocation_calls;
    uint32_t fail_at_call;
    uint32_t outstanding;
    size_t largest_allocation;
} allocator_state_t;

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static uint64_t get_u64(const uint8_t *bytes)
{
    return (uint64_t)get_u32(bytes) | ((uint64_t)get_u32(bytes + 4u) << 32u);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static uint32_t crc32c(const uint8_t *bytes, uint64_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    uint64_t index;
    for (index = 0u; index < size; ++index) {
        uint32_t bit;
        crc ^= bytes[index];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1u) ^ (UINT32_C(0x82F63B78) & mask);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static uint8_t *find_entry(memory_stream_t *stream, const char id[4])
{
    uint32_t count = get_u32(stream->bytes + 20u);
    uint64_t directory = get_u64(stream->bytes + 24u);
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        uint8_t *entry = stream->bytes + (size_t)directory + index * 40u;
        if (memcmp(entry, id, 4u) == 0)
            return entry;
    }
    return NULL;
}

static int load_fixture(memory_stream_t *stream)
{
    FILE *file = fopen(APTA_DJ_GOLDEN_HEX_PATH, "rb");
    int high = -1;
    int character;
    if (file == NULL)
        return 0;
    memset(stream, 0, sizeof(*stream));
    while ((character = fgetc(file)) != EOF) {
        int value;
        if (character >= '0' && character <= '9')
            value = character - '0';
        else if (character >= 'a' && character <= 'f')
            value = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F')
            value = character - 'A' + 10;
        else
            continue;
        if (high < 0)
            high = value;
        else {
            if (stream->size == FILE_CAPACITY) {
                fclose(file);
                return 0;
            }
            stream->bytes[stream->size++] = (uint8_t)((high << 4) | value);
            high = -1;
        }
    }
    fclose(file);
    return high < 0 && stream->size != 0u;
}

static apta_status_t APTA_CALL memory_write(void *user_data, const void *data,
                                            uint64_t requested,
                                            uint64_t *written_out)
{
    memory_stream_t *stream = (memory_stream_t *)user_data;
    uint64_t amount = requested;
    ++stream->write_calls;
    *written_out = 0u;
    if (stream->fail_write_call == stream->write_calls)
        return APTA_ERROR_SOURCE;
    if (stream->zero_write_call == stream->write_calls)
        return APTA_STATUS_OK;
    if (stream->maximum_transfer != 0u && amount > stream->maximum_transfer) {
        amount = stream->maximum_transfer;
    }
    if (stream->position > FILE_CAPACITY ||
        amount > FILE_CAPACITY - stream->position) {
        return APTA_ERROR_LIMIT_EXCEEDED;
    }
    memcpy(stream->bytes + (size_t)stream->position, data, (size_t)amount);
    stream->position += amount;
    if (stream->position > stream->size)
        stream->size = stream->position;
    *written_out = amount;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL memory_seek(void *user_data, uint64_t position)
{
    memory_stream_t *stream = (memory_stream_t *)user_data;
    ++stream->seek_calls;
    if (stream->fail_seek_call == stream->seek_calls)
        return APTA_ERROR_SOURCE;
    if (position > FILE_CAPACITY)
        return APTA_ERROR_LIMIT_EXCEEDED;
    stream->position = position;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL memory_flush(void *user_data)
{
    memory_stream_t *stream = (memory_stream_t *)user_data;
    ++stream->flush_calls;
    return stream->fail_flush_call == stream->flush_calls ? APTA_ERROR_SOURCE
                                                          : APTA_STATUS_OK;
}

static apta_status_t APTA_CALL memory_read_at(void *user_data, uint64_t offset,
                                              void *data, uint64_t requested,
                                              uint64_t *read_out)
{
    memory_stream_t *stream = (memory_stream_t *)user_data;
    uint64_t amount = requested;
    ++stream->read_calls;
    if (requested > stream->maximum_request_seen) {
        stream->maximum_request_seen = requested;
    }
    *read_out = 0u;
    if (stream->fail_read_call == stream->read_calls)
        return APTA_ERROR_SOURCE;
    if (stream->zero_read_call == stream->read_calls)
        return APTA_STATUS_OK;
    if (offset >= stream->size)
        return APTA_STATUS_END_OF_INPUT;
    if (amount > stream->size - offset)
        amount = stream->size - offset;
    if (stream->maximum_transfer != 0u && amount > stream->maximum_transfer) {
        amount = stream->maximum_transfer;
    }
    memcpy(data, stream->bytes + (size_t)offset, (size_t)amount);
    *read_out = amount;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL memory_get_size(void *user_data,
                                               uint64_t *size_out)
{
    memory_stream_t *stream = (memory_stream_t *)user_data;
    ++stream->size_calls;
    if (stream->fail_size_call == stream->size_calls)
        return APTA_ERROR_SOURCE;
    *size_out = stream->size + ((stream->change_size_after_first != 0u &&
                                 stream->size_calls > 1u)
                                    ? 1u
                                    : 0u);
    return APTA_STATUS_OK;
}

static void *APTA_CALL test_allocate(void *user_data, size_t size,
                                     size_t alignment,
                                     apta_memory_flags_t flags)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    void *memory;
    (void)alignment;
    (void)flags;
    ++state->allocation_calls;
    if (state->fail_at_call == state->allocation_calls)
        return NULL;
    memory = malloc(size);
    if (memory != NULL) {
        ++state->outstanding;
        if (size > state->largest_allocation)
            state->largest_allocation = size;
    }
    return memory;
}

static void APTA_CALL test_deallocate(void *user_data, void *memory)
{
    allocator_state_t *state = (allocator_state_t *)user_data;
    if (memory != NULL) {
        free(memory);
        --state->outstanding;
    }
}

static int create_context(allocator_state_t *allocator_state,
                          apta_context_t **context_out)
{
    apta_context_config_t config;
    apta_context_config_init(&config);
    if (allocator_state != NULL) {
        config.allocator.user_data = allocator_state;
        config.allocator.allocate = test_allocate;
        config.allocator.deallocate = test_deallocate;
    }
    return apta_context_create(&config, context_out) == APTA_STATUS_OK;
}

static int check_initializers(void)
{
    apta_output_stream_t output;
    apta_input_stream_t input;
    apta_stream_parse_options_t options;
    memset(&output, 0xA5, sizeof(output));
    memset(&input, 0xA5, sizeof(input));
    memset(&options, 0xA5, sizeof(options));
    apta_output_stream_init(&output);
    apta_input_stream_init(&input);
    apta_stream_parse_options_init(&options);
    CHECK(output.struct_size == sizeof(output));
    CHECK(output.api_version == APTA_API_VERSION);
    CHECK(output.user_data == NULL && output.write == NULL &&
          output.seek == NULL && output.flush == NULL);
    CHECK(input.struct_size == sizeof(input));
    CHECK(input.api_version == APTA_API_VERSION);
    CHECK(input.user_data == NULL && input.read_at == NULL &&
          input.get_size == NULL);
    CHECK(options.struct_size == sizeof(options));
    CHECK(options.api_version == APTA_API_VERSION);
    CHECK(options.flags == APTA_PARSE_STRICT);
    CHECK(options.requested_features == APTA_FEATURE_ALL_KNOWN);
    CHECK(options.maximum_scratch_bytes == 65536u);
    CHECK(options.scratch_buffer == NULL && options.scratch_buffer_size == 0u);
    return 0;
}

static int check_output_stream(void)
{
    memory_stream_t fixture;
    memory_stream_t output_state;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_output_stream_t output;
    uint64_t written = UINT64_MAX;
    uint32_t fail_call;
    uint32_t successful_seek_calls;
    CHECK(load_fixture(&fixture));
    CHECK(create_context(NULL, &context));
    CHECK(apta_result_parse(context, NULL, fixture.bytes, (size_t)fixture.size,
                            &result) == APTA_STATUS_OK);

    memset(&output_state, 0, sizeof(output_state));
    output_state.maximum_transfer = 3u;
    apta_output_stream_init(&output);
    output.user_data = &output_state;
    output.write = memory_write;
    output.seek = memory_seek;
    output.flush = memory_flush;
    CHECK(apta_result_serialize_to_stream(result, NULL, &output, &written) ==
          APTA_STATUS_OK);
    CHECK(written == fixture.size);
    CHECK(output_state.size == fixture.size);
    CHECK(memcmp(output_state.bytes, fixture.bytes, (size_t)fixture.size) == 0);
    CHECK(output_state.write_calls > 20u);
    CHECK(output_state.seek_calls >= 2u);
    CHECK(output_state.flush_calls == 1u);
    successful_seek_calls = output_state.seek_calls;

    for (fail_call = 1u; fail_call <= output_state.write_calls; ++fail_call) {
        memset(&output_state, 0, sizeof(output_state));
        output_state.maximum_transfer = 5u;
        output_state.fail_write_call = fail_call;
        written = UINT64_MAX;
        CHECK(apta_result_serialize_to_stream(result, NULL, &output,
                                              &written) == APTA_ERROR_SOURCE);
        CHECK(written == 0u && output_state.flush_calls == 0u);
    }
    memset(&output_state, 0, sizeof(output_state));
    output_state.zero_write_call = 1u;
    written = UINT64_MAX;
    CHECK(apta_result_serialize_to_stream(result, NULL, &output, &written) ==
          APTA_ERROR_SOURCE);
    CHECK(written == 0u && output_state.flush_calls == 0u);
    for (fail_call = 1u; fail_call <= successful_seek_calls; ++fail_call) {
        memset(&output_state, 0, sizeof(output_state));
        output_state.fail_seek_call = fail_call;
        written = UINT64_MAX;
        CHECK(apta_result_serialize_to_stream(result, NULL, &output,
                                              &written) == APTA_ERROR_SOURCE);
        CHECK(written == 0u && output_state.flush_calls == 0u);
    }
    memset(&output_state, 0, sizeof(output_state));
    output_state.fail_flush_call = 1u;
    CHECK(apta_result_serialize_to_stream(result, NULL, &output, &written) ==
          APTA_ERROR_SOURCE);
    CHECK(written == 0u && output_state.flush_calls == 1u);

    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

static int parse_selected(memory_stream_t *fixture,
                          allocator_state_t *allocator_state,
                          apta_feature_mask_t requested,
                          const apta_result_t **result_out,
                          apta_context_t **context_out)
{
    uint8_t scratch[7];
    apta_input_stream_t input;
    apta_stream_parse_options_t options;
    if (!create_context(allocator_state, context_out))
        return 0;
    apta_input_stream_init(&input);
    input.user_data = fixture;
    input.read_at = memory_read_at;
    input.get_size = memory_get_size;
    apta_stream_parse_options_init(&options);
    options.requested_features = requested;
    options.scratch_buffer = scratch;
    options.scratch_buffer_size = sizeof(scratch);
    options.maximum_scratch_bytes = sizeof(scratch);
    return apta_result_parse_from_stream(*context_out, &options, &input,
                                         result_out) == APTA_STATUS_OK;
}

static int expect_parse_status(memory_stream_t *fixture,
                               const apta_stream_parse_options_t *options,
                               apta_status_t expected)
{
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_input_stream_t input;
    apta_status_t status;
    if (!create_context(NULL, &context))
        return 0;
    apta_input_stream_init(&input);
    input.user_data = fixture;
    input.read_at = memory_read_at;
    input.get_size = memory_get_size;
    status = apta_result_parse_from_stream(context, options, &input, &result);
    if (result != NULL)
        apta_result_release(result);
    if (apta_context_destroy(context) != APTA_STATUS_OK)
        return 0;
    return status == expected && result == NULL;
}

static int check_input_selection_and_limits(void)
{
    memory_stream_t fixture;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_feature_mask_t available = UINT64_MAX;
    apta_key_view_t key;
    apta_meter_view_t meter;
    apta_quality_view_t quality;
    apta_source_info_t source;
    allocator_state_t allocator_state = {0};
    CHECK(load_fixture(&fixture));
    fixture.maximum_transfer = 2u;
    CHECK(parse_selected(&fixture, &allocator_state, APTA_FEATURE_MUSICAL_KEY,
                         &result, &context));
    CHECK(fixture.maximum_request_seen <= 7u);
    CHECK(fixture.size_calls == 2u);
    CHECK(allocator_state.largest_allocation < 65536u);
    available = apta_result_get_available_features(result);
    CHECK(available == APTA_FEATURE_MUSICAL_KEY);
    apta_key_view_init(&key);
    apta_meter_view_init(&meter);
    apta_source_info_init(&source);
    CHECK(apta_result_get_key(result, NULL, &key) == APTA_STATUS_OK);
    CHECK(key.candidate_count == 2u);
    CHECK(apta_result_get_meter(result, NULL, &meter) ==
          APTA_STATUS_NOT_AVAILABLE);
    CHECK(apta_result_get_source_info(result, &source) == APTA_STATUS_OK);
    CHECK(source.total_frames == 96000u && source.sample_rate == 48000u);
    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(allocator_state.outstanding == 0u);

    fixture.read_calls = fixture.size_calls = 0u;
    fixture.maximum_request_seen = 0u;
    result = NULL;
    context = NULL;
    CHECK(parse_selected(&fixture, NULL,
                         APTA_FEATURE_MUSICAL_KEY |
                             APTA_FEATURE_CALIBRATED_QUALITY,
                         &result, &context));
    apta_quality_view_init(&quality);
    CHECK(apta_result_get_quality(result, APTA_FEATURE_MUSICAL_KEY, &quality) ==
          APTA_STATUS_OK);
    CHECK(apta_result_get_quality(result, APTA_FEATURE_METER_DOWNBEAT,
                                  &quality) == APTA_STATUS_NOT_AVAILABLE);
    apta_result_release(result);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);

    {
        apta_stream_parse_options_t options;
        uint8_t scratch[9];
        apta_stream_parse_options_init(&options);
        options.scratch_buffer = scratch;
        options.scratch_buffer_size = sizeof(scratch);
        options.maximum_scratch_bytes = sizeof(scratch);

        options.maximum_input_bytes = fixture.size - 1u;
        CHECK(
            expect_parse_status(&fixture, &options, APTA_ERROR_LIMIT_EXCEEDED));
        options.maximum_input_bytes = fixture.size;

        options.maximum_section_count = 1u;
        CHECK(
            expect_parse_status(&fixture, &options, APTA_ERROR_LIMIT_EXCEEDED));
        options.maximum_section_count = 64u;

        options.maximum_section_bytes = 1u;
        CHECK(
            expect_parse_status(&fixture, &options, APTA_ERROR_LIMIT_EXCEEDED));
        options.maximum_section_bytes = fixture.size;

        options.maximum_allocation_bytes = 1u;
        CHECK(
            expect_parse_status(&fixture, &options, APTA_ERROR_LIMIT_EXCEEDED));
        options.maximum_allocation_bytes = fixture.size * 16u;

        options.maximum_overview_spans = 1u;
        options.maximum_waveform_columns = 1u;
        options.scratch_buffer_size = sizeof(scratch) - 1u;
        CHECK(expect_parse_status(&fixture, &options,
                                  APTA_ERROR_INVALID_ARGUMENT));
    }

    return 0;
}

static int check_input_failures(void)
{
    memory_stream_t original;
    uint32_t fail_call;
    CHECK(load_fixture(&original));
    for (fail_call = 1u; fail_call <= 2u; ++fail_call) {
        memory_stream_t fixture = original;
        apta_stream_parse_options_t options;
        uint8_t scratch[17];
        apta_stream_parse_options_init(&options);
        options.scratch_buffer = scratch;
        options.scratch_buffer_size = sizeof(scratch);
        options.maximum_scratch_bytes = sizeof(scratch);
        fixture.fail_size_call = fail_call;
        CHECK(expect_parse_status(&fixture, &options, APTA_ERROR_SOURCE));
    }
    for (fail_call = 1u; fail_call <= 80u; ++fail_call) {
        memory_stream_t fixture = original;
        apta_context_t *context = NULL;
        const apta_result_t *result = NULL;
        apta_input_stream_t input;
        apta_stream_parse_options_t options;
        apta_status_t status;
        apta_input_stream_init(&input);
        input.user_data = &fixture;
        input.read_at = memory_read_at;
        input.get_size = memory_get_size;
        apta_stream_parse_options_init(&options);
        options.maximum_scratch_bytes = 11u;
        fixture.maximum_transfer = 11u;
        fixture.fail_read_call = fail_call;
        CHECK(create_context(NULL, &context));
        status =
            apta_result_parse_from_stream(context, &options, &input, &result);
        if (fail_call <= fixture.read_calls) {
            CHECK(status == APTA_ERROR_SOURCE && result == NULL);
        } else {
            CHECK(status == APTA_STATUS_OK && result != NULL);
        }
        if (result != NULL)
            apta_result_release(result);
        CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
        if (status == APTA_STATUS_OK)
            break;
    }

    {
        memory_stream_t fixture = original;
        apta_context_t *context = NULL;
        const apta_result_t *result = NULL;
        apta_input_stream_t input;
        apta_stream_parse_options_t options;
        apta_input_stream_init(&input);
        input.user_data = &fixture;
        input.read_at = memory_read_at;
        input.get_size = memory_get_size;
        apta_stream_parse_options_init(&options);
        options.maximum_scratch_bytes = 5u;
        fixture.zero_read_call = 1u;
        CHECK(create_context(NULL, &context));
        CHECK(apta_result_parse_from_stream(context, &options, &input,
                                            &result) == APTA_ERROR_SOURCE);
        CHECK(result == NULL);
        CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    }
    {
        memory_stream_t fixture = original;
        apta_context_t *context = NULL;
        const apta_result_t *result = NULL;
        apta_input_stream_t input;
        apta_stream_parse_options_t options;
        apta_input_stream_init(&input);
        input.user_data = &fixture;
        input.read_at = memory_read_at;
        input.get_size = memory_get_size;
        apta_stream_parse_options_init(&options);
        options.maximum_scratch_bytes = 5u;
        fixture.change_size_after_first = 1u;
        CHECK(create_context(NULL, &context));
        CHECK(
            apta_result_parse_from_stream(context, &options, &input, &result) ==
            APTA_ERROR_CORRUPT_DATA);
        CHECK(result == NULL);
        CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    }
    return 0;
}

static int check_hardened_materialization(void)
{
    memory_stream_t fixture;
    apta_context_t *context = NULL;
    const apta_result_t *result = NULL;
    apta_stream_parse_options_t options;
    uint8_t scratch[11];
    uint8_t *entry;
    uint64_t section_offset;
    uint64_t section_size;
    uint8_t *payload;
    uint64_t column_offset;
    uint64_t span_offset;
    uint32_t packed_index;
    uint8_t *column_flags;

    CHECK(load_fixture(&fixture));
    entry = find_entry(&fixture, "WOVR");
    CHECK(entry != NULL);
    section_offset = get_u64(entry + 8u);
    section_size = get_u64(entry + 16u);
    payload = fixture.bytes + (size_t)section_offset;
    span_offset = get_u64(payload + 24u);
    column_offset = get_u64(payload + 32u);
    packed_index = get_u32(payload + (size_t)span_offset + 24u);
    column_flags = payload + (size_t)column_offset + packed_index * 10u + 9u;
    CHECK((*column_flags & APTA_WAVEFORM_COLUMN_VALID) != 0u);
    *column_flags &= (uint8_t)~APTA_WAVEFORM_COLUMN_VALID;
    put_u32(entry + 32u, crc32c(payload, section_size));

    CHECK(create_context(NULL, &context));
    CHECK(apta_result_parse(context, NULL, fixture.bytes, (size_t)fixture.size,
                            &result) == APTA_ERROR_CORRUPT_DATA);
    CHECK(result == NULL);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);

    apta_stream_parse_options_init(&options);
    options.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    options.scratch_buffer = scratch;
    options.scratch_buffer_size = sizeof(scratch);
    options.maximum_scratch_bytes = sizeof(scratch);
    CHECK(expect_parse_status(&fixture, &options, APTA_ERROR_CORRUPT_DATA));
    return 0;
}

static int check_allocation_failure_rollback(void)
{
    memory_stream_t original;
    uint32_t total_calls = 0u;
    uint32_t fail_at;
    CHECK(load_fixture(&original));
    for (fail_at = 0u; fail_at <= total_calls;
         fail_at = fail_at == 0u ? 2u : fail_at + 1u) {
        memory_stream_t fixture = original;
        allocator_state_t state = {0u, fail_at, 0u, 0u};
        apta_context_t *context = NULL;
        const apta_result_t *result = NULL;
        apta_input_stream_t input;
        apta_stream_parse_options_t options;
        apta_status_t status;
        apta_input_stream_init(&input);
        input.user_data = &fixture;
        input.read_at = memory_read_at;
        input.get_size = memory_get_size;
        apta_stream_parse_options_init(&options);
        options.maximum_scratch_bytes = 13u;
        CHECK(create_context(&state, &context));
        status =
            apta_result_parse_from_stream(context, &options, &input, &result);
        if (fail_at == 0u) {
            CHECK(status == APTA_STATUS_OK && result != NULL);
            total_calls = state.allocation_calls;
            CHECK(total_calls >= 5u);
        } else {
            CHECK(status == APTA_ERROR_OUT_OF_MEMORY && result == NULL);
        }
        if (result != NULL)
            apta_result_release(result);
        CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
        CHECK(state.outstanding == 0u);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "output") == 0) {
        CHECK(check_initializers() == 0);
        CHECK(check_output_stream() == 0);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "input") == 0) {
        CHECK(check_input_selection_and_limits() == 0);
        CHECK(check_input_failures() == 0);
        CHECK(check_hardened_materialization() == 0);
        CHECK(check_allocation_failure_rollback() == 0);
        return 0;
    }
    CHECK(check_initializers() == 0);
    CHECK(check_output_stream() == 0);
    CHECK(check_input_selection_and_limits() == 0);
    CHECK(check_input_failures() == 0);
    CHECK(check_hardened_materialization() == 0);
    CHECK(check_allocation_failure_rollback() == 0);
    return 0;
}
