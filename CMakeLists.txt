# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)

project(
    libapta
    VERSION 0.1.0
    LANGUAGES C CXX)

option(APTA_BUILD_TESTS "Build libapta tests" ON)
option(APTA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(APTA_BUILD_FUZZING "Build libFuzzer targets" OFF)

add_library(apta_headers INTERFACE)
add_library(apta::headers ALIAS apta_headers)

target_include_directories(
    apta_headers
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(
    apta_headers
    INTERFACE
        c_std_11
        cxx_std_11)

add_library(apta_sanitizers INTERFACE)

if(APTA_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
            apta_sanitizers
            INTERFACE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer)
        target_link_options(
            apta_sanitizers
            INTERFACE
                -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "APTA_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
endif()

set_source_files_properties(
    src/serialization/apta_wovr_reader.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_base")

set_source_files_properties(
    src/core/apta_session_lifecycle.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_create=apta_session_create_base")

set_source_files_properties(
    src/core/apta_session_scheduler.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_set_focus=apta_session_set_focus_base;apta_session_request_region=apta_session_request_region_base;apta_session_next_pcm_request=apta_session_next_pcm_request_base")

set_source_files_properties(
    src/core/apta_result_accessors.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_get_feature_state=apta_result_get_feature_state_base;apta_result_get_waveform_tile=apta_result_get_waveform_tile_base")

set_source_files_properties(
    src/waveform/apta_waveform_input.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_accept_pcm=apta_internal_waveform_accept_pcm_base")

set_source_files_properties(
    src/waveform/apta_waveform_process.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_base")

set_source_files_properties(
    src/waveform/apta_waveform_snapshot.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_build_snapshot=apta_internal_waveform_build_overview_snapshot;apta_internal_waveform_cleanup_result=apta_internal_waveform_cleanup_result_base")

set_source_files_properties(
    src/waveform/apta_waveform_detail_replay.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_detail_next_pcm_request=apta_internal_detail_next_pcm_request_base;apta_internal_detail_accept_replay=apta_internal_detail_accept_replay_base")

add_library(
    apta_core
    STATIC
        src/core/apta_config.c
        src/core/apta_context.c
        src/core/apta_initializers.c
        src/core/apta_memory.c
        src/core/apta_result_accessors.c
        src/core/apta_result_detail_accessors.c
        src/core/apta_result_lifetime.c
        src/core/apta_session_detail_contract.c
        src/core/apta_session_lifecycle.c
        src/core/apta_session_scheduler.c
        src/core/apta_session_source.c
        src/serialization/apta_crc32c.c
        src/serialization/apta_wovr_reader.c
        src/serialization/apta_wovr_reader_hardening.c
        src/serialization/apta_wovr_writer.c
        src/waveform/apta_waveform_detail_mvp.c
        src/waveform/apta_waveform_detail_replay.c
        src/waveform/apta_waveform_detail_replay_hardening.c
        src/waveform/apta_waveform_detail_requests.c
        src/waveform/apta_waveform_input.c
        src/waveform/apta_waveform_input_detail.c
        src/waveform/apta_waveform_process.c
        src/waveform/apta_waveform_process_detail.c
        src/waveform/apta_waveform_snapshot.c
        src/waveform/apta_waveform_snapshot_combined.c)

add_library(apta::core ALIAS apta_core)

set_target_properties(
    apta_core
    PROPERTIES
        OUTPUT_NAME apta
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)

target_link_libraries(
    apta_core
    PUBLIC
        apta::headers
        apta_sanitizers)

if(UNIX)
    target_link_libraries(apta_core PRIVATE m)
endif()

target_include_directories(
    apta_core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

target_compile_definitions(apta_core PRIVATE APTA_BUILDING_LIBRARY=1)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(apta_core PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(apta_core PRIVATE /W4)
endif()

if(APTA_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
