# SPDX-License-Identifier: Apache-2.0

if(WIN32)
    add_library(
        apta_port_windows
        STATIC
            ${CMAKE_SOURCE_DIR}/ports/windows/apta_windows_file.c)
    add_library(apta::port_windows ALIAS apta_port_windows)
    add_library(apta::port_native ALIAS apta_port_windows)
    set(APTA_NATIVE_PORT_TARGET apta_port_windows)
elseif(UNIX)
    add_library(
        apta_port_posix
        STATIC
            apta_posix_file.c)
    add_library(apta::port_posix ALIAS apta_port_posix)
    add_library(apta::port_native ALIAS apta_port_posix)
    set(APTA_NATIVE_PORT_TARGET apta_port_posix)
    target_compile_definitions(
        apta_port_posix
        PRIVATE
            _FILE_OFFSET_BITS=64)
endif()

target_link_libraries(
    ${APTA_NATIVE_PORT_TARGET}
    PUBLIC
        apta::headers
    PRIVATE
        apta_sanitizers)

set_target_properties(
    ${APTA_NATIVE_PORT_TARGET}
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES)

add_library(
    apta_decoder_wav
    STATIC
        apta_decoder.c
        apta_wav_decoder.c)
add_library(apta::decoder_wav ALIAS apta_decoder_wav)

target_link_libraries(
    apta_decoder_wav
    PUBLIC
        apta::headers
        apta::core
        apta::port_native
    PRIVATE
        apta_sanitizers)

set_target_properties(
    apta_decoder_wav
    PROPERTIES
        OUTPUT_NAME apta_decoder_wav
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${APTA_NATIVE_PORT_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(apta_decoder_wav PRIVATE -Wall -Wextra -Wpedantic)
    if(APTA_WARNINGS_AS_ERRORS)
        target_compile_options(${APTA_NATIVE_PORT_TARGET} PRIVATE -Werror)
        target_compile_options(apta_decoder_wav PRIVATE -Werror)
    endif()
elseif(MSVC)
    target_compile_options(${APTA_NATIVE_PORT_TARGET} PRIVATE /W4)
    target_compile_options(apta_decoder_wav PRIVATE /W4)
    if(APTA_WARNINGS_AS_ERRORS)
        target_compile_options(${APTA_NATIVE_PORT_TARGET} PRIVATE /WX)
        target_compile_options(apta_decoder_wav PRIVATE /WX)
    endif()
endif()
