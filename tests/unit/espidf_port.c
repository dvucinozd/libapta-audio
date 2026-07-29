// SPDX-License-Identifier: Apache-2.0
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <apta/apta.h>
#include <apta/apta_espidf.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static uint32_t last_caps;
static uint32_t fail_caps;
static uint32_t allocation_calls;
static uint32_t free_calls;
static int64_t timer_microseconds;
static uint32_t log_calls;
static esp_log_level_t last_log_level;
static char last_log_tag[32];

void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
    size_t rounded;
    void *memory;

    last_caps = caps;
    allocation_calls += 1u;
    if (caps == fail_caps) {
        return NULL;
    }
    rounded = (size + alignment - 1u) & ~(alignment - 1u);
    memory = aligned_alloc(alignment, rounded);
    return memory;
}

void heap_caps_aligned_free(void *memory)
{
    if (memory != NULL) {
        free_calls += 1u;
        free(memory);
    }
}

size_t heap_caps_get_free_size(uint32_t caps)
{
    (void)caps;
    return 1024u * 1024u;
}

int64_t esp_timer_get_time(void)
{
    return timer_microseconds;
}

void esp_log_write(
    esp_log_level_t level,
    const char *tag,
    const char *format,
    ...)
{
    va_list args;
    (void)format;
    va_start(args, format);
    va_end(args);
    log_calls += 1u;
    last_log_level = level;
    snprintf(last_log_tag, sizeof(last_log_tag), "%s", tag != NULL ? tag : "");
}

int main(void)
{
    apta_espidf_port_t port;
    apta_context_config_t config;
    apta_context_t *context = NULL;
    void *memory;
    float left[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float right[4] = {4.0f, 3.0f, 2.0f, 1.0f};
    float dot = 0.0f;
    uint32_t before;

    apta_espidf_port_init(&port);
    CHECK(port.struct_size == sizeof(port));
    CHECK(port.api_version == APTA_API_VERSION);
    CHECK(port.default_caps == MALLOC_CAP_8BIT);
    CHECK(port.fast_caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    CHECK(port.dma_caps ==
          (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    apta_context_config_init(&config);
    config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    port.log_tag = "apta-test";
    CHECK(apta_espidf_bind_context_config(&port, &config) == APTA_STATUS_OK);
    CHECK(config.allocator.allocate != NULL);
    CHECK(config.allocator.deallocate != NULL);
    CHECK(config.allocator.reallocate == NULL);
    CHECK(config.clock.monotonic_time_ns != NULL);
    CHECK(config.logger.write != NULL);

    timer_microseconds = 1234567;
    CHECK(config.clock.monotonic_time_ns(config.clock.user_data) ==
          UINT64_C(1234567000));
    timer_microseconds = -1;
    CHECK(config.clock.monotonic_time_ns(config.clock.user_data) == 0u);

    memory = config.allocator.allocate(
        config.allocator.user_data,
        73u,
        32u,
        APTA_MEMORY_FAST);
    CHECK(memory != NULL);
    CHECK(((uintptr_t)memory & 31u) == 0u);
    CHECK(last_caps == port.fast_caps);
    config.allocator.deallocate(config.allocator.user_data, memory);

    fail_caps = port.large_caps;
    before = allocation_calls;
    memory = config.allocator.allocate(
        config.allocator.user_data,
        64u,
        16u,
        APTA_MEMORY_LARGE);
    CHECK(memory != NULL);
    CHECK(allocation_calls == before + 2u);
    CHECK(last_caps == port.default_caps);
    config.allocator.deallocate(config.allocator.user_data, memory);

    port.flags |= APTA_ESP_IDF_PORT_FLAG_STRICT_MEMORY_CAPS;
    before = allocation_calls;
    memory = config.allocator.allocate(
        config.allocator.user_data,
        64u,
        16u,
        APTA_MEMORY_LARGE);
    CHECK(memory == NULL);
    CHECK(allocation_calls == before + 1u);
    port.flags &= ~APTA_ESP_IDF_PORT_FLAG_STRICT_MEMORY_CAPS;
    fail_caps = 0u;

    config.logger.write(
        config.logger.user_data,
        APTA_LOG_WARN,
        17u,
        "warning");
    CHECK(log_calls == 1u);
    CHECK(last_log_level == ESP_LOG_WARN);
    CHECK(strcmp(last_log_tag, "apta-test") == 0);

    CHECK(apta_espidf_dsp_backend() == APTA_ESP_IDF_DSP_BACKEND_SCALAR);
    CHECK(apta_espidf_dot_product_f32(left, right, 4u, &dot) ==
          APTA_STATUS_OK);
    CHECK(dot == 20.0f);
    CHECK(apta_espidf_dot_product_f32(NULL, right, 4u, &dot) ==
          APTA_ERROR_INVALID_ARGUMENT);

    CHECK(apta_context_create(&config, &context) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;
    CHECK(allocation_calls == free_calls + 2u); /* fallback and strict failures */

    port.reserved32[0] = 1u;
    apta_context_config_init(&config);
    CHECK(apta_espidf_bind_context_config(&port, &config) ==
          APTA_ERROR_INVALID_ARGUMENT);
    return 0;
}
