# SPDX-License-Identifier: Apache-2.0
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
