# SPDX-License-Identifier: Apache-2.0
if(NOT APTA_BUILD_TESTS)
    return()
endif()
if(APTA_ENABLE_SANITIZERS OR APTA_BUILD_FUZZING)
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/interoperability/run_installed.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/run_installed_interchange.in.cmake"
    @ONLY)
file(
    GENERATE
    OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/run_installed_interchange-$<CONFIG>.cmake"
    INPUT
        "${CMAKE_CURRENT_BINARY_DIR}/run_installed_interchange.in.cmake")
add_test(
    NAME apta.interchange.versioned
    COMMAND
        "${CMAKE_COMMAND}"
        -DAPTA_TEST_CONFIG=$<CONFIG>
        -P
        "${CMAKE_CURRENT_BINARY_DIR}/run_installed_interchange-$<CONFIG>.cmake")
set_tests_properties(
    apta.interchange.versioned
    PROPERTIES
        LABELS "public_conformance;interoperability"
        TIMEOUT 600)
