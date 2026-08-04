# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(APTAInterchangeBridge VERSION 1.0.0 LANGUAGES C)

find_package(APTA 1.0 CONFIG REQUIRED)

add_executable(apta_interchange_bridge main.c)
target_link_libraries(apta_interchange_bridge PRIVATE apta::core)
set_target_properties(
    apta_interchange_bridge
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(
        apta_interchange_bridge
        PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config}" "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(apta_interchange_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    target_compile_options(apta_interchange_bridge PRIVATE /W4 /WX)
endif()
