// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_TYPES_H
#define APTA_TYPES_H

#include <stdint.h>

#include <apta/apta_version.h>

#if defined(APTA_INTERNAL_LAYER)
#  define APTA_API
#elif defined(_WIN32) && defined(APTA_SHARED)
#  if defined(APTA_BUILDING_LIBRARY)
#    define APTA_API __declspec(dllexport)
#  else
#    define APTA_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(APTA_SHARED)
#  define APTA_API __attribute__((visibility("default")))
#else
#  define APTA_API
#endif

#if defined(_MSC_VER)
#  define APTA_DEPRECATED(message) __declspec(deprecated(message))
#elif defined(__GNUC__) || defined(__clang__)
#  define APTA_DEPRECATED(message) __attribute__((deprecated(message)))
#else
#  define APTA_DEPRECATED(message)
#endif

#if defined(_WIN32) && !defined(_WIN64) && defined(APTA_USE_STDCALL)
#  define APTA_CALL __stdcall
#else
#  define APTA_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct apta_context apta_context_t;
typedef struct apta_session apta_session_t;
typedef struct apta_result apta_result_t;

typedef uint64_t apta_source_frame_t;
typedef uint64_t apta_generation_t;
typedef uint64_t apta_feature_mask_t;
typedef uint8_t apta_confidence_value_t;

typedef uint32_t apta_sample_format_t;
typedef uint32_t apta_channel_layout_t;
typedef uint32_t apta_input_mode_t;
typedef uint32_t apta_session_state_t;
typedef uint32_t apta_feature_state_t;
typedef uint32_t apta_request_state_t;
typedef uint32_t apta_grid_representation_t;
typedef uint32_t apta_tempo_relation_t;
typedef uint32_t apta_memory_flags_t;
typedef uint32_t apta_log_level_t;
typedef uint32_t apta_diagnostic_severity_t;
typedef uint32_t apta_tempo_millibpm_t;
typedef int64_t apta_beat_ordinal_t;
typedef uint32_t apta_source_fingerprint_kind_t;

#define APTA_SOURCE_FINGERPRINT_NONE                       0u
#define APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256     1u
#define APTA_SOURCE_FINGERPRINT_SHA256_SOURCE_OBJECT_BYTES 2u
#define APTA_SOURCE_FINGERPRINT_SIZE                       32u

#define APTA_TOTAL_FRAMES_UNKNOWN UINT64_MAX

#define APTA_CONFIDENCE_MIN       0u
#define APTA_CONFIDENCE_MAX       100u
#define APTA_CONFIDENCE_UNKNOWN   255u

#define APTA_FEATURE_WAVEFORM_OVERVIEW  (UINT64_C(1) << 0)
#define APTA_FEATURE_WAVEFORM_DETAIL    (UINT64_C(1) << 1)
#define APTA_FEATURE_WAVEFORM_3BAND     (UINT64_C(1) << 2)
#define APTA_FEATURE_BPM                (UINT64_C(1) << 3)
#define APTA_FEATURE_LOCAL_BEATGRID     (UINT64_C(1) << 4)
#define APTA_FEATURE_GLOBAL_BEATGRID    (UINT64_C(1) << 5)
#define APTA_FEATURE_DYNAMIC_TEMPO      (UINT64_C(1) << 6)
#define APTA_FEATURE_CONFIDENCE         (UINT64_C(1) << 7)
#define APTA_FEATURE_GRID_LOCKING       (UINT64_C(1) << 8)

#define APTA_SAMPLE_S16_NATIVE_INTERLEAVED  1u
#define APTA_SAMPLE_S24_3LE_INTERLEAVED     2u
#define APTA_SAMPLE_S32_NATIVE_INTERLEAVED  3u
#define APTA_SAMPLE_F32_NATIVE_INTERLEAVED  4u
#define APTA_SAMPLE_F32_NATIVE_PLANAR       5u

