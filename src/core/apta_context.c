// SPDX-License-Identifier: Apache-2.0
#include "apta_internal.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

static int apta_internal_allocator_is_valid(
    const apta_allocator_t *allocator)
{
    if (allocator->allocate == NULL && allocator->deallocate == NULL) {
        return 1;
    }

    return apta_internal_validate_struct(
               allocator,
               sizeof(*allocator),
               allocator->struct_size,
               allocator->api_version) &&
           allocator->allocate != NULL &&
           allocator->deallocate != NULL;
}

static int apta_internal_logger_is_valid(
    const apta_logger_t *logger)
{
    return logger->write == NULL ||
           apta_internal_validate_struct(
               logger,
               sizeof(*logger),
               logger->struct_size,
               logger->api_version);
}

static int apta_internal_clock_is_valid(
    const apta_clock_t *clock)
{
    return clock->monotonic_time_ns == NULL ||
           apta_internal_validate_struct(
               clock,
               sizeof(*clock),
               clock->struct_size,
               clock->api_version);
}

static void *apta_internal_allocate_context_object(
    const apta_context_config_t *config)
{
    if (config->allocator.allocate != NULL) {
        return config->allocator.allocate(
            config->allocator.user_data,
            sizeof(apta_context_t),
            alignof(apta_context_t),
            APTA_MEMORY_PERSISTENT);
    }

    return malloc(sizeof(apta_context_t));
}

static void apta_internal_deallocate_context_object(
    apta_context_t *context)
{
    apta_allocator_t allocator;

    allocator = context->allocator;
    if (allocator.deallocate != NULL) {
        allocator.deallocate(allocator.user_data, context);
    } else {
        free(context);
    }
}

apta_status_t APTA_CALL apta_context_create(
    const apta_context_config_t *config,
    apta_context_t **context_out)
{
    apta_context_t *context;

    if (context_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    *context_out = NULL;

    if (config == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    if (!apta_internal_validate_struct(
            config,
            sizeof(*config),
            config->struct_size,
            config->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    if (!apta_internal_allocator_is_valid(&config->allocator) ||
        !apta_internal_logger_is_valid(&config->logger) ||
        !apta_internal_clock_is_valid(&config->clock)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    /* The first implementation stage exposes no analysis capability yet. */
    if (config->requested_capabilities != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }

    context = (apta_context_t *)apta_internal_allocate_context_object(config);
    if (context == NULL) {
        return APTA_ERROR_OUT_OF_MEMORY;
    }

    memset(context, 0, sizeof(*context));
    context->allocator = config->allocator;
    context->logger = config->logger;
    context->clock = config->clock;
    context->capabilities = 0u;
    context->memory_limit_bytes = config->memory_limit_bytes;
    context->flags = config->flags;

    atomic_init(&context->allocated_bytes, 0u);
    atomic_init(&context->session_count, 0u);
    atomic_init(&context->result_count, 0u);
    atomic_init(&context->lineage_counter, 0u);

    *context_out = context;
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_context_destroy(apta_context_t *context)
{
    if (context == NULL) {
        return APTA_STATUS_OK;
    }

    if (atomic_load_explicit(&context->session_count, memory_order_acquire) != 0u ||
        atomic_load_explicit(&context->result_count, memory_order_acquire) != 0u ||
        atomic_load_explicit(&context->allocated_bytes, memory_order_acquire) != 0u) {
        return APTA_ERROR_BUSY;
    }

    apta_internal_deallocate_context_object(context);
    return APTA_STATUS_OK;
}

apta_feature_mask_t APTA_CALL apta_context_get_capabilities(
    const apta_context_t *context)
{
    return context != NULL ? context->capabilities : 0u;
}
