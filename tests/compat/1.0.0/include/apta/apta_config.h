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

#define APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS              (1u << 0)
#define APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING (1u << 1)

#define APTA_MEMORY_REQUIREMENTS_INCLUDE_RESULT_POOL (1u << 0)

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

    /*
     * Frames summarised into one overview waveform column, for level 0.
     *
     * Zero selects the library default, so a config produced by
     * apta_session_config_init() behaves exactly as before. A non-zero value
     * must be a power of two between 64 and 65536; anything else is rejected
     * at apta_session_create() with APTA_ERROR_INVALID_ARGUMENT.
     *
     * Lower values raise the horizontal resolution a zoom UI can draw and
     * raise the static workspace proportionally, because the accumulator array
     * holds one entry per column across the whole track. Ask
     * apta_query_workspace_requirements() rather than scaling a published
     * figure by hand.
     *
     * The value reached is reported back in
     * apta_waveform_level_info_t.frames_per_column.
     *
     * Taken from the reserved space, so the struct size and therefore the ABI
     * are unchanged.
     */
    uint32_t overview_frames_per_column;

    /*
     * Optional portable source identity used by result inspection,
     * serialization and checkpoint-seeding policy. Kind NONE requires all
     * fingerprint bytes to be zero. The two defined non-zero kinds map to the
     * version-1 container header.
     *
     * These fields consume the pre-1.0 reserved tail; sizeof and alignment of
     * apta_session_config_t are unchanged.
     */
    apta_source_fingerprint_kind_t source_fingerprint_kind;
    uint32_t reserved32[3];
    uint8_t source_fingerprint[APTA_SOURCE_FINGERPRINT_SIZE];
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

/*
 * Size of the static workspace this configuration requires, for hosts that
 * supply apta_session_config_t.static_workspace instead of an allocator.
 *
 * minimum_bytes is what apta_session_create() enforces; a buffer at least that
 * large completes the analysis the configuration describes.
 * recommended_bytes adds headroom for allocator slack. required_alignment is
 * the alignment the buffer must satisfy.
 *
 * config->total_frames must be set: the overview accumulators scale with track
 * duration and dominate the figure for anything longer than a few seconds. The
 * fields consulted are total_frames and requested_features; static_workspace
 * and static_workspace_size are ignored, so this can be called before a buffer
 * exists.
 *
 * requirements_out must be initialized with apta_memory_requirements_init()
 * first, as for apta_query_memory_requirements().
 */
APTA_API apta_status_t APTA_CALL
apta_query_workspace_requirements(
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
