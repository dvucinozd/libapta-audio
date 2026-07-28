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

option(APTA_BUILD_TESTS "Build libapta tests" ON)

if(APTA_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
