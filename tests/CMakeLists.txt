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

add_executable(apta_core_smoke unit/core_smoke.c)
target_link_libraries(apta_core_smoke PRIVATE apta::core)
set_target_properties(apta_core_smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_allocation_failure unit/allocation_failure.c)
target_link_libraries(apta_allocation_failure PRIVATE apta::core)
set_target_properties(apta_allocation_failure PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_cancellation unit/cancellation.c)
target_link_libraries(apta_cancellation PRIVATE apta::core)
set_target_properties(apta_cancellation PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_initializers unit/initializers.c)
target_link_libraries(apta_initializers PRIVATE apta::core)
set_target_properties(apta_initializers PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_version_validation unit/version_validation.c)
target_link_libraries(apta_version_validation PRIVATE apta::core)
set_target_properties(apta_version_validation PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_metadata_lifecycle unit/metadata_lifecycle.c)
target_link_libraries(apta_metadata_lifecycle PRIVATE apta::core)
set_target_properties(apta_metadata_lifecycle PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_overview unit/waveform_overview.c)
target_link_libraries(apta_waveform_overview PRIVATE apta::core)
set_target_properties(apta_waveform_overview PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_determinism unit/waveform_determinism.c)
target_link_libraries(apta_waveform_determinism PRIVATE apta::core)
set_target_properties(apta_waveform_determinism PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_priority unit/waveform_priority.c)
target_link_libraries(apta_waveform_priority PRIVATE apta::core)
set_target_properties(apta_waveform_priority PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_scheduler_deadline unit/scheduler_deadline.c)
target_link_libraries(apta_scheduler_deadline PRIVATE apta::core)
set_target_properties(apta_scheduler_deadline PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_scheduler_process_deadline unit/scheduler_process_deadline.c)
target_link_libraries(apta_scheduler_process_deadline PRIVATE apta::core)
set_target_properties(apta_scheduler_process_deadline PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_scheduler_aging unit/scheduler_aging.c)
target_link_libraries(apta_scheduler_aging PRIVATE apta::core)
set_target_properties(apta_scheduler_aging PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_contracts unit/waveform_contracts.c)
target_link_libraries(apta_waveform_contracts PRIVATE apta::core)
set_target_properties(apta_waveform_contracts PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_detail unit/waveform_detail.c)
target_link_libraries(apta_waveform_detail PRIVATE apta::core)
set_target_properties(apta_waveform_detail PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_detail_contracts unit/waveform_detail_contracts.c)
target_link_libraries(apta_waveform_detail_contracts PRIVATE apta::core)
set_target_properties(apta_waveform_detail_contracts PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_detail_replay unit/waveform_detail_replay.c)
target_link_libraries(apta_waveform_detail_replay PRIVATE apta::core)
set_target_properties(apta_waveform_detail_replay PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_memory_requirements unit/memory_requirements.c)
target_link_libraries(apta_memory_requirements PRIVATE apta::core)
set_target_properties(apta_memory_requirements PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_publication_retry unit/publication_retry.c)
target_link_libraries(apta_publication_retry PRIVATE apta::core)
set_target_properties(apta_publication_retry PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wovr_writer unit/wovr_writer.c)
target_link_libraries(apta_wovr_writer PRIVATE apta::core)
set_target_properties(apta_wovr_writer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wovr_writer_partial unit/wovr_writer_partial.c)
target_link_libraries(apta_wovr_writer_partial PRIVATE apta::core)
set_target_properties(apta_wovr_writer_partial PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wovr_roundtrip unit/wovr_roundtrip.c)
target_link_libraries(apta_wovr_roundtrip PRIVATE apta::core)
set_target_properties(apta_wovr_roundtrip PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wovr_reader_malformed unit/wovr_reader_malformed.c)
target_link_libraries(apta_wovr_reader_malformed PRIVATE apta::core)
set_target_properties(apta_wovr_reader_malformed PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wovr_reader_hardening unit/wovr_reader_hardening.c)
target_link_libraries(apta_wovr_reader_hardening PRIVATE apta::core)
set_target_properties(apta_wovr_reader_hardening PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wovr_reader_allocation_failure unit/wovr_reader_allocation_failure.c)
target_link_libraries(apta_wovr_reader_allocation_failure PRIVATE apta::core)
set_target_properties(apta_wovr_reader_allocation_failure PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wdtl_roundtrip unit/wdtl_roundtrip.c)
target_link_libraries(apta_wdtl_roundtrip PRIVATE apta::core)
set_target_properties(apta_wdtl_roundtrip PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wdtl_reader_malformed unit/wdtl_reader_malformed.c)
target_link_libraries(apta_wdtl_reader_malformed PRIVATE apta::core)
set_target_properties(apta_wdtl_reader_malformed PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_wdtl_reader_allocation_failure unit/wdtl_reader_allocation_failure.c)
target_link_libraries(apta_wdtl_reader_allocation_failure PRIVATE apta::core)
set_target_properties(apta_wdtl_reader_allocation_failure PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_meta_roundtrip unit/meta_roundtrip.c)
target_link_libraries(apta_meta_roundtrip PRIVATE apta::core)
set_target_properties(apta_meta_roundtrip PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_meta_reader_malformed unit/meta_reader_malformed.c)
target_link_libraries(apta_meta_reader_malformed PRIVATE apta::core)
set_target_properties(apta_meta_reader_malformed PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_meta_allocation_failure unit/meta_allocation_failure.c)
target_link_libraries(apta_meta_allocation_failure PRIVATE apta::core)
set_target_properties(apta_meta_allocation_failure PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_result_concurrency unit/result_concurrency.cpp)
target_link_libraries(apta_result_concurrency PRIVATE apta::core Threads::Threads)
set_target_properties(apta_result_concurrency PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)

