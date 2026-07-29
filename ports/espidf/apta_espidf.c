// SPDX-License-Identifier: Apache-2.0
#include <apta/apta_espidf.h>

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#if defined(CONFIG_APTA_ESP_DSP_BACKEND) && CONFIG_APTA_ESP_DSP_BACKEND
#include "dsps_dotprod.h"
#endif

static const char *apta_espidf_log_tag(const apta_espidf_port_t *port)
{
    return port != NULL && port->log_tag != NULL ? port->log_tag : "libapta";
}

static int apta_espidf_port_is_valid(const apta_espidf_port_t *port)
{
    size_t index;

    if (port == NULL || port->struct_size < sizeof(*port) ||
        port->api_version != APTA_API_VERSION ||
        (port->flags & ~(APTA_ESP_IDF_PORT_FLAG_LOG_TO_ESP_LOG |
                         APTA_ESP_IDF_PORT_FLAG_STRICT_MEMORY_CAPS |
                         APTA_ESP_IDF_PORT_FLAG_PREFER_SPIRAM_LARGE)) != 0u) {
        return 0;
    }
    for (index = 0u; index < sizeof(port->reserved32) / sizeof(port->reserved32[0]); ++index) {
        if (port->reserved32[index] != 0u) {
            return 0;
        }
    }
    for (index = 0u; index < sizeof(port->reserved64) / sizeof(port->reserved64[0]); ++index) {
        if (port->reserved64[index] != 0u) {
            return 0;
        }
    }
    return port->default_caps != 0u;
}

static uint32_t apta_espidf_caps_for_flags(
    const apta_espidf_port_t *port,
    apta_memory_flags_t flags)
{
    if ((flags & APTA_MEMORY_DMA) != 0u) {
        return port->dma_caps;
    }
    if ((flags & APTA_MEMORY_FAST) != 0u) {
        return port->fast_caps;
    }
    if ((flags & APTA_MEMORY_LARGE) != 0u) {
        return port->large_caps;
    }
    if ((flags & APTA_MEMORY_PERSISTENT) != 0u) {
        return port->persistent_caps;
    }
    if ((flags & APTA_MEMORY_TEMPORARY) != 0u) {
        return port->temporary_caps;
    }
    return port->default_caps;
}

static void *APTA_CALL apta_espidf_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    apta_espidf_port_t *port = (apta_espidf_port_t *)user_data;
    uint32_t caps;
    void *memory;

    if (!apta_espidf_port_is_valid(port) || size == 0u ||
        alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        return NULL;
    }

    caps = apta_espidf_caps_for_flags(port, flags);
    memory = heap_caps_aligned_alloc(alignment, size, caps);
    if (memory == NULL &&
        (port->flags & APTA_ESP_IDF_PORT_FLAG_STRICT_MEMORY_CAPS) == 0u &&
        caps != port->default_caps) {
        memory = heap_caps_aligned_alloc(alignment, size, port->default_caps);
    }
    return memory;
}

static void APTA_CALL apta_espidf_deallocate(void *user_data, void *memory)
{
    (void)user_data;
    heap_caps_free(memory);
}

static void APTA_CALL apta_espidf_log_write(
    void *user_data,
    apta_log_level_t level,
    uint32_t diagnostic_code,
    const char *message)
{
    const apta_espidf_port_t *port = (const apta_espidf_port_t *)user_data;
    esp_log_level_t esp_level;

    if (message == NULL) {
        message = "";
    }
    switch (level) {
        case APTA_LOG_ERROR:
            esp_level = ESP_LOG_ERROR;
            break;
        case APTA_LOG_WARN:
            esp_level = ESP_LOG_WARN;
            break;
        case APTA_LOG_INFO:
            esp_level = ESP_LOG_INFO;
            break;
        case APTA_LOG_DEBUG:
            esp_level = ESP_LOG_DEBUG;
            break;
        default:
            esp_level = ESP_LOG_VERBOSE;
            break;
    }
    esp_log_write(
        esp_level,
        apta_espidf_log_tag(port),
        "[%" PRIu32 "] %s\n",
        diagnostic_code,
        message);
}

void APTA_CALL apta_espidf_port_init(apta_espidf_port_t *port)
{
    if (port == NULL) {
        return;
    }
    *port = (apta_espidf_port_t){0};
    port->struct_size = (uint32_t)sizeof(*port);
    port->api_version = APTA_API_VERSION;
    port->flags = APTA_ESP_IDF_PORT_FLAG_LOG_TO_ESP_LOG |
                  APTA_ESP_IDF_PORT_FLAG_PREFER_SPIRAM_LARGE;
    port->default_caps = MALLOC_CAP_8BIT;
    port->fast_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    port->large_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    port->persistent_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    port->temporary_caps = MALLOC_CAP_8BIT;
    port->dma_caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    port->log_tag = "libapta";
}

apta_status_t APTA_CALL apta_espidf_bind_context_config(
    apta_espidf_port_t *port,
    apta_context_config_t *context_config)
{
    if (!apta_espidf_port_is_valid(port) || context_config == NULL ||
        context_config->struct_size < sizeof(*context_config) ||
        context_config->api_version != APTA_API_VERSION) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    apta_allocator_init(&context_config->allocator);
    context_config->allocator.user_data = port;
    context_config->allocator.allocate = apta_espidf_allocate;
    context_config->allocator.deallocate = apta_espidf_deallocate;
    context_config->allocator.reallocate = NULL;

    apta_clock_init(&context_config->clock);
    context_config->clock.user_data = port;
    context_config->clock.monotonic_time_ns = apta_espidf_monotonic_time_ns;

    apta_logger_init(&context_config->logger);
    if ((port->flags & APTA_ESP_IDF_PORT_FLAG_LOG_TO_ESP_LOG) != 0u) {
        context_config->logger.user_data = port;
        context_config->logger.write = apta_espidf_log_write;
    }
    return APTA_STATUS_OK;
}

uint64_t APTA_CALL apta_espidf_monotonic_time_ns(void *user_data)
{
    int64_t microseconds;
    (void)user_data;

    microseconds = esp_timer_get_time();
    if (microseconds <= 0) {
        return 0u;
    }
    if ((uint64_t)microseconds > UINT64_MAX / UINT64_C(1000)) {
        return UINT64_MAX;
    }
    return (uint64_t)microseconds * UINT64_C(1000);
}

uint32_t APTA_CALL apta_espidf_dsp_backend(void)
{
#if defined(CONFIG_APTA_ESP_DSP_BACKEND) && CONFIG_APTA_ESP_DSP_BACKEND
    return APTA_ESP_IDF_DSP_BACKEND_ESP_DSP;
#else
    return APTA_ESP_IDF_DSP_BACKEND_SCALAR;
#endif
}

apta_status_t APTA_CALL apta_espidf_dot_product_f32(
    const float *left,
    const float *right,
    size_t count,
    float *result_out)
{
    size_t index;
    float result = 0.0f;

    if (left == NULL || right == NULL || result_out == NULL ||
        count == 0u || count > (size_t)INT_MAX) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
#if defined(CONFIG_APTA_ESP_DSP_BACKEND) && CONFIG_APTA_ESP_DSP_BACKEND
    if (dsps_dotprod_f32(left, right, result_out, (int)count) != ESP_OK) {
        return APTA_ERROR_INTERNAL;
    }
#else
    for (index = 0u; index < count; ++index) {
        result += left[index] * right[index];
    }
    *result_out = result;
#endif
    return APTA_STATUS_OK;
}
