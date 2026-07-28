// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <stdalign.h>
#include <string.h>

#define APTA_INIT_STRUCT(value)              \
    do {                                     \
        memset((value), 0, sizeof(*(value))); \
        (value)->struct_size = (uint32_t)sizeof(*(value)); \
        (value)->api_version = APTA_API_VERSION;            \
    } while (0)

void APTA_CALL apta_allocator_init(apta_allocator_t *allocator)
{
    if (allocator != NULL) {
        APTA_INIT_STRUCT(allocator);
    }
}

void APTA_CALL apta_logger_init(apta_logger_t *logger)
{
    if (logger != NULL) {
        APTA_INIT_STRUCT(logger);
    }
}

void APTA_CALL apta_clock_init(apta_clock_t *clock)
{
    if (clock != NULL) {
        APTA_INIT_STRUCT(clock);
    }
}

void APTA_CALL apta_context_config_init(apta_context_config_t *config)
{
    if (config != NULL) {
        APTA_INIT_STRUCT(config);
        apta_allocator_init(&config->allocator);
        apta_logger_init(&config->logger);
        apta_clock_init(&config->clock);
    }
}

void APTA_CALL apta_session_config_init(apta_session_config_t *config)
{
    if (config != NULL) {
        APTA_INIT_STRUCT(config);
        config->input_mode = APTA_INPUT_MODE_PUSH;
        config->total_frames = APTA_TOTAL_FRAMES_UNKNOWN;
    }
}

void APTA_CALL apta_pcm_block_init(apta_pcm_block_t *block)
{
    if (block != NULL) {
        APTA_INIT_STRUCT(block);
    }
}

void APTA_CALL apta_pcm_source_init(apta_pcm_source_t *source)
{
    if (source != NULL) {
        APTA_INIT_STRUCT(source);
    }
}

void APTA_CALL apta_focus_init(apta_focus_t *focus)
{
    if (focus != NULL) {
        APTA_INIT_STRUCT(focus);
        focus->priority = APTA_PRIORITY_INTERACTIVE;
    }
}

void APTA_CALL apta_region_request_init(apta_region_request_t *request)
{
    if (request != NULL) {
        APTA_INIT_STRUCT(request);
        request->priority = APTA_PRIORITY_NORMAL;
        request->range.struct_size = (uint32_t)sizeof(request->range);
        request->range.api_version = APTA_API_VERSION;
    }
}

void APTA_CALL apta_pcm_request_init(apta_pcm_request_t *request)
{
    if (request != NULL) {
        APTA_INIT_STRUCT(request);
        request->range.struct_size = (uint32_t)sizeof(request->range);
        request->range.api_version = APTA_API_VERSION;
    }
}

void APTA_CALL apta_work_budget_init(apta_work_budget_t *budget)
{
    if (budget != NULL) {
        APTA_INIT_STRUCT(budget);
    }
}

apta_status_t APTA_CALL apta_query_memory_requirements(
    const apta_session_config_t *config,
    apta_memory_requirements_t *requirements_out)
{
    if (config == NULL || requirements_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (!apta_internal_validate_struct(
            requirements_out,
            sizeof(*requirements_out),
            requirements_out->struct_size,
            requirements_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    requirements_out->minimum_bytes = 0u;
    requirements_out->recommended_bytes = 0u;
    requirements_out->required_alignment = alignof(max_align_t);
    requirements_out->flags = 0u;
    memset(requirements_out->reserved32, 0, sizeof(requirements_out->reserved32));

    return APTA_STATUS_OK;
}
