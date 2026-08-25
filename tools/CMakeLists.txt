# SPDX-License-Identifier: Apache-2.0

add_library(
    apta_tool_common
    STATIC
        apta_tool_common.c)

target_link_libraries(
    apta_tool_common
    PUBLIC
        apta::core
        apta::port_native
    PRIVATE
        apta_sanitizers)

if(UNIX)
    target_compile_definitions(
        apta_tool_common
        PRIVATE
            _POSIX_C_SOURCE=200809L)
endif()

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
            apta::port_native
            apta::decoder_wav
            apta_sanitizers)
    if(UNIX)
        target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(${target} PRIVATE m)
    endif()
    set_target_properties(
        ${target}
        PROPERTIES
            OUTPUT_NAME ${output_name}
            C_STANDARD 11
            C_STANDARD_REQUIRED YES
            C_EXTENSIONS NO)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(APTA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        # The tools intentionally use portable C stdio APIs such as fopen.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target} PRIVATE /W4)
        if(APTA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
endfunction()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(apta_tool_common PRIVATE -Wall -Wextra -Wpedantic)
    if(APTA_WARNINGS_AS_ERRORS)
        target_compile_options(apta_tool_common PRIVATE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(apta_tool_common PRIVATE /W4)
    if(APTA_WARNINGS_AS_ERRORS)
        target_compile_options(apta_tool_common PRIVATE /WX)
    endif()
endif()

# Measurement instrument rather than a deliverable, but it needs the WAV
# decoder for --tracks, so it is built like the tools rather than by hand.
apta_add_tool(apta_tempo_corpus apta-tempo-corpus apta_tempo_corpus.c)
apta_add_tool(apta_workspace_probe apta-workspace-probe apta_workspace_probe.c)

if(APTA_ENABLE_INTERNAL_PROFILING)
    apta_add_tool(apta_cost_probe apta-cost-probe apta_cost_probe.c)
    target_compile_definitions(
        apta_cost_probe
        PRIVATE
            APTA_INTERNAL_PROFILE_S4=1)
    target_include_directories(
        apta_cost_probe
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)
endif()

if(APTA_ENABLE_EXPERIMENTAL_METER_TRACE)
    # Development diagnostic instrument. It links the library's private meter
    # header and must be built with the same trace macro as the library, so it
    # is only defined when explicitly requested.
    apta_add_tool(apta_meter_trace apta-meter-trace apta_meter_trace.c)
    target_compile_definitions(
        apta_meter_trace
        PRIVATE
            APTA_INTERNAL_METER_TRACE=1)
    target_include_directories(
        apta_meter_trace
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/beatgrid)
endif()

apta_add_tool(apta_analyze apta-analyze apta_analyze.c)
apta_add_tool(apta_inspect apta-inspect apta_inspect.c)
apta_add_tool(apta_validate apta-validate apta_validate.c)
apta_add_tool(apta_version apta-version apta_version.c)
