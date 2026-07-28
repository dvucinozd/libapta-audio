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

add_executable(apta_waveform_overview unit/waveform_overview.c)
target_link_libraries(apta_waveform_overview PRIVATE apta::core)
set_target_properties(apta_waveform_overview PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_determinism unit/waveform_determinism.c)
target_link_libraries(apta_waveform_determinism PRIVATE apta::core)
set_target_properties(apta_waveform_determinism PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_priority unit/waveform_priority.c)
target_link_libraries(apta_waveform_priority PRIVATE apta::core)
set_target_properties(apta_waveform_priority PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_waveform_contracts unit/waveform_contracts.c)
target_link_libraries(apta_waveform_contracts PRIVATE apta::core)
set_target_properties(apta_waveform_contracts PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_memory_requirements unit/memory_requirements.c)
target_link_libraries(apta_memory_requirements PRIVATE apta::core)
set_target_properties(apta_memory_requirements PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_result_concurrency unit/result_concurrency.cpp)
target_link_libraries(apta_result_concurrency PRIVATE apta::core Threads::Threads)
set_target_properties(apta_result_concurrency PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)

add_test(NAME apta.core.smoke COMMAND apta_core_smoke)
add_test(NAME apta.core.allocation_failure COMMAND apta_allocation_failure)
add_test(NAME apta.core.cancellation COMMAND apta_cancellation)
add_test(NAME apta.api.initializers COMMAND apta_initializers)
add_test(NAME apta.api.version_validation COMMAND apta_version_validation)
add_test(NAME apta.waveform.overview COMMAND apta_waveform_overview)
add_test(NAME apta.waveform.determinism COMMAND apta_waveform_determinism)
add_test(NAME apta.waveform.priority COMMAND apta_waveform_priority)
add_test(NAME apta.waveform.contracts COMMAND apta_waveform_contracts)
add_test(NAME apta.memory.requirements COMMAND apta_memory_requirements)
add_test(NAME apta.result.concurrency COMMAND apta_result_concurrency)