add_executable(apta_cancellation_thread unit/cancellation_thread.cpp)
target_link_libraries(apta_cancellation_thread PRIVATE apta::core Threads::Threads)
set_target_properties(apta_cancellation_thread PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)

add_test(NAME apta.core.smoke COMMAND apta_core_smoke)
add_test(NAME apta.core.allocation_failure COMMAND apta_allocation_failure)
add_test(NAME apta.core.cancellation COMMAND apta_cancellation)
add_test(NAME apta.api.initializers COMMAND apta_initializers)
add_test(NAME apta.api.version_validation COMMAND apta_version_validation)
add_test(NAME apta.metadata.lifecycle COMMAND apta_metadata_lifecycle)
add_test(NAME apta.waveform.overview COMMAND apta_waveform_overview)
add_test(NAME apta.waveform.determinism COMMAND apta_waveform_determinism)
add_test(NAME apta.waveform.priority COMMAND apta_waveform_priority)
add_test(NAME apta.scheduler.deadline COMMAND apta_scheduler_deadline)
add_test(NAME apta.scheduler.process_deadline COMMAND apta_scheduler_process_deadline)
add_test(NAME apta.scheduler.aging COMMAND apta_scheduler_aging)
add_test(NAME apta.waveform.contracts COMMAND apta_waveform_contracts)
add_test(NAME apta.waveform.detail COMMAND apta_waveform_detail)
add_test(NAME apta.waveform.detail_contracts COMMAND apta_waveform_detail_contracts)
add_test(NAME apta.waveform.detail_replay COMMAND apta_waveform_detail_replay)
add_test(NAME apta.memory.requirements COMMAND apta_memory_requirements)
add_test(NAME apta.result.publication_retry COMMAND apta_publication_retry)
add_test(NAME apta.serialization.wovr_writer COMMAND apta_wovr_writer)
add_test(NAME apta.serialization.wovr_writer_partial COMMAND apta_wovr_writer_partial)
add_test(NAME apta.serialization.wovr_roundtrip COMMAND apta_wovr_roundtrip)
add_test(NAME apta.serialization.wovr_reader_malformed COMMAND apta_wovr_reader_malformed)
add_test(NAME apta.serialization.wovr_reader_hardening COMMAND apta_wovr_reader_hardening)
add_test(NAME apta.serialization.wovr_reader_allocation_failure COMMAND apta_wovr_reader_allocation_failure)
add_test(NAME apta.serialization.wdtl_roundtrip COMMAND apta_wdtl_roundtrip)
add_test(NAME apta.serialization.wdtl_reader_malformed COMMAND apta_wdtl_reader_malformed)
add_test(NAME apta.serialization.wdtl_reader_allocation_failure COMMAND apta_wdtl_reader_allocation_failure)
add_test(NAME apta.serialization.meta_roundtrip COMMAND apta_meta_roundtrip)
add_test(NAME apta.serialization.meta_reader_malformed COMMAND apta_meta_reader_malformed)
add_test(NAME apta.serialization.meta_allocation_failure COMMAND apta_meta_allocation_failure)
add_test(NAME apta.result.concurrency COMMAND apta_result_concurrency)
add_test(NAME apta.core.cancellation_thread COMMAND apta_cancellation_thread)

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