#define APTA_CHANNEL_LAYOUT_UNSPECIFIED 0u
#define APTA_CHANNEL_LAYOUT_MONO        1u
#define APTA_CHANNEL_LAYOUT_STEREO      2u

#define APTA_INPUT_MODE_PUSH 1u
#define APTA_INPUT_MODE_PULL 2u

#define APTA_SESSION_CREATED    0u
#define APTA_SESSION_ACTIVE     1u
#define APTA_SESSION_DRAINING   2u
#define APTA_SESSION_COMPLETED  3u
#define APTA_SESSION_CANCELLED  4u
#define APTA_SESSION_FAILED     5u

#define APTA_FEATURE_ABSENT       0u
#define APTA_FEATURE_PARTIAL      1u
#define APTA_FEATURE_PROVISIONAL  2u
#define APTA_FEATURE_STABLE       3u
#define APTA_FEATURE_FINAL        4u
#define APTA_FEATURE_FAILED       5u

#define APTA_REQUEST_QUEUED               0u
#define APTA_REQUEST_WAITING_FOR_PCM      1u
#define APTA_REQUEST_RUNNABLE             2u
#define APTA_REQUEST_PARTIALLY_SATISFIED  3u
#define APTA_REQUEST_SATISFIED            4u
#define APTA_REQUEST_CANCELLED            5u
#define APTA_REQUEST_FAILED               6u

#define APTA_PRIORITY_BACKGROUND          32u
#define APTA_PRIORITY_NORMAL              96u
#define APTA_PRIORITY_INTERACTIVE         192u
#define APTA_PRIORITY_PLAYBACK_CRITICAL   240u

#define APTA_GRID_REPRESENTATION_NONE      0u
#define APTA_GRID_REPRESENTATION_SEGMENTS  1u
#define APTA_GRID_REPRESENTATION_EXPLICIT  2u
#define APTA_GRID_REPRESENTATION_HYBRID    3u

/*
 * Relation of a candidate tempo to the selected one.
 *
 * These values are serialized as a single byte in the TEMP section, so they
 * are append-only: existing values must never be renumbered. B2 added 5 to 8;
 * a reader that predates them rejects the section rather than misreading it.
 */
#define APTA_TEMPO_RELATION_INDEPENDENT 0u
#define APTA_TEMPO_RELATION_HALF        1u
#define APTA_TEMPO_RELATION_DOUBLE      2u
#define APTA_TEMPO_RELATION_THREE_HALF  3u
#define APTA_TEMPO_RELATION_TWO_THIRDS  4u
#define APTA_TEMPO_RELATION_THIRD       5u
#define APTA_TEMPO_RELATION_TRIPLE      6u
#define APTA_TEMPO_RELATION_QUARTER     7u
#define APTA_TEMPO_RELATION_QUADRUPLE   8u

#define APTA_MEMORY_DEFAULT     0u
#define APTA_MEMORY_FAST        (1u << 0)
#define APTA_MEMORY_LARGE       (1u << 1)
#define APTA_MEMORY_PERSISTENT  (1u << 2)
#define APTA_MEMORY_TEMPORARY   (1u << 3)
#define APTA_MEMORY_DMA         (1u << 4)

#define APTA_LOG_ERROR 1u
#define APTA_LOG_WARN  2u
#define APTA_LOG_INFO  3u
#define APTA_LOG_DEBUG 4u
#define APTA_LOG_TRACE 5u

#define APTA_DIAGNOSTIC_INFO     1u
#define APTA_DIAGNOSTIC_WARNING  2u
#define APTA_DIAGNOSTIC_ERROR    3u
#define APTA_DIAGNOSTIC_FATAL    4u

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;
    apta_source_frame_t first_frame;
    apta_source_frame_t end_frame;
} apta_frame_range_t;

typedef struct {
    uint64_t whole_frame;
    uint32_t fraction_q32;
    uint32_t reserved;
} apta_fractional_frame_t;

typedef struct {
    uint64_t whole_frames;
    uint32_t fraction_q32;
    uint32_t reserved;
} apta_frame_period_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* APTA_TYPES_H */
