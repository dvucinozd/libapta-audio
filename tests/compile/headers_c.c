// SPDX-License-Identifier: Apache-2.0
#include <apta/apta.h>

static apta_status_t APTA_CALL compile_write(
    void *user_data, const void *data, uint64_t requested,
    uint64_t *written_out)
{
    (void)user_data;
    (void)data;
    *written_out = requested;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL compile_seek(void *user_data, uint64_t position)
{
    (void)user_data;
    (void)position;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL compile_flush(void *user_data)
{
    (void)user_data;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL compile_read_at(
    void *user_data, uint64_t offset, void *data, uint64_t requested,
    uint64_t *read_out)
{
    (void)user_data;
    (void)offset;
    (void)data;
    *read_out = requested;
    return APTA_STATUS_OK;
}

static apta_status_t APTA_CALL compile_get_size(
    void *user_data, uint64_t *size_out)
{
    (void)user_data;
    *size_out = 0u;
    return APTA_STATUS_OK;
}
#include <apta/desktop/apta_decoder.h>
#include <apta/desktop/apta_file.h>
#include <apta/desktop/apta_posix_file.h>

int main(void)
{
    apta_output_stream_t output;
    apta_input_stream_t input;
    apta_stream_parse_options_t stream_options;

    apta_output_stream_init(&output);
    output.write = compile_write;
    output.seek = compile_seek;
    output.flush = compile_flush;
    apta_input_stream_init(&input);
    input.read_at = compile_read_at;
    input.get_size = compile_get_size;
    apta_stream_parse_options_init(&stream_options);
    (void)apta_result_serialize_to_stream(NULL, NULL, &output, NULL);
    (void)apta_result_parse_from_stream(NULL, &stream_options, &input, NULL);
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_focus_t focus;
    apta_work_budget_t budget;
    apta_result_info_t result_info;

    apta_context_config_init(&context_config);
    apta_session_config_init(&session_config);
    apta_focus_init(&focus);
    apta_work_budget_init(&budget);

    result_info.struct_size = (uint32_t)sizeof(result_info);
    result_info.api_version = APTA_API_VERSION;

    return (APTA_API_VERSION == 0u) ? 1 : 0;
}
