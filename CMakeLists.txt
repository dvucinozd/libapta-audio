# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APTA_VERSION_FULL)
string(STRIP "${APTA_VERSION_FULL}" APTA_VERSION_FULL)
if(NOT APTA_VERSION_FULL MATCHES "^([0-9]+\.[0-9]+\.[0-9]+)(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "VERSION must be SemVer, got: ${APTA_VERSION_FULL}")
endif()
set(APTA_VERSION_NUMERIC "${CMAKE_MATCH_1}")

project(
    libapta
    VERSION ${APTA_VERSION_NUMERIC}
    LANGUAGES C CXX)

include(cmake/APTAVersionCheck.cmake)

option(APTA_BUILD_TESTS "Build libapta tests" ON)
option(APTA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(APTA_BUILD_FUZZING "Build libFuzzer targets" OFF)
option(APTA_BUILD_DESKTOP_ADAPTERS "Build native desktop and reference decoder adapters" ON)
option(APTA_BUILD_TOOLS "Build reference desktop command-line tools" ON)
option(APTA_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(
    APTA_ENABLE_INTERNAL_PROFILING
    "Build opt-in internal timing probes (never enable for a shipped library)"
    OFF)
option(
    APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET
    "Build the phase-6 B3 three-band onset novelty experiment"
    OFF)

if(APTA_ENABLE_EXPERIMENTAL_MULTIBAND_ONSET)
    # Directory scope deliberately reaches the internal tests and tools too.
    # The macro changes private structure layout, but never enters public
    # headers or an installed target's interface.
    add_compile_definitions(APTA_INTERNAL_MULTIBAND_ONSET=1)
endif()

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

add_library(apta_c11_atomics INTERFACE)

if(MSVC)
    include(CheckCSourceCompiles)

    set(APTA_SAVED_CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
    set(
        CMAKE_REQUIRED_FLAGS
        "${CMAKE_REQUIRED_FLAGS} /std:c11 /experimental:c11atomics")

    check_c_source_compiles(
        [=[
            #include <stddef.h>
            #include <stdint.h>
            #include <stdatomic.h>

            int main(void)
            {
                atomic_uint state;
                atomic_size_t allocated_bytes;
                atomic_uint_fast64_t counter;
                atomic_flag lock = ATOMIC_FLAG_INIT;
                unsigned int expected = 0u;

                atomic_init(&state, 0u);
                atomic_init(&allocated_bytes, (size_t)0u);
                atomic_init(&counter, UINT64_C(0));
                atomic_flag_clear_explicit(&lock, memory_order_release);

                (void)atomic_compare_exchange_strong_explicit(
                    &state,
                    &expected,
                    1u,
                    memory_order_acq_rel,
                    memory_order_acquire);
                (void)atomic_fetch_add_explicit(
                    &allocated_bytes,
                    (size_t)1u,
                    memory_order_relaxed);
                (void)atomic_fetch_add_explicit(
                    &counter,
                    UINT64_C(1),
                    memory_order_relaxed);
                (void)atomic_flag_test_and_set_explicit(
                    &lock,
                    memory_order_acquire);

                return (int)atomic_load_explicit(
                    &state,
                    memory_order_relaxed);
            }
        ]=]
        APTA_MSVC_C11_ATOMICS_AVAILABLE)

    set(CMAKE_REQUIRED_FLAGS "${APTA_SAVED_CMAKE_REQUIRED_FLAGS}")
    unset(APTA_SAVED_CMAKE_REQUIRED_FLAGS)

    if(NOT APTA_MSVC_C11_ATOMICS_AVAILABLE)
        message(
            FATAL_ERROR
            "MSVC C11 atomics are unavailable. "
            "Use Visual Studio 2022 17.5 or newer with "
            "/experimental:c11atomics support.")
    endif()

    target_compile_options(
        apta_c11_atomics
        INTERFACE
            "$<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>")
endif()

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
    src/serialization/apta_s4_reader.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_parse=apta_result_parse_s4")

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
    src/serialization/apta_s4_writer.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_query_serialized_size=apta_result_query_serialized_size_s4;apta_result_serialize=apta_result_serialize_s4")

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
    src/core/apta_session_pull_activation.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_session_create=apta_session_create_s6_guard_base")

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
    src/tempo/apta_s4_accessors.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_result_get_feature_state=apta_result_get_feature_state_s6_base;apta_result_get_beatgrid=apta_result_get_beatgrid_s6_base")

set_source_files_properties(
    src/core/apta_result_pool_snapshot.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_result_pool_create_session_result=apta_internal_result_pool_create_session_result_base")

set_source_files_properties(
    src/tempo/apta_s4_pool_snapshot.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_result_pool_create_session_result=apta_internal_result_pool_create_session_result_s6_base")

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
    src/tempo/apta_s4_process.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_process=apta_internal_waveform_process_s6_base")

set_source_files_properties(
    src/waveform/apta_waveform_snapshot.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_build_snapshot=apta_internal_waveform_build_overview_snapshot;apta_internal_waveform_cleanup_session=apta_internal_waveform_cleanup_session_base;apta_internal_waveform_cleanup_result=apta_internal_waveform_cleanup_result_base")

set_source_files_properties(
    src/tempo/apta_s4_cleanup.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_waveform_cleanup_session=apta_internal_waveform_cleanup_session_s6_base")

set_source_files_properties(
    src/waveform/apta_waveform_detail_replay.c
    PROPERTIES
        COMPILE_DEFINITIONS "apta_internal_detail_next_pcm_request=apta_internal_detail_next_pcm_request_base;apta_internal_detail_accept_replay=apta_internal_detail_accept_replay_base")

add_library(
    apta_core
    STATIC
        src/beatgrid/apta_s6.c
        src/beatgrid/apta_s6_accessors.c
        src/beatgrid/apta_s6_cleanup.c
        src/beatgrid/apta_s6_pool_snapshot.c
        src/beatgrid/apta_s6_process.c
        src/beatgrid/apta_s6_session_guard.c
        src/core/apta_config.c
        src/core/apta_context.c
        src/core/apta_initializers.c
        src/core/apta_memory.c
        src/core/apta_metadata.c
        src/core/apta_result_allocation.c
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
        src/serialization/apta_s6_reader.c
        src/serialization/apta_s6_writer.c
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

file(
    GLOB_RECURSE
    APTA_DISCOVERED_CORE_SOURCES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    CONFIGURE_DEPENDS
    src/core/*.c
    src/serialization/*.c
    src/tempo/*.c
    src/beatgrid/*.c
    src/waveform/*.c)
get_target_property(APTA_CONFIGURED_CORE_SOURCES apta_core SOURCES)
foreach(APTA_DISCOVERED_SOURCE IN LISTS APTA_DISCOVERED_CORE_SOURCES)
    list(
        FIND
        APTA_CONFIGURED_CORE_SOURCES
        "${APTA_DISCOVERED_SOURCE}"
        APTA_DISCOVERED_SOURCE_INDEX)
    if(APTA_DISCOVERED_SOURCE_INDEX EQUAL -1)
        message(
            FATAL_ERROR
            "Production source is not part of apta_core: ${APTA_DISCOVERED_SOURCE}")
    endif()
endforeach()

add_library(apta::core ALIAS apta_core)

if(APTA_ENABLE_INTERNAL_PROFILING)
    target_compile_definitions(apta_core PRIVATE APTA_INTERNAL_PROFILE_S4=1)
endif()

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
    PRIVATE
        apta_c11_atomics
    PUBLIC
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
    if(APTA_WARNINGS_AS_ERRORS)
        target_compile_options(apta_core PRIVATE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(apta_core PRIVATE /W4)
    if(APTA_WARNINGS_AS_ERRORS)
        target_compile_options(apta_core PRIVATE /WX)
    endif()
endif()

if(APTA_BUILD_DESKTOP_ADAPTERS)
    if(NOT UNIX AND NOT WIN32)
        message(FATAL_ERROR "APTA_BUILD_DESKTOP_ADAPTERS requires POSIX or Windows")
    endif()
    add_subdirectory(src/desktop)
endif()

if(APTA_BUILD_TOOLS)
    if(NOT APTA_BUILD_DESKTOP_ADAPTERS)
        message(FATAL_ERROR "APTA_BUILD_TOOLS requires APTA_BUILD_DESKTOP_ADAPTERS")
    endif()
    add_subdirectory(tools)
endif()

if(APTA_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
