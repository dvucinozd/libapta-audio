# SPDX-License-Identifier: Apache-2.0

add_library(
    apta_tool_common
    STATIC
        apta_tool_common.c)

target_link_libraries(
    apta_tool_common
    PUBLIC
        apta::core
        apta::port_posix
    PRIVATE
        apta_sanitizers)

target_compile_definitions(
    apta_tool_common
    PRIVATE
        _POSIX_C_SOURCE=200809L)

set_target_properties(
    apta_tool_common
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)

function(apta_add_tool target output_name source)
    add_executable(${target} ${source})
    target_link_libraries(
        ${target}
        PRIVATE
            apta_tool_common
            apta::core
            apta::port_posix
            apta::decoder_wav
            apta_sanitizers)
    target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
    set_target_properties(
        ${target}
        PROPERTIES
            OUTPUT_NAME ${output_name}
            C_STANDARD 11
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    endif()
endfunction()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(apta_tool_common PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(apta_tool_common PRIVATE /W4)
endif()

apta_add_tool(apta_analyze apta-analyze apta_analyze.c)
apta_add_tool(apta_inspect apta-inspect apta_inspect.c)
apta_add_tool(apta_validate apta-validate apta_validate.c)
