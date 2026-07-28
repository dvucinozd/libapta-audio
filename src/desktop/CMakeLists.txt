# SPDX-License-Identifier: Apache-2.0

add_library(
    apta_port_posix
    STATIC
        apta_posix_file.c)
add_library(apta::port_posix ALIAS apta_port_posix)

target_link_libraries(
    apta_port_posix
    PUBLIC
        apta::headers
    PRIVATE
        apta_sanitizers)

target_compile_definitions(
    apta_port_posix
    PRIVATE
        _FILE_OFFSET_BITS=64)

set_target_properties(
    apta_port_posix
    PROPERTIES
        OUTPUT_NAME apta_port_posix
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS YES)

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
        apta::port_posix
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
    target_compile_options(apta_port_posix PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(apta_decoder_wav PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(apta_port_posix PRIVATE /W4)
    target_compile_options(apta_decoder_wav PRIVATE /W4)
endif()
