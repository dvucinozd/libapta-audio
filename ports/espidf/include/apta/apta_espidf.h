// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_ESP_IDF_H
#define APTA_ESP_IDF_H

#include <stddef.h>
#include <stdint.h>

#include <apta/apta.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APTA_ESP_IDF_PORT_FLAG_LOG_TO_ESP_LOG        (1u << 0)
#define APTA_ESP_IDF_PORT_FLAG_STRICT_MEMORY_CAPS    (1u << 1)
#define APTA_ESP_IDF_PORT_FLAG_PREFER_SPIRAM_LARGE   (1u << 2)

#define APTA_ESP_IDF_DSP_BACKEND_SCALAR  0u
#define APTA_ESP_IDF_DSP_BACKEND_ESP_DSP 1u

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t flags;
    uint32_t default_caps;
    uint32_t fast_caps;
    uint32_t large_caps;
    uint32_t persistent_caps;
    uint32_t temporary_caps;
    uint32_t dma_caps;

    const char *log_tag;

    uint32_t reserved32[4];
    uint64_t reserved64[4];
} apta_espidf_port_t;

APTA_API void APTA_CALL
apta_espidf_port_init(apta_espidf_port_t *port);

APTA_API apta_status_t APTA_CALL
apta_espidf_bind_context_config(
    apta_espidf_port_t *port,
    apta_context_config_t *context_config);

APTA_API uint64_t APTA_CALL
apta_espidf_monotonic_time_ns(void *user_data);

APTA_API uint32_t APTA_CALL
apta_espidf_dsp_backend(void);

APTA_API apta_status_t APTA_CALL
apta_espidf_dot_product_f32(
    const float *left,
    const float *right,
    size_t count,
    float *result_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_ESP_IDF_H */
