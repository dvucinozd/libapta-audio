# SPDX-License-Identifier: Apache-2.0

function(apta_add_example target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE apta::core)
    target_compile_features(${target} PRIVATE c_std_11)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(APTA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(APTA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()

    if(BUILD_TESTING)
        add_test(NAME apta.example.${target} COMMAND ${target})
        set_tests_properties(apta.example.${target} PROPERTIES LABELS examples)
    endif()
endfunction()

apta_add_example(apta_pcm_push_example pcm_push/main.c)
apta_add_example(apta_pcm_pull_example pcm_pull/main.c)
