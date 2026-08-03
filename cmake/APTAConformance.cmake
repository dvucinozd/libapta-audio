# SPDX-License-Identifier: Apache-2.0
if(NOT APTA_BUILD_TESTS)
    return()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(
    APTA_TEST_CLASSIFICATION_REPORT
    "${CMAKE_CURRENT_BINARY_DIR}/conformance-reports/test-classification-$<CONFIG>.json")
add_test(
    NAME apta.tests.classification
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/classification/check_ctest_inventory.py"
        --ctest "${CMAKE_CTEST_COMMAND}"
        --build-dir "${CMAKE_BINARY_DIR}"
        --config "$<CONFIG>"
        --manifest "${CMAKE_CURRENT_SOURCE_DIR}/tests/classification/rules.json"
        --output "${APTA_TEST_CLASSIFICATION_REPORT}")
set_tests_properties(
    apta.tests.classification
    PROPERTIES
        LABELS implementation_regression
        TIMEOUT 60)

if(NOT APTA_ENABLE_SANITIZERS AND NOT APTA_BUILD_FUZZING)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/run_installed.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/run_installed_conformance.in.cmake"
        @ONLY)
    file(
        GENERATE
        OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/run_installed_conformance-$<CONFIG>.cmake"
        INPUT
            "${CMAKE_CURRENT_BINARY_DIR}/run_installed_conformance.in.cmake")
    add_test(
        NAME apta.conformance.installed
        COMMAND
            "${CMAKE_COMMAND}"
            -DAPTA_TEST_CONFIG=$<CONFIG>
            -P
            "${CMAKE_CURRENT_BINARY_DIR}/run_installed_conformance-$<CONFIG>.cmake")
    set_tests_properties(
        apta.conformance.installed
        PROPERTIES
            LABELS public_conformance
            TIMEOUT 600)
endif()
