cmake_minimum_required(VERSION 3.16)
project(apta_package_consumer LANGUAGES C)

find_package(APTA 1.0 CONFIG REQUIRED)

add_executable(apta_package_consumer main.c)
target_link_libraries(apta_package_consumer PRIVATE apta::core)
set_target_properties(
    apta_package_consumer
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(
        apta_package_consumer
        PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/bin")
endforeach()
