# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)

project(
    libapta
    VERSION 0.1.0
    LANGUAGES C CXX)

option(APTA_BUILD_TESTS "Build libapta tests" ON)
option(APTA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(APTA_BUILD_FUZZING "Build libFuzzer targets" OFF)
option(APTA_BUILD_DESKTOP_ADAPTERS "Build POSIX and reference decoder adapters" ON)

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
    src/core/apta_config.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_query_memory_requirements=apta_query_memory_requirements_base")

set_source_files_properties(
    src/serialization/apta_wovr_reader.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_base")

set_source_files_properties(
    src/serialization/apta_wovr_reader_hardening.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_wovr_hardened")

set_source_files_properties(
    src/serialization/apta_wdtl_reader.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_waveform")

set_source_files_properties(
    src/serialization/apta_meta_reader.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_meta_base")

set_source_files_properties(
    src/serialization/apta_meta_reader_hardening.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_meta_hardened")

set_source_files_properties(
    src/serialization/apta_wovr_writer.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_query_serialized_size=apta_result_query_serialized_size_wovr;apta_result_serialize=apta_result_serialize_wovr")

set_source_files_properties(
    src/serialization/apta_wdtl_writer.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_query_serialized_size=apta_result_query_serialized_size_waveform;apta_result_serialize=apta_result_serialize_waveform")

set_source_files_properties(
    src/serialization/apta_meta_writer.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_query_serialized_size=apta_result_query_serialized_size_meta;apta_result_serialize=apta_result_serialize_meta")

set_source_files_properties(
    src/core/apta_metadata.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_set_metadata=apta_session_set_metadata_base")

set_source_files_properties(
    src/core/apta_session_metadata_workspace.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_set_metadata=apta_session_set_metadata_workspace_base")

set_source_files_properties(
    src/core/apta_result_lifetime.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_publish_result=apta_internal_publish_result_base")

set_source_files_properties(
    src/core/apta_session_lifecycle.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_create=apta_session_create_base;apta_session_destroy=apta_session_destroy_heap_base;apta_session_process=apta_session_process_base")

set_source_files_properties(
    src/core/apta_session_detail_contract.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_create=apta_session_create_contract_base")

set_source_files_properties(
    src/core/apta_session_workspace_lifecycle.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_create=apta_session_create_workspace_base")

set_source_files_properties(
    src/core/apta_session_result_pool_activation.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_create=apta_session_create_activation_base;apta_session_process=apta_session_process_activation_base")

set_source_files_properties(
    src/core/apta_session_source.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_set_source=apta_session_set_source_base;apta_session_push_pcm=apta_session_push_pcm_base;apta_session_signal_end_of_input=apta_session_signal_end_of_input_base")

set_source_files_properties(
    src/core/apta_session_scheduler.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_set_focus=apta_session_set_focus_base;apta_session_request_region=apta_session_request_region_base;apta_session_next_pcm_request=apta_session_next_pcm_request_base")

set_source_files_properties(
    src/core/apta_result_accessors.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_get_feature_state=apta_result_get_feature_state_base;apta_result_get_waveform_tile=apta_result_get_waveform_tile_base;apta_result_get_tempo=apta_result_get_tempo_base;apta_result_get_beatgrid=apta_result_get_beatgrid_base")

set_source_files_properties(
    src/core/apta_result_detail_accessors.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_get_feature_state=apta_result_get_feature_state_s4_base")

set_source_files_properties(
    src/core/apta_result_pool_snapshot.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_result_pool_create_session_result=apta_internal_result_pool_create_session_result_base")

set_source_files_properties(
    src/waveform/apta_waveform_input.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_accept_pcm=apta_internal_waveform_accept_pcm_base")

set_source_files_properties(
    src/waveform/apta_waveform_process.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_base;apta_internal_waveform_next_pcm_request=apta_internal_waveform_next_pcm_request_base")

set_source_files_properties(
    src/waveform/apta_waveform_process_detail.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_s4_base")

set_source_files_properties(
    src/waveform/apta_waveform_snapshot.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_build_snapshot=apta_internal_waveform_build_overview_snapshot;apta_internal_waveform_cleanup_session=apta_internal_waveform_cleanup_session_base;apta_internal_waveform_cleanup_result=apta_internal_waveform_cleanup_result_base")

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
        src/core/apta_metadata.c
        src/core/apta_result_accessors.c
        src/core/apta_result_detail_accessors.c
        src/core/apta_result_lifetime.c
        src/core/apta_result_pool.c
        src/core/apta_result_pool_layout.c
        src/core/apta_result_pool_publication.c
        src/core/apta_result_pool_snapshot.c
        src/core/apta_scheduler_policy.c
        src/core/apta_session_detail_contract.c
        src/core/apta_session_lifecycle.c
        src/core/apta_session_metadata_bounded_guard.c
        src/core/apta_session_metadata_workspace.c
        src/core/apta_session_pull.c
        src/core/apta_session_pull_activation.c
        src/core/apta_session_result_pool_activation.c
        src/core/apta_session_scheduler.c
        src/core/apta_session_source.c
        src/core/apta_session_workspace.c
        src/core/apta_session_workspace_lifecycle.c
        src/serialization/apta_crc32c.c
        src/serialization/apta_meta_reader.c
        src/serialization/apta_meta_reader_hardening.c
        src/serialization/apta_meta_writer.c
        src/serialization/apta_s4_reader.c
        src/serialization/apta_s4_writer.c
        src/serialization/apta_wdtl_reader.c
        src/serialization/apta_wdtl_writer.c
        src/serialization/apta_wovr_reader.c
        src/serialization/apta_wovr_reader_hardening.c
        src/serialization/apta_wovr_writer.c
        src/tempo/apta_s4.c
        src/tempo/apta_s4_accessors.c
        src/tempo/apta_s4_cleanup.c
        src/tempo/apta_s4_pool_snapshot.c
        src/tempo/apta_s4_process.c
        src/waveform/apta_waveform_detail_mvp.c
        src/waveform/apta_waveform_detail_replay.c
        src/waveform/apta_waveform_detail_replay_hardening.c
        src/waveform/apta_waveform_detail_requests.c
        src/waveform/apta_waveform_input.c
        src/waveform/apta_waveform_input_detail.c
        src/waveform/apta_waveform_process.c
        src/waveform/apta_waveform_process_detail.c
        src/waveform/apta_waveform_scheduler.c
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

if(APTA_BUILD_DESKTOP_ADAPTERS)
    if(NOT UNIX)
        message(FATAL_ERROR "APTA_BUILD_DESKTOP_ADAPTERS currently requires a POSIX platform")
    endif()
    add_subdirectory(src/desktop)
endif()

if(APTA_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
