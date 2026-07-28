# SPDX-License-Identifier: Apache-2.0

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

add_test(NAME apta.core.smoke COMMAND apta_core_smoke)
add_test(NAME apta.core.allocation_failure COMMAND apta_allocation_failure)
add_test(NAME apta.core.cancellation COMMAND apta_cancellation)
add_test(NAME apta.api.initializers COMMAND apta_initializers)
add_test(NAME apta.api.version_validation COMMAND apta_version_validation)
add_test(NAME apta.waveform.overview COMMAND apta_waveform_overview)
