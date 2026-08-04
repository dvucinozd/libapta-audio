# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(apta_installed_consumer LANGUAGES C)

find_package(APTA 1.0 CONFIG REQUIRED)

add_executable(apta_installed_consumer main.c)
target_link_libraries(apta_installed_consumer PRIVATE apta::core)
target_compile_features(apta_installed_consumer PRIVATE c_std_11)
