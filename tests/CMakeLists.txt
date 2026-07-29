# SPDX-License-Identifier: Apache-2.0

find_package(Threads REQUIRED)

add_library(apta_headers_c OBJECT compile/headers_c.c)
target_link_libraries(apta_headers_c PRIVATE apta::headers)
set_target_properties(apta_headers_c PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_library(apta_headers_cpp OBJECT compile/headers_cpp.cpp)
target_link_libraries(apta_headers_cpp PRIVATE apta::headers)
set_target_properties(apta_headers_cpp PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)

add_library(apta_abi_layout OBJECT compile/abi_layout.c)
target_link_libraries(apta_abi_layout PRIVATE apta::headers)
set_target_properties(apta_abi_layout PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

function(apta_add_c_test target test_name source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE apta::core)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

function(apta_add_cpp_test target test_name source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE apta::core Threads::Threads)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

function(apta_add_desktop_test target test_name source)
    add_executable(${target} ${source})
    target_link_libraries(
        ${target}
        PRIVATE
            apta::core
            apta::decoder_wav
            apta::port_posix)
    target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

apta_add_c_test(apta_core_smoke apta.core.smoke unit/core_smoke.c)
apta_add_c_test(apta_allocation_failure apta.core.allocation_failure unit/allocation_failure.c)
apta_add_c_test(apta_cancellation apta.core.cancellation unit/cancellation.c)
apta_add_c_test(apta_initializers apta.api.initializers unit/initializers.c)
apta_add_c_test(apta_version_validation apta.api.version_validation unit/version_validation.c)
apta_add_c_test(apta_metadata_lifecycle apta.metadata.lifecycle unit/metadata_lifecycle.c)
apta_add_c_test(apta_pcm_pull apta.source.pcm_pull unit/pcm_pull.c)
apta_add_c_test(apta_pcm_pull_bounded apta.source.pcm_pull_bounded unit/pcm_pull_bounded.c)

apta_add_c_test(apta_waveform_overview apta.waveform.overview unit/waveform_overview.c)
apta_add_c_test(apta_waveform_determinism apta.waveform.determinism unit/waveform_determinism.c)
apta_add_c_test(apta_waveform_priority apta.waveform.priority unit/waveform_priority.c)
apta_add_c_test(apta_scheduler_deadline apta.scheduler.deadline unit/scheduler_deadline.c)
apta_add_c_test(apta_scheduler_process_deadline apta.scheduler.process_deadline unit/scheduler_process_deadline.c)
apta_add_c_test(apta_scheduler_aging apta.scheduler.aging unit/scheduler_aging.c)
apta_add_c_test(apta_waveform_contracts apta.waveform.contracts unit/waveform_contracts.c)
apta_add_c_test(apta_waveform_detail apta.waveform.detail unit/waveform_detail.c)
apta_add_c_test(apta_waveform_detail_contracts apta.waveform.detail_contracts unit/waveform_detail_contracts.c)
apta_add_c_test(apta_waveform_detail_replay apta.waveform.detail_replay unit/waveform_detail_replay.c)

apta_add_c_test(apta_s4_tempo_grid apta.s4.tempo_grid unit/s4_tempo_grid.c)
apta_add_c_test(apta_s4_silence apta.s4.silence unit/s4_silence.c)
apta_add_c_test(apta_s4_bounded apta.s4.bounded unit/s4_bounded.c)
apta_add_c_test(apta_s4_pull apta.s4.pull unit/s4_pull.c)
apta_add_c_test(apta_s4_tempo_vectors apta.s4.tempo_vectors unit/s4_tempo_vectors.c)
apta_add_c_test(apta_s4_focus apta.s4.focus unit/s4_focus.c)
apta_add_c_test(apta_s4_serialization apta.serialization.s4 unit/s4_serialization.c)
apta_add_c_test(apta_s4_allocation_failure apta.serialization.s4_allocation_failure unit/s4_allocation_failure.c)

apta_add_c_test(apta_s6_global_grid apta.s6.global_grid unit/s6_global_grid.c)
apta_add_c_test(apta_s6_revision apta.s6.revision unit/s6_revision.c)
apta_add_c_test(apta_s6_bounded apta.s6.bounded unit/s6_bounded.c)
apta_add_c_test(apta_s6_serialization apta.serialization.s6 unit/s6_serialization.c)
target_include_directories(apta_s6_serialization PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
apta_add_c_test(apta_s6_allocation_failure apta.serialization.s6_allocation_failure unit/s6_allocation_failure.c)

add_executable(
    apta_espidf_port
    unit/espidf_port.c
    ${CMAKE_SOURCE_DIR}/ports/espidf/apta_espidf.c)
target_link_libraries(apta_espidf_port PRIVATE apta::core)
target_include_directories(
    apta_espidf_port
    PRIVATE
        ${CMAKE_SOURCE_DIR}/ports/espidf/include
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs/espidf)
target_compile_definitions(apta_espidf_port PRIVATE CONFIG_APTA_ESP_DSP_BACKEND=0)
set_target_properties(apta_espidf_port PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
add_test(NAME apta.port.espidf COMMAND apta_espidf_port)

if(TARGET apta_decoder_wav)
    apta_add_desktop_test(apta_desktop_posix_file apta.desktop.posix_file unit/desktop_posix_file.c)
    apta_add_desktop_test(apta_desktop_wav_decoder apta.desktop.wav_decoder unit/desktop_wav_decoder.c)
    apta_add_desktop_test(apta_desktop_wav_malformed apta.desktop.wav_malformed unit/desktop_wav_malformed.c)
    apta_add_desktop_test(apta_desktop_wav_pull apta.desktop.wav_pull unit/desktop_wav_pull_integration.c)
endif()

if(TARGET apta_analyze AND TARGET apta_inspect AND TARGET apta_validate)
    set(APTA_CLI_WAV "${CMAKE_CURRENT_BINARY_DIR}/apta-cli-click.wav")
    set(APTA_CLI_CONTAINER "${CMAKE_CURRENT_BINARY_DIR}/apta-cli-result.apta")
    set(APTA_CLI_CORRUPT "${CMAKE_CURRENT_BINARY_DIR}/apta-cli-corrupt.apta")

    add_executable(apta_desktop_cli_fixture unit/desktop_cli_fixture.c)
    target_compile_definitions(apta_desktop_cli_fixture PRIVATE _POSIX_C_SOURCE=200809L)
    set_target_properties(
        apta_desktop_cli_fixture
        PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES)

    add_test(
        NAME apta.tools.fixture_wav
        COMMAND apta_desktop_cli_fixture wav ${APTA_CLI_WAV})
    set_tests_properties(
        apta.tools.fixture_wav
        PROPERTIES
            FIXTURES_SETUP apta_cli_wav)

    add_test(
        NAME apta.tools.analyze
        COMMAND apta_analyze ${APTA_CLI_WAV}
                --output ${APTA_CLI_CONTAINER}
                --profile performance)
    set_tests_properties(
        apta.tools.analyze
        PROPERTIES
            FIXTURES_REQUIRED apta_cli_wav
            FIXTURES_SETUP apta_cli_container
            PASS_REGULAR_EXPRESSION "analyzed")

    add_test(
        NAME apta.tools.validate_strict
        COMMAND apta_validate ${APTA_CLI_CONTAINER} --strict)
    set_tests_properties(
        apta.tools.validate_strict
        PROPERTIES
            FIXTURES_REQUIRED apta_cli_container
            PASS_REGULAR_EXPRESSION "valid:")

    add_test(
        NAME apta.tools.inspect_json
        COMMAND apta_inspect ${APTA_CLI_CONTAINER} --json --section TEMP)
    set_tests_properties(
        apta.tools.inspect_json
        PROPERTIES
            FIXTURES_REQUIRED apta_cli_container
            PASS_REGULAR_EXPRESSION "tempo_millibpm")

    add_test(
        NAME apta.tools.fixture_corrupt
        COMMAND apta_desktop_cli_fixture truncate
                ${APTA_CLI_CONTAINER} ${APTA_CLI_CORRUPT})
    set_tests_properties(
        apta.tools.fixture_corrupt
        PROPERTIES
            FIXTURES_REQUIRED apta_cli_container
            FIXTURES_SETUP apta_cli_corrupt)

    add_test(
        NAME apta.tools.reject_corrupt
        COMMAND apta_validate ${APTA_CLI_CORRUPT} --strict --quiet)
    set_tests_properties(
        apta.tools.reject_corrupt
        PROPERTIES
            FIXTURES_REQUIRED apta_cli_corrupt
            WILL_FAIL TRUE)
endif()

apta_add_c_test(apta_memory_requirements apta.memory.requirements unit/memory_requirements.c)
apta_add_c_test(apta_static_workspace apta.memory.static_workspace unit/static_workspace.c)
apta_add_c_test(apta_static_workspace_metadata apta.memory.static_workspace_metadata unit/static_workspace_metadata.c)
apta_add_c_test(apta_result_pool_layout apta.memory.result_pool_layout unit/result_pool_layout.c)
apta_add_c_test(apta_result_pool_storage apta.memory.result_pool_storage unit/result_pool_storage.c)
target_include_directories(apta_result_pool_storage PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
apta_add_c_test(apta_result_pool_empty_result apta.result.pool_empty_result unit/result_pool_empty_result.c)
target_include_directories(apta_result_pool_empty_result PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
apta_add_c_test(apta_bounded_session_initial_result apta.session.bounded_initial_result unit/bounded_session_initial_result.c)
apta_add_c_test(apta_bounded_waveform_publication apta.result.bounded_waveform_publication unit/bounded_waveform_publication.c)
apta_add_c_test(apta_bounded_pool_capacity apta.memory.bounded_pool_capacity unit/bounded_pool_capacity.c)
apta_add_cpp_test(apta_bounded_result_concurrency apta.result.bounded_concurrency unit/bounded_result_concurrency.cpp)
apta_add_c_test(apta_publication_retry apta.result.publication_retry unit/publication_retry.c)

apta_add_c_test(apta_wovr_writer apta.serialization.wovr_writer unit/wovr_writer.c)
apta_add_c_test(apta_wovr_writer_partial apta.serialization.wovr_writer_partial unit/wovr_writer_partial.c)
apta_add_c_test(apta_wovr_roundtrip apta.serialization.wovr_roundtrip unit/wovr_roundtrip.c)
apta_add_c_test(apta_wovr_reader_malformed apta.serialization.wovr_reader_malformed unit/wovr_reader_malformed.c)
apta_add_c_test(apta_wovr_reader_hardening apta.serialization.wovr_reader_hardening unit/wovr_reader_hardening.c)
apta_add_c_test(apta_wovr_reader_allocation_failure apta.serialization.wovr_reader_allocation_failure unit/wovr_reader_allocation_failure.c)
apta_add_c_test(apta_wdtl_roundtrip apta.serialization.wdtl_roundtrip unit/wdtl_roundtrip.c)
apta_add_c_test(apta_wdtl_reader_malformed apta.serialization.wdtl_reader_malformed unit/wdtl_reader_malformed.c)
apta_add_c_test(apta_wdtl_reader_allocation_failure apta.serialization.wdtl_reader_allocation_failure unit/wdtl_reader_allocation_failure.c)
apta_add_c_test(apta_meta_roundtrip apta.serialization.meta_roundtrip unit/meta_roundtrip.c)
apta_add_c_test(apta_meta_wdtl_roundtrip apta.serialization.meta_wdtl_roundtrip unit/meta_wdtl_roundtrip.c)
apta_add_c_test(apta_meta_reader_malformed apta.serialization.meta_reader_malformed unit/meta_reader_malformed.c)
apta_add_c_test(apta_meta_allocation_failure apta.serialization.meta_allocation_failure unit/meta_allocation_failure.c)
apta_add_c_test(apta_container_truncation apta.serialization.container_truncation unit/container_truncation.c)

apta_add_cpp_test(apta_result_concurrency apta.result.concurrency unit/result_concurrency.cpp)
apta_add_cpp_test(apta_cancellation_thread apta.core.cancellation_thread unit/cancellation_thread.cpp)

if(APTA_BUILD_FUZZING)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "APTA_BUILD_FUZZING requires Clang and libFuzzer")
    endif()

    add_executable(apta_wovr_seed_generator fuzz/wovr_seed_generator.c)
    target_link_libraries(apta_wovr_seed_generator PRIVATE apta::core)
    set_target_properties(
        apta_wovr_seed_generator
        PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES)

    add_executable(apta_s6_seed_generator fuzz/s6_seed_generator.c)
    target_link_libraries(apta_s6_seed_generator PRIVATE apta::core)
    set_target_properties(
        apta_s6_seed_generator
        PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES)

    add_executable(apta_wovr_reader_fuzz fuzz/wovr_reader_fuzz.c)
    target_link_libraries(apta_wovr_reader_fuzz PRIVATE apta::core)
    target_compile_options(apta_wovr_reader_fuzz PRIVATE -fsanitize=fuzzer)
    target_link_options(apta_wovr_reader_fuzz PRIVATE -fsanitize=fuzzer)
    set_target_properties(
        apta_wovr_reader_fuzz
        PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED YES)
endif()
