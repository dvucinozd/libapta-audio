# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)

project(
    libapta
    VERSION 0.1.0
    LANGUAGES C CXX)

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

add_library(
    apta_core
    STATIC
        src/core/apta_config.c
        src/core/apta_context.c
        src/core/apta_memory.c
        src/core/apta_result.c
        src/core/apta_session_lifecycle.c
        src/core/apta_session_scheduler.c
        src/core/apta_session_source.c)

add_library(apta::core ALIAS apta_core)

set_target_properties(
    apta_core
    PROPERTIES
        OUTPUT_NAME apta
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)

target_link_libraries(apta_core PUBLIC apta::headers)

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

option(APTA_BUILD_TESTS "Build libapta tests" ON)

if(APTA_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
