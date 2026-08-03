# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(APTAConformance VERSION 1.0.0 LANGUAGES C)

find_package(APTA 1.0 CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(CheckLibraryExists)
check_library_exists(m sin "" APTA_CONFORMANCE_HAVE_LIBM)

execute_process(
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/check_public_sources.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    RESULT_VARIABLE APTA_PUBLIC_SOURCE_CHECK_RESULT)
if(NOT APTA_PUBLIC_SOURCE_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR "Public conformance source boundary check failed")
endif()

set(
    APTA_CONFORMANCE_PROFILES
    "WAVEFORM-1.0;ADAPTIVE-WAVEFORM-1.0;CORE-ANALYSIS-1.0"
    CACHE STRING
    "Semicolon-separated public APTA profiles to claim")
set(
    APTA_CONFORMANCE_QUALIFIERS
    "REFERENCE-WAVEFORM-1.0"
    CACHE STRING
    "Semicolon-separated reference qualifiers to claim")
set(
    APTA_CONFORMANCE_IMPLEMENTATION_NAME
    "libapta"
    CACHE STRING
    "Implementation name recorded in the JSON report")
set(
    APTA_CONFORMANCE_SOURCE_REVISION
    "$ENV{GITHUB_SHA}"
    CACHE STRING
    "Implementation source revision recorded in the JSON report")
if(APTA_CONFORMANCE_SOURCE_REVISION STREQUAL "")
    set(APTA_CONFORMANCE_SOURCE_REVISION "unknown")
endif()
set(
    APTA_CONFORMANCE_REPORT
    "${CMAKE_BINARY_DIR}/apta-conformance-report.json"
    CACHE FILEPATH
    "Output path for the deterministic JSON report")

set(APTA_CONFORMANCE_TARGETS)

function(apta_add_conformance_case target source)
    add_executable(${target} "${source}")
    target_link_libraries(${target} PRIVATE apta::core)
    set_target_properties(
        ${target}
        PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS NO
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(
            ${target}
            PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${config_upper}"
                    "${CMAKE_BINARY_DIR}/bin")
    endforeach()
    list(APPEND APTA_CONFORMANCE_TARGETS ${target})
    set(APTA_CONFORMANCE_TARGETS "${APTA_CONFORMANCE_TARGETS}" PARENT_SCOPE)
endfunction()

apta_add_conformance_case(
    apta_conf_api_initializers tests/api_initializers.c)
apta_add_conformance_case(
    apta_conf_api_version tests/api_version.c)
apta_add_conformance_case(
    apta_conf_core_lifecycle tests/core_lifecycle.c)
apta_add_conformance_case(
    apta_conf_waveform_semantic tests/waveform_semantic.c)
apta_add_conformance_case(
    apta_conf_waveform_serialization tests/waveform_serialization.c)
apta_add_conformance_case(
    apta_conf_waveform_detail tests/waveform_detail.c)
apta_add_conformance_case(
    apta_conf_waveform_three_band tests/waveform_three_band.c)
if(APTA_CONFORMANCE_HAVE_LIBM)
    target_link_libraries(apta_conf_waveform_three_band PRIVATE m)
endif()
apta_add_conformance_case(
    apta_conf_analysis_semantics tests/analysis_semantics.c)
apta_add_conformance_case(
    apta_conf_revision_semantics tests/revision_semantics.c)
apta_add_conformance_case(
    apta_conf_canonical_fixtures tests/canonical_fixtures.c)
target_compile_definitions(
    apta_conf_canonical_fixtures
    PRIVATE
        APTA_EXTERNAL_FIXTURE_HEX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/reference-wovr-meta.apta.hex")
apta_add_conformance_case(
    apta_conf_reference_waveform tests/reference_waveform.c)
apta_add_conformance_case(
    apta_conf_reference_partition tests/reference_partition.c)
apta_add_conformance_case(
    apta_conf_reference_wovr tests/reference_wovr.c)

set(profile_args)
foreach(profile IN LISTS APTA_CONFORMANCE_PROFILES)
    if(NOT profile STREQUAL "")
        list(APPEND profile_args --profile "${profile}")
    endif()
endforeach()
set(qualifier_args)
foreach(qualifier IN LISTS APTA_CONFORMANCE_QUALIFIERS)
    if(NOT qualifier STREQUAL "")
        list(APPEND qualifier_args --qualifier "${qualifier}")
    endif()
endforeach()

add_custom_target(
    apta_conformance_report
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/run_suite.py"
        --manifest "${CMAKE_CURRENT_SOURCE_DIR}/suite-manifest.json"
        --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/manifest.json"
        --bin-dir "${CMAKE_BINARY_DIR}/bin"
        --exe-suffix "${CMAKE_EXECUTABLE_SUFFIX}"
        --output "${APTA_CONFORMANCE_REPORT}"
        --runtime-dir "${APTA_RUNTIME_DIR}"
        --implementation-name "${APTA_CONFORMANCE_IMPLEMENTATION_NAME}"
        --implementation-version "${APTA_VERSION_FULL}"
        --source-revision "${APTA_CONFORMANCE_SOURCE_REVISION}"
        ${profile_args}
        ${qualifier_args}
        --platform-system "${CMAKE_SYSTEM_NAME}"
        --platform-processor "${CMAKE_SYSTEM_PROCESSOR}"
        --platform-toolchain "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
        --build-option "shared=${APTA_SHARED_PACKAGE}"
        --build-option "configuration=$<CONFIG>"
        --build-option "generator=${CMAKE_GENERATOR}"
        --evidence "sanitizer=parent-ci:sanitizer-runtime"
        --evidence "fuzz=parent-ci:bounded-fuzz-smoke"
    DEPENDS ${APTA_CONFORMANCE_TARGETS}
    VERBATIM)
