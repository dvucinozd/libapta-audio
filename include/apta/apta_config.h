// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_CONFIG_H
#define APTA_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include <apta/apta_errors.h>
#include <apta/apta_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    void *user_data;

    void *(APTA_CALL *allocate)(
        void *user_data,
        size_t size,
        size_t alignment,
        apta_memory_flags_t flags);

    void (APTA_CALL *deallocate)(
        void *user_data,
        void *memory);

    void *(APTA_CALL *reallocate)(
        void *user_data,
        void *memory,
        size_t new_size,
        size_t alignment,
        apta_memory_flags_t flags);
} apta_allocator_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    void *user_data;

    void (APTA_CALL *write)(
        void *user_data,
        apta_log_level_t level,
        uint32_t diagnostic_code,
        const char *message);
} apta_logger_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    void *user_data;

    uint64_t (APTA_CALL *monotonic_time_ns)(void *user_data);
} apta_clock_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_allocator_t allocator;
    apta_logger_t logger;
    apta_clock_t clock;

    apta_feature_mask_t requested_capabilities;
    uint64_t memory_limit_bytes;

    uint32_t flags;
    uint32_t reserved32[5];
    uint64_t reserved64[4];
} apta_context_config_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_input_mode_t input_mode;
    uint32_t source_sample_rate;

    uint16_t channel_count;
    uint16_t reserved16;

    apta_sample_format_t sample_format;
    apta_channel_layout_t channel_layout;

    apta_source_frame_t total_frames;
    apta_feature_mask_t requested_features;
    uint64_t memory_budget_bytes;

    void *static_workspace;
    size_t static_workspace_size;

    uint32_t flags;
    uint32_t reserved32[5];
    uint64_t reserved64[4];
} apta_session_config_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    size_t minimum_bytes;
    size_t recommended_bytes;
    size_t required_alignment;

    uint32_t flags;
    uint32_t reserved32[3];
} apta_memory_requirements_t;

APTA_API void APTA_CALL
apta_allocator_init(apta_allocator_t *allocator);

APTA_API void APTA_CALL
apta_logger_init(apta_logger_t *logger);

APTA_API void APTA_CALL
apta_clock_init(apta_clock_t *clock);

APTA_API void APTA_CALL
apta_context_config_init(apta_context_config_t *config);

APTA_API void APTA_CALL
apta_session_config_init(apta_session_config_t *config);

APTA_API apta_status_t APTA_CALL
apta_query_memory_requirements(
    const apta_session_config_t *config,
    apta_memory_requirements_t *requirements_out);

APTA_API apta_status_t APTA_CALL
apta_context_create(
    const apta_context_config_t *config,
    apta_context_t **context_out);

APTA_API apta_status_t APTA_CALL
apta_context_destroy(apta_context_t *context);

APTA_API apta_feature_mask_t APTA_CALL
apta_context_get_capabilities(const apta_context_t *context);

APTA_API apta_status_t APTA_CALL
apta_session_create(
    apta_context_t *context,
    const apta_session_config_t *config,
    apta_session_t **session_out);

APTA_API apta_status_t APTA_CALL
apta_session_destroy(apta_session_t *session);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_CONFIG_H */
