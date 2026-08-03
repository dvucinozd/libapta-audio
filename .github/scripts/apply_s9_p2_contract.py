#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    if not content.endswith("\n"):
        content += "\n"
    target.write_text(content, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# P2.1: one release-version source.
# ---------------------------------------------------------------------------
write("VERSION", "1.0.0-rc.1\n")

write(
    "include/apta/apta_version.h",
    r'''// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_VERSION_H
#define APTA_VERSION_H

#define APTA_PACKAGE_VERSION_MAJOR 1u
#define APTA_PACKAGE_VERSION_MINOR 0u
#define APTA_PACKAGE_VERSION_PATCH 0u
#define APTA_PACKAGE_VERSION_PRERELEASE "rc.1"
#define APTA_PACKAGE_VERSION_STRING "1.0.0-rc.1"

#define APTA_SPEC_VERSION_MAJOR 1u
#define APTA_SPEC_VERSION_MINOR 0u

#define APTA_API_VERSION_MAJOR 1u
#define APTA_API_VERSION_MINOR 0u
#define APTA_API_VERSION_PATCH 0u

#define APTA_CONTAINER_VERSION 1u

#define APTA_API_VERSION_ENCODE(major, minor, patch) \
    ((((major) & 0x3ffu) << 22) | (((minor) & 0x3ffu) << 12) | ((patch) & 0xfffu))

#define APTA_API_VERSION_GET_MAJOR(version) (((version) >> 22) & 0x3ffu)
#define APTA_API_VERSION_GET_MINOR(version) (((version) >> 12) & 0x3ffu)
#define APTA_API_VERSION_GET_PATCH(version) ((version) & 0xfffu)

#define APTA_API_VERSION \
    APTA_API_VERSION_ENCODE( \
        APTA_API_VERSION_MAJOR, \
        APTA_API_VERSION_MINOR, \
        APTA_API_VERSION_PATCH)

#endif /* APTA_VERSION_H */
''')

replace_once(
    "CMakeLists.txt",
    '''cmake_minimum_required(VERSION 3.16)

project(
    libapta
    VERSION 0.1.0
    LANGUAGES C CXX)
''',
    '''cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APTA_VERSION_FULL)
string(STRIP "${APTA_VERSION_FULL}" APTA_VERSION_FULL)
if(NOT APTA_VERSION_FULL MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "VERSION must be SemVer, got: ${APTA_VERSION_FULL}")
endif()
set(APTA_VERSION_NUMERIC "${CMAKE_MATCH_1}")

project(
    libapta
    VERSION ${APTA_VERSION_NUMERIC}
    LANGUAGES C CXX)

include(cmake/APTAVersionCheck.cmake)
''')

write(
    "cmake/APTAVersionCheck.cmake",
    r'''# SPDX-License-Identifier: Apache-2.0
file(
    READ
    "${CMAKE_CURRENT_SOURCE_DIR}/include/apta/apta_version.h"
    APTA_PUBLIC_VERSION_HEADER)

function(apta_extract_define name pattern output)
    string(REGEX MATCH "#define[ \t]+${name}[ \t]+${pattern}" match
           "${APTA_PUBLIC_VERSION_HEADER}")
    if(NOT match)
        message(FATAL_ERROR "Missing or invalid ${name} in apta_version.h")
    endif()
    set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

apta_extract_define(
    APTA_PACKAGE_VERSION_STRING
    "\\\"([^\\\"]+)\\\""
    APTA_HEADER_PACKAGE_VERSION)
apta_extract_define(APTA_PACKAGE_VERSION_MAJOR "([0-9]+)u" APTA_HEADER_PACKAGE_MAJOR)
apta_extract_define(APTA_PACKAGE_VERSION_MINOR "([0-9]+)u" APTA_HEADER_PACKAGE_MINOR)
apta_extract_define(APTA_PACKAGE_VERSION_PATCH "([0-9]+)u" APTA_HEADER_PACKAGE_PATCH)
apta_extract_define(APTA_API_VERSION_MAJOR "([0-9]+)u" APTA_HEADER_API_MAJOR)
apta_extract_define(APTA_API_VERSION_MINOR "([0-9]+)u" APTA_HEADER_API_MINOR)
apta_extract_define(APTA_API_VERSION_PATCH "([0-9]+)u" APTA_HEADER_API_PATCH)
apta_extract_define(APTA_SPEC_VERSION_MAJOR "([0-9]+)u" APTA_HEADER_SPEC_MAJOR)
apta_extract_define(APTA_SPEC_VERSION_MINOR "([0-9]+)u" APTA_HEADER_SPEC_MINOR)
apta_extract_define(APTA_CONTAINER_VERSION "([0-9]+)u" APTA_HEADER_CONTAINER_VERSION)

set(APTA_HEADER_PACKAGE_NUMERIC
    "${APTA_HEADER_PACKAGE_MAJOR}.${APTA_HEADER_PACKAGE_MINOR}.${APTA_HEADER_PACKAGE_PATCH}")
set(APTA_HEADER_API_NUMERIC
    "${APTA_HEADER_API_MAJOR}.${APTA_HEADER_API_MINOR}.${APTA_HEADER_API_PATCH}")

if(NOT APTA_HEADER_PACKAGE_VERSION STREQUAL APTA_VERSION_FULL)
    message(FATAL_ERROR
        "VERSION (${APTA_VERSION_FULL}) and public package version "
        "(${APTA_HEADER_PACKAGE_VERSION}) disagree")
endif()
if(NOT APTA_HEADER_PACKAGE_NUMERIC VERSION_EQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "CMake project version (${PROJECT_VERSION}) and public package version "
        "(${APTA_HEADER_PACKAGE_NUMERIC}) disagree")
endif()
if(NOT APTA_HEADER_API_NUMERIC VERSION_EQUAL PROJECT_VERSION)
    message(FATAL_ERROR
        "P2 requires package and API numeric versions to agree: "
        "${PROJECT_VERSION} versus ${APTA_HEADER_API_NUMERIC}")
endif()
if(NOT APTA_HEADER_SPEC_MAJOR EQUAL 1 OR NOT APTA_HEADER_SPEC_MINOR EQUAL 0)
    message(FATAL_ERROR "P2 requires APTA specification version 1.0")
endif()
if(NOT APTA_HEADER_CONTAINER_VERSION EQUAL 1)
    message(FATAL_ERROR "Container version is independent and must remain 1")
endif()

set(APTA_PACKAGE_VERSION "${APTA_VERSION_FULL}")
set(APTA_API_VERSION_STRING "${APTA_HEADER_API_NUMERIC}")
set(APTA_SPEC_VERSION_STRING
    "${APTA_HEADER_SPEC_MAJOR}.${APTA_HEADER_SPEC_MINOR}")
set(APTA_CONTAINER_VERSION_STRING "${APTA_HEADER_CONTAINER_VERSION}")
''')

# ---------------------------------------------------------------------------
# P2.2/P2.5: stable API predicate and public source identity.
# ---------------------------------------------------------------------------
replace_once(
    "include/apta/apta_types.h",
    '''#if defined(_WIN32) && defined(APTA_SHARED)
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
''',
    '''#if defined(_WIN32) && defined(APTA_SHARED)
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
''')

replace_once(
    "include/apta/apta_types.h",
    '''typedef uint32_t apta_tempo_millibpm_t;
typedef int64_t apta_beat_ordinal_t;

#define APTA_TOTAL_FRAMES_UNKNOWN UINT64_MAX
''',
    '''typedef uint32_t apta_tempo_millibpm_t;
typedef int64_t apta_beat_ordinal_t;
typedef uint32_t apta_source_fingerprint_kind_t;

#define APTA_SOURCE_FINGERPRINT_NONE                       0u
#define APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256     1u
#define APTA_SOURCE_FINGERPRINT_SHA256_SOURCE_OBJECT_BYTES 2u
#define APTA_SOURCE_FINGERPRINT_SIZE                       32u

#define APTA_TOTAL_FRAMES_UNKNOWN UINT64_MAX
''')

replace_once(
    "include/apta/apta_config.h",
    '#define APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS (1u << 0)\n',
    '''#define APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS              (1u << 0)
#define APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING (1u << 1)
''')

replace_once(
    "include/apta/apta_config.h",
    '''    uint32_t overview_frames_per_column;

    uint32_t reserved32[4];
    uint64_t reserved64[4];
} apta_session_config_t;
''',
    '''    uint32_t overview_frames_per_column;

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
''')

replace_once(
    "include/apta/apta_result.h",
    '''typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t specification_major;
''',
    '''typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    apta_source_frame_t total_frames;
    uint32_t sample_rate;
    uint16_t channel_count;
    uint16_t reserved16;
    apta_channel_layout_t channel_layout;
    apta_source_fingerprint_kind_t fingerprint_kind;
    uint8_t fingerprint[APTA_SOURCE_FINGERPRINT_SIZE];
    uint32_t flags;
    uint32_t reserved32[3];
} apta_source_info_t;

typedef struct {
    uint32_t struct_size;
    uint32_t api_version;

    uint32_t specification_major;
''')

replace_once(
    "include/apta/apta_result.h",
    '''APTA_API apta_status_t APTA_CALL
apta_result_get_info(
    const apta_result_t *result,
    apta_result_info_t *info_out);

APTA_API apta_generation_t APTA_CALL
''',
    '''APTA_API apta_status_t APTA_CALL
apta_result_get_info(
    const apta_result_t *result,
    apta_result_info_t *info_out);

APTA_API apta_status_t APTA_CALL
apta_result_get_source_info(
    const apta_result_t *result,
    apta_source_info_t *info_out);

APTA_API apta_generation_t APTA_CALL
''')

replace_once(
    "include/apta/apta_initializers.h",
    '''APTA_API void APTA_CALL
apta_result_info_init(apta_result_info_t *info);
''',
    '''APTA_API void APTA_CALL
apta_source_info_init(apta_source_info_t *info);

APTA_API void APTA_CALL
apta_result_info_init(apta_result_info_t *info);
''')

replace_once(
    "src/core/apta_internal.h",
    '''    uint32_t result_pool_slot_index;
    uint32_t result_flags;
    apta_result_info_t info;

    apta_source_frame_t total_source_frames;
''',
    '''    uint32_t result_pool_slot_index;
    uint32_t result_flags;
    apta_result_info_t info;
    apta_source_info_t source_info;

    apta_source_frame_t total_source_frames;
''')

replace_once(
    "src/core/apta_internal.h",
    '''int apta_internal_validate_struct(
    const void *structure,
    size_t minimum_size,
    uint32_t structure_size,
    uint32_t api_version);

int apta_internal_is_power_of_two(size_t value);
''',
    '''int apta_internal_api_version_is_compatible(uint32_t api_version);

int apta_internal_validate_struct(
    const void *structure,
    size_t minimum_size,
    uint32_t structure_size,
    uint32_t api_version);

int apta_internal_source_fingerprint_is_valid(
    apta_source_fingerprint_kind_t kind,
    const uint8_t fingerprint[APTA_SOURCE_FINGERPRINT_SIZE]);

int apta_internal_source_identity_is_valid(
    const apta_session_config_t *config);

int apta_internal_is_power_of_two(size_t value);
''')

replace_once(
    "src/core/apta_memory.c",
    '''int apta_internal_validate_struct(
    const void *structure,
    size_t minimum_size,
    uint32_t structure_size,
    uint32_t api_version)
{
    return structure != NULL &&
           structure_size >= minimum_size &&
           api_version == APTA_API_VERSION;
}

int apta_internal_is_power_of_two(size_t value)
''',
    '''int apta_internal_api_version_is_compatible(uint32_t api_version)
{
    return APTA_API_VERSION_GET_MAJOR(api_version) ==
               APTA_API_VERSION_MAJOR &&
           APTA_API_VERSION_GET_MINOR(api_version) <=
               APTA_API_VERSION_MINOR;
}

int apta_internal_validate_struct(
    const void *structure,
    size_t minimum_size,
    uint32_t structure_size,
    uint32_t api_version)
{
    return structure != NULL &&
           structure_size >= minimum_size &&
           apta_internal_api_version_is_compatible(api_version);
}

int apta_internal_source_fingerprint_is_valid(
    apta_source_fingerprint_kind_t kind,
    const uint8_t fingerprint[APTA_SOURCE_FINGERPRINT_SIZE])
{
    uint32_t index;

    if (fingerprint == NULL) {
        return 0;
    }
    if (kind == APTA_SOURCE_FINGERPRINT_NONE) {
        for (index = 0u; index < APTA_SOURCE_FINGERPRINT_SIZE; ++index) {
            if (fingerprint[index] != 0u) {
                return 0;
            }
        }
        return 1;
    }
    return kind == APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256 ||
           kind == APTA_SOURCE_FINGERPRINT_SHA256_SOURCE_OBJECT_BYTES;
}

int apta_internal_source_identity_is_valid(
    const apta_session_config_t *config)
{
    return config != NULL &&
           apta_internal_source_fingerprint_is_valid(
               config->source_fingerprint_kind,
               config->source_fingerprint);
}

int apta_internal_is_power_of_two(size_t value)
''')

replace_once(
    "src/core/apta_initializers.c",
    '''void APTA_CALL apta_result_info_init(apta_result_info_t *info)
{
    if (info != NULL) {
        APTA_INIT_STRUCT(info);
    }
}
''',
    '''void APTA_CALL apta_source_info_init(apta_source_info_t *info)
{
    if (info != NULL) {
        APTA_INIT_STRUCT(info);
        info->total_frames = APTA_TOTAL_FRAMES_UNKNOWN;
    }
}

void APTA_CALL apta_result_info_init(apta_result_info_t *info)
{
    if (info != NULL) {
        APTA_INIT_STRUCT(info);
    }
}
''')

# Session configuration validation: accept both stable flags and validate the
# now-public identity fields on every creation/query path.
replace_once(
    "src/core/apta_config.c",
    '''    if ((config->flags &
         ~APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS) != 0u) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_overview_resolution_is_valid(config)) {
''',
    '''    if ((config->flags &
         ~(APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS |
           APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING)) != 0u ||
        !apta_internal_source_identity_is_valid(config)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_overview_resolution_is_valid(config)) {
''')

replace_once(
    "src/core/apta_config.c",
    '''    if (!apta_internal_validate_struct(
            requirements_out,
            sizeof(*requirements_out),
            requirements_out->struct_size,
            requirements_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    required = apta_internal_session_workspace_requirement(config);
''',
    '''    if (!apta_internal_validate_struct(
            requirements_out,
            sizeof(*requirements_out),
            requirements_out->struct_size,
            requirements_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }
    if ((config->flags &
         ~(APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS |
           APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING)) != 0u ||
        !apta_internal_source_identity_is_valid(config)) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }

    required = apta_internal_session_workspace_requirement(config);
''')

replace_once(
    "src/core/apta_session_lifecycle.c",
    '''    if (config->input_mode != APTA_INPUT_MODE_PUSH &&
        config->input_mode != APTA_INPUT_MODE_PULL) {
        return 0;
    }

    if (config->source_sample_rate == 0u ||
''',
    '''    if (config->input_mode != APTA_INPUT_MODE_PUSH &&
        config->input_mode != APTA_INPUT_MODE_PULL) {
        return 0;
    }
    if ((config->flags &
         ~(APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS |
           APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING)) != 0u ||
        !apta_internal_source_identity_is_valid(config)) {
        return 0;
    }

    if (config->source_sample_rate == 0u ||
''')

replace_once(
    "src/core/apta_session_workspace_lifecycle.c",
    '''    if (config->input_mode != APTA_INPUT_MODE_PUSH &&
        config->input_mode != APTA_INPUT_MODE_PULL) {
        return 0;
    }

    if (config->source_sample_rate == 0u ||
''',
    '''    if (config->input_mode != APTA_INPUT_MODE_PUSH &&
        config->input_mode != APTA_INPUT_MODE_PULL) {
        return 0;
    }
    if ((config->flags &
         ~(APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS |
           APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING)) != 0u ||
        !apta_internal_source_identity_is_valid(config)) {
        return 0;
    }

    if (config->source_sample_rate == 0u ||
''')

# Populate and expose source information for heap-backed results.
replace_once(
    "src/core/apta_result_lifetime.c",
    '''    result->total_source_frames = session->config.total_frames;
    result->source_sample_rate = session->config.source_sample_rate;
    result->source_channel_count = session->config.channel_count;
    result->source_channel_layout = session->config.channel_layout;

    next_generation = session->generation + 1u;
''',
    '''    apta_source_info_init(&result->source_info);
    result->source_info.total_frames = session->config.total_frames;
    result->source_info.sample_rate = session->config.source_sample_rate;
    result->source_info.channel_count = session->config.channel_count;
    result->source_info.channel_layout = session->config.channel_layout;
    result->source_info.fingerprint_kind =
        session->config.source_fingerprint_kind;
    memcpy(
        result->source_info.fingerprint,
        session->config.source_fingerprint,
        APTA_SOURCE_FINGERPRINT_SIZE);

    result->total_source_frames = result->source_info.total_frames;
    result->source_sample_rate = result->source_info.sample_rate;
    result->source_channel_count = result->source_info.channel_count;
    result->source_channel_layout = result->source_info.channel_layout;

    next_generation = session->generation + 1u;
''')

replace_once(
    "src/core/apta_result_lifetime.c",
    '''    memcpy(info_out, &result->info, sizeof(*info_out));
    return APTA_STATUS_OK;
}

apta_generation_t APTA_CALL apta_result_get_generation(
''',
    '''    memcpy(info_out, &result->info, sizeof(*info_out));
    return APTA_STATUS_OK;
}

apta_status_t APTA_CALL apta_result_get_source_info(
    const apta_result_t *result,
    apta_source_info_t *info_out)
{
    if (result == NULL || info_out == NULL) {
        return APTA_ERROR_INVALID_ARGUMENT;
    }
    if (!apta_internal_validate_struct(
            info_out,
            sizeof(*info_out),
            info_out->struct_size,
            info_out->api_version)) {
        return APTA_ERROR_INCOMPATIBLE_VERSION;
    }

    memcpy(info_out, &result->source_info, sizeof(*info_out));
    return APTA_STATUS_OK;
}

apta_generation_t APTA_CALL apta_result_get_generation(
''')

# Populate source information for bounded result slots too.
replace_once(
    "src/core/apta_result_pool.c",
    '''            result->total_source_frames = config->total_frames;
            result->source_sample_rate = config->source_sample_rate;
            result->source_channel_count = config->channel_count;
            result->source_channel_layout = config->channel_layout;

            result->info.struct_size = (uint32_t)sizeof(result->info);
''',
    '''            apta_source_info_init(&result->source_info);
            result->source_info.total_frames = config->total_frames;
            result->source_info.sample_rate = config->source_sample_rate;
            result->source_info.channel_count = config->channel_count;
            result->source_info.channel_layout = config->channel_layout;
            result->source_info.fingerprint_kind =
                config->source_fingerprint_kind;
            memcpy(
                result->source_info.fingerprint,
                config->source_fingerprint,
                APTA_SOURCE_FINGERPRINT_SIZE);

            result->total_source_frames = result->source_info.total_frames;
            result->source_sample_rate = result->source_info.sample_rate;
            result->source_channel_count = result->source_info.channel_count;
            result->source_channel_layout = result->source_info.channel_layout;

            result->info.struct_size = (uint32_t)sizeof(result->info);
''')

# Container reader: retain the version-1 identity header rather than rejecting
# every non-zero kind.
replace_once(
    "src/serialization/apta_wovr_reader.c",
    '''    uint32_t source_channel_layout;
    uint32_t specification_major;
    uint32_t specification_minor;
    uint32_t producer_api_version;
} apta_wovr_input_t;
''',
    '''    uint32_t source_channel_layout;
    apta_source_fingerprint_kind_t source_fingerprint_kind;
    uint8_t source_fingerprint[APTA_SOURCE_FINGERPRINT_SIZE];
    uint32_t specification_major;
    uint32_t specification_minor;
    uint32_t producer_api_version;
} apta_wovr_input_t;
''')

replace_once(
    "src/serialization/apta_wovr_reader.c",
    '''    source_fingerprint_kind = apta_get_u32(bytes + 88u);

    if (header_size < APTA_CONTAINER_HEADER_SIZE ||
''',
    '''    source_fingerprint_kind = apta_get_u32(bytes + 88u);
    input->source_fingerprint_kind = source_fingerprint_kind;
    memcpy(
        input->source_fingerprint,
        bytes + 56u,
        APTA_SOURCE_FINGERPRINT_SIZE);

    if (header_size < APTA_CONTAINER_HEADER_SIZE ||
''')

replace_once(
    "src/serialization/apta_wovr_reader.c",
    '''    if (source_fingerprint_kind != 0u) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (!apta_bytes_are_zero(bytes + 56u, 32u)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
''',
    '''    if (source_fingerprint_kind != APTA_SOURCE_FINGERPRINT_NONE &&
        source_fingerprint_kind !=
            APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256 &&
        source_fingerprint_kind !=
            APTA_SOURCE_FINGERPRINT_SHA256_SOURCE_OBJECT_BYTES) {
        return APTA_ERROR_UNSUPPORTED;
    }
    if (source_fingerprint_kind == APTA_SOURCE_FINGERPRINT_NONE &&
        !apta_bytes_are_zero(
            bytes + 56u,
            APTA_SOURCE_FINGERPRINT_SIZE)) {
        return APTA_ERROR_CORRUPT_DATA;
    }
''')

replace_once(
    "src/serialization/apta_wovr_reader.c",
    '''    result->total_source_frames = input->total_source_frames;
    result->source_sample_rate = input->source_sample_rate;
    result->source_channel_count = input->source_channel_count;
    result->source_channel_layout = input->source_channel_layout;

    result->overview.struct_size = (uint32_t)sizeof(result->overview);
''',
    '''    apta_source_info_init(&result->source_info);
    result->source_info.total_frames = input->total_source_frames;
    result->source_info.sample_rate = input->source_sample_rate;
    result->source_info.channel_count =
        (uint16_t)input->source_channel_count;
    result->source_info.channel_layout = input->source_channel_layout;
    result->source_info.fingerprint_kind =
        input->source_fingerprint_kind;
    memcpy(
        result->source_info.fingerprint,
        input->source_fingerprint,
        APTA_SOURCE_FINGERPRINT_SIZE);

    result->total_source_frames = result->source_info.total_frames;
    result->source_sample_rate = result->source_info.sample_rate;
    result->source_channel_count = result->source_info.channel_count;
    result->source_channel_layout = result->source_info.channel_layout;

    result->overview.struct_size = (uint32_t)sizeof(result->overview);
''')

# Canonical writer emits exactly the source identity exposed by the result.
replace_once(
    "src/serialization/apta_wovr_writer.c",
    '''        result->source_channel_count > UINT16_MAX ||
        result->source_channel_layout > UINT16_MAX) {
        return APTA_ERROR_INTERNAL;
    }
''',
    '''        result->source_channel_count > UINT16_MAX ||
        result->source_channel_layout > UINT16_MAX ||
        !apta_internal_source_fingerprint_is_valid(
            result->source_info.fingerprint_kind,
            result->source_info.fingerprint)) {
        return APTA_ERROR_INTERNAL;
    }
''')

replace_once(
    "src/serialization/apta_wovr_writer.c",
    '''    apta_put_u16(bytes + 52u, (uint16_t)result->source_channel_count);
    apta_put_u16(bytes + 54u, (uint16_t)result->source_channel_layout);
    apta_put_u32(bytes + 88u, 0u);
''',
    '''    apta_put_u16(bytes + 52u, (uint16_t)result->source_channel_count);
    apta_put_u16(bytes + 54u, (uint16_t)result->source_channel_layout);
    memcpy(
        bytes + 56u,
        result->source_info.fingerprint,
        APTA_SOURCE_FINGERPRINT_SIZE);
    apta_put_u32(
        bytes + 88u,
        result->source_info.fingerprint_kind);
''')

# Seeding identity policy: equal geometry remains necessary but is not source
# identity. By default missing identity is accepted; the opt-in flag makes it
# mandatory.
replace_once(
    "src/waveform/apta_waveform_input.c",
    '''    if (result->source_channel_count != 0u &&
        result->source_channel_count != session->config.channel_count) {
        status = APTA_ERROR_CONFLICT;
        goto done;
    }
    /* An unknown length on either side is not a conflict: a checkpoint can
''',
    '''    if (result->source_channel_count != 0u &&
        result->source_channel_count != session->config.channel_count) {
        status = APTA_ERROR_CONFLICT;
        goto done;
    }
    if (result->source_channel_layout != APTA_CHANNEL_LAYOUT_UNSPECIFIED &&
        session->config.channel_layout != APTA_CHANNEL_LAYOUT_UNSPECIFIED &&
        result->source_channel_layout != session->config.channel_layout) {
        status = APTA_ERROR_CONFLICT;
        goto done;
    }
    if ((session->config.flags &
         APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING) != 0u &&
        (session->config.source_fingerprint_kind ==
             APTA_SOURCE_FINGERPRINT_NONE ||
         result->source_info.fingerprint_kind ==
             APTA_SOURCE_FINGERPRINT_NONE)) {
        status = APTA_ERROR_CONFLICT;
        goto done;
    }
    if (session->config.source_fingerprint_kind !=
            APTA_SOURCE_FINGERPRINT_NONE &&
        result->source_info.fingerprint_kind !=
            APTA_SOURCE_FINGERPRINT_NONE &&
        (session->config.source_fingerprint_kind !=
             result->source_info.fingerprint_kind ||
         memcmp(
             session->config.source_fingerprint,
             result->source_info.fingerprint,
             APTA_SOURCE_FINGERPRINT_SIZE) != 0)) {
        status = APTA_ERROR_CONFLICT;
        goto done;
    }
    /* An unknown length on either side is not a conflict: a checkpoint can
''')

# Header contract text follows the implemented policy.
replace_once(
    "include/apta/apta_source.h",
    ''' * The result must carry APTA_FEATURE_WAVEFORM_OVERVIEW, and its column
 * geometry, source sample rate, channel count and track length must match the
 * session, or APTA_ERROR_CONFLICT is returned. A length that is unknown on
 * either side is not a conflict, since a checkpoint can predate the point where
 * the length became known. See docs/api/APTA-SESSION-SEEDING-0.1.md.
''',
    ''' * The result must carry APTA_FEATURE_WAVEFORM_OVERVIEW, and its column
 * geometry, known source sample rate, channel layout/count and known track
 * length must match the session, or APTA_ERROR_CONFLICT is returned. A length
 * that is unknown on either side is not a conflict.
 *
 * When both sides carry a source fingerprint, kind and bytes must match. A
 * missing identity is accepted by default because equal geometry does not prove
 * equal audio; hosts that require an identity set
 * APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING. See
 * docs/api/APTA-SESSION-SEEDING-0.1.md.
''')

# ---------------------------------------------------------------------------
# Tests for version compatibility and source information/identity.
# ---------------------------------------------------------------------------
write(
    "tests/unit/version_validation.c",
    r'''// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);                         \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void configure_session(apta_session_config_t *config)
{
    apta_session_config_init(config);
    config->source_sample_rate = 48000u;
    config->channel_count = 2u;
    config->sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    config->channel_layout = APTA_CHANNEL_LAYOUT_STEREO;
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    apta_pcm_block_t block;
    apta_result_info_t info;
    const apta_result_t *result;
    int16_t pcm[2] = {0, 0};
    uint32_t accepted = 77u;

    CHECK(APTA_API_VERSION_GET_MAJOR(APTA_API_VERSION) == 1u);
    CHECK(APTA_API_VERSION_GET_MINOR(APTA_API_VERSION) == 0u);
    CHECK(APTA_API_VERSION_GET_PATCH(APTA_API_VERSION) == 0u);

    /* Patch differences never change the 1.x ABI contract. */
    apta_context_config_init(&context_config);
    context_config.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 4095u);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    context = NULL;

    /* Wrong major, newer caller minor and undersized prefixes are rejected. */
    apta_context_config_init(&context_config);
    context_config.api_version = APTA_API_VERSION_ENCODE(0u, 3u, 0u);
    CHECK(apta_context_create(&context_config, &context) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(context == NULL);

    apta_context_config_init(&context_config);
    context_config.api_version = APTA_API_VERSION_ENCODE(1u, 1u, 0u);
    CHECK(apta_context_create(&context_config, &context) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(context == NULL);

    apta_context_config_init(&context_config);
    context_config.struct_size = (uint32_t)sizeof(context_config) - 1u;
    CHECK(apta_context_create(&context_config, &context) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(context == NULL);

    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    configure_session(&session_config);
    session_config.api_version = APTA_API_VERSION_ENCODE(2u, 0u, 0u);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(session == NULL);

    configure_session(&session_config);
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_block_init(&block);
    block.data = pcm;
    block.frame_count = 1u;
    block.api_version = APTA_API_VERSION_ENCODE(1u, 1u, 0u);
    CHECK(apta_session_push_pcm(session, &block, &accepted) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);
    CHECK(accepted == 0u);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CREATED);

    result = apta_session_acquire_result(session);
    CHECK(result != NULL);

    apta_result_info_init(&info);
    info.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 77u);
    CHECK(apta_result_get_info(result, &info) == APTA_STATUS_OK);
    CHECK(info.api_version == APTA_API_VERSION);

    apta_result_info_init(&info);
    info.api_version = APTA_API_VERSION_ENCODE(1u, 1u, 0u);
    CHECK(apta_result_get_info(result, &info) ==
          APTA_ERROR_INCOMPATIBLE_VERSION);

    apta_result_release(result);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);

    return 0;
}
''')

# Seed test: establish identity before serialization, inspect it after parsing,
# and cover mismatch/missing policy.
replace_once(
    "tests/unit/session_seeding.c",
    '''    config->total_frames = TOTAL_FRAMES;
    config->requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
}
''',
    '''    config->total_frames = TOTAL_FRAMES;
    config->requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    config->source_fingerprint_kind =
        APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256;
    config->source_fingerprint[0] = 0x41u;
    config->source_fingerprint[1] = 0x50u;
    config->source_fingerprint[2] = 0x54u;
    config->source_fingerprint[3] = 0x41u;
}
''')

replace_once(
    "tests/unit/session_seeding.c",
    '''    apta_waveform_overview_view_t reference;
    apta_waveform_overview_view_t resumed;
''',
    '''    apta_waveform_overview_view_t reference;
    apta_waveform_overview_view_t resumed;
    apta_source_info_t source_info;
''')

replace_once(
    "tests/unit/session_seeding.c",
    '''    CHECK(checkpoint != NULL);

    /* 3. Seeding is rejected outside APTA_SESSION_CREATED and on mismatch. */
''',
    '''    CHECK(checkpoint != NULL);
    apta_source_info_init(&source_info);
    CHECK(apta_result_get_source_info(checkpoint, &source_info) ==
          APTA_STATUS_OK);
    CHECK(source_info.total_frames == TOTAL_FRAMES);
    CHECK(source_info.sample_rate == RATE);
    CHECK(source_info.channel_count == 1u);
    CHECK(source_info.channel_layout == APTA_CHANNEL_LAYOUT_MONO);
    CHECK(source_info.fingerprint_kind ==
          APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256);
    CHECK(source_info.fingerprint[0] == 0x41u);
    CHECK(source_info.fingerprint[3] == 0x41u);

    /* 3. Seeding is rejected outside APTA_SESSION_CREATED and on mismatch. */
''')

replace_once(
    "tests/unit/session_seeding.c",
    '''    configure(&config);
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(apta_session_seed_from_result(session, NULL) ==
          APTA_ERROR_INVALID_ARGUMENT);
''',
    '''    configure(&config);
    config.source_fingerprint[0] ^= 0x01u;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(apta_session_seed_from_result(session, checkpoint) ==
          APTA_ERROR_CONFLICT);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    /* Missing identity remains host policy by default. */
    configure(&config);
    config.source_fingerprint_kind = APTA_SOURCE_FINGERPRINT_NONE;
    memset(config.source_fingerprint, 0, sizeof(config.source_fingerprint));
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(apta_session_seed_from_result(session, checkpoint) ==
          APTA_STATUS_OK);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    /* The opt-in strict policy requires identity on both sides. */
    configure(&config);
    config.source_fingerprint_kind = APTA_SOURCE_FINGERPRINT_NONE;
    memset(config.source_fingerprint, 0, sizeof(config.source_fingerprint));
    config.flags |= APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING;
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(apta_session_seed_from_result(session, checkpoint) ==
          APTA_ERROR_CONFLICT);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = NULL;

    configure(&config);
    CHECK(apta_session_create(context, &config, &session) == APTA_STATUS_OK);
    CHECK(apta_session_seed_from_result(session, NULL) ==
          APTA_ERROR_INVALID_ARGUMENT);
''')

# Version-report command and CTest gate.
write(
    "tools/apta_version.c",
    r'''// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>

#include <apta/apta.h>

int main(void)
{
    printf("package=%s\n", APTA_PACKAGE_VERSION_STRING);
    printf("api=%u.%u.%u\n",
           APTA_API_VERSION_MAJOR,
           APTA_API_VERSION_MINOR,
           APTA_API_VERSION_PATCH);
    printf("spec=%u.%u\n",
           APTA_SPEC_VERSION_MAJOR,
           APTA_SPEC_VERSION_MINOR);
    printf("container=%u\n", APTA_CONTAINER_VERSION);
    return 0;
}
''')

replace_once(
    "tools/CMakeLists.txt",
    '''apta_add_tool(apta_analyze apta-analyze apta_analyze.c)
apta_add_tool(apta_inspect apta-inspect apta_inspect.c)
apta_add_tool(apta_validate apta-validate apta_validate.c)
''',
    '''apta_add_tool(apta_analyze apta-analyze apta_analyze.c)
apta_add_tool(apta_inspect apta-inspect apta_inspect.c)
apta_add_tool(apta_validate apta-validate apta_validate.c)
apta_add_tool(apta_version apta-version apta_version.c)
''')

replace_once(
    "tests/CMakeLists.txt",
    '''if(TARGET apta_tool_common)
    add_executable(apta_tool_features_all unit/tool_features_all.c)
''',
    '''if(TARGET apta_version)
    add_test(NAME apta.api.version_report COMMAND apta_version)
    set_tests_properties(
        apta.api.version_report
        PROPERTIES
            PASS_REGULAR_EXPRESSION
                "package=1.0.0-rc.1.*api=1.0.0.*spec=1.0.*container=1")
endif()

if(TARGET apta_tool_common)
    add_executable(apta_tool_features_all unit/tool_features_all.c)
''')

write(
    "docs/api/APTA-API-ABI-1.0.md",
    r'''# APTA public API and ABI 1.0

**Status:** release-candidate contract
**Package candidate:** `1.0.0-rc.1`
**API:** `1.0.0`

## Version domains

The root `VERSION` file is the package release source. CMake and
`apta_version.h` must agree with it. Specification and container versions are
separate compatibility domains; the version-1 container does not change merely
because the package or API receives a compatible 1.x update.

## API compatibility

Every extensible public structure begins with `struct_size` and `api_version`.
The library accepts a caller when:

- API major equals the library API major;
- caller minor is not newer than the library minor;
- patch is ignored for ABI compatibility;
- `struct_size` covers every field read by that entry point.

A newer caller minor is rejected unless a later API explicitly documents a
safe prefix rule. A different major is rejected. Public 1.x symbols and field
interpretations are append-only; removal or incompatible reinterpretation
requires API 2.0.

## Source information and checkpoint identity

`apta_result_get_source_info()` exposes the geometry and optional 256-bit
identity that the library uses for serialization and seeding checks. Supported
identity kinds are application-opaque bytes and SHA-256 of exact source-object
bytes. Equal geometry is not proof of equal audio.

When both a session and checkpoint carry identities, they must match. Missing
identity is accepted by default so hosts retain policy control. A host that
requires identity for checkpoint continuation sets
`APTA_SESSION_FLAG_REQUIRE_SOURCE_IDENTITY_FOR_SEEDING`.

## Freeze progression

The checked-in 1.0 header snapshot, public layout manifests and public symbol
manifest are added by the remaining P2 commits. After the P2 exit gate, any new
public API requires an explicit S9 freeze exception.
''')

write(
    "docs/status/S9-P2-API-ABI-STATUS.md",
    r'''# Stage S9 P2 — Public API and ABI freeze status

**Status:** implementation in progress
**Branch:** `agent/s9-p2-api-abi-freeze`

## Completed in the contract commit

- root package version source and configure-time consistency checks;
- package/API/specification/container version report;
- API 1.0 compatibility predicate with patch-tolerant, major/minor-aware rules;
- stable public source-information accessor;
- portable source fingerprint round trip through container version 1;
- explicit optional/required checkpoint identity policy;
- geometry and identity seeding regression coverage.

## Remaining P2 gates

- checked-in 1.0 public-header snapshot and old-header/new-library client;
- LP64, ILP32 and LLP64 public layout manifests;
- public symbol manifest and shared-library export checks;
- final P2 compatibility report and roadmap transition.

No analysis algorithm or canonical waveform/tempo/grid payload semantics change
in this phase.
''')

# Restore the normal CI workflow and remove this bootstrap source from the
# resulting commit. The running job keeps using the already-loaded workflow.
ci_path = ROOT / ".github/workflows/ci.yml"
ci = ci_path.read_text(encoding="utf-8")
ci = ci.replace("permissions:\n  contents: write\n", "permissions:\n  contents: read\n", 1)
start = ci.find("\n  p2-contract-bootstrap:\n")
end_marker = "\n  core-build:\n"
if start < 0:
    raise SystemExit("ci.yml: P2 bootstrap block not found")
end = ci.find(end_marker, start)
if end < 0:
    raise SystemExit("ci.yml: core-build anchor not found after bootstrap")
ci = ci[:start] + end_marker + ci[end + len(end_marker):]
ci_path.write_text(ci, encoding="utf-8")

Path(__file__).unlink()
