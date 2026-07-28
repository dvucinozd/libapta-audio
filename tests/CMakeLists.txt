# SPDX-License-Identifier: Apache-2.0

add_executable(apta_headers_c compile/headers_c.c)
target_link_libraries(apta_headers_c PRIVATE apta::headers)
set_target_properties(apta_headers_c PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)

add_executable(apta_headers_cpp compile/headers_cpp.cpp)
target_link_libraries(apta_headers_cpp PRIVATE apta::headers)
set_target_properties(apta_headers_cpp PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)

add_test(NAME apta.headers.c COMMAND apta_headers_c)
add_test(NAME apta.headers.cpp COMMAND apta_headers_cpp)
