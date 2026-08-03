# SPDX-License-Identifier: Apache-2.0
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(APTA_PACKAGE_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/APTA")
set(APTA_BUILD_TREE_PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/package-build-tree/APTA")
set(APTA_BUILD_TREE_PACKAGE_DIR "${APTA_BUILD_TREE_PACKAGE_ROOT}/$<CONFIG>")

if(BUILD_SHARED_LIBS)
    set(APTA_IMPORTED_LIBRARY_TYPE SHARED)
    set(APTA_PACKAGE_IS_SHARED TRUE)
    set(APTA_PACKAGE_SHARED_DEFINITION
        "    set_property(TARGET apta::core APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS APTA_SHARED=1)")
else()
    set(APTA_IMPORTED_LIBRARY_TYPE STATIC)
    set(APTA_PACKAGE_IS_SHARED FALSE)
    set(APTA_PACKAGE_SHARED_DEFINITION "")
endif()

set(APTA_PACKAGE_LINK_SUFFIX "")
set(APTA_PC_PRIVATE_LIBS "")
if(NOT BUILD_SHARED_LIBS AND UNIX)
    set(APTA_PACKAGE_LINK_SUFFIX ";m")
    set(APTA_PC_PRIVATE_LIBS "-lm")
endif()

set(APTA_PACKAGE_IMPLIB_PROPERTY "")
set(APTA_BUILD_TREE_IMPLIB_PROPERTY "")
if(BUILD_SHARED_LIBS AND WIN32)
    set(APTA_PACKAGE_RUNTIME_RELATIVE_DIR "${CMAKE_INSTALL_BINDIR}")
    set(
        APTA_PACKAGE_LIBRARY_RELATIVE_PATH
        "${CMAKE_INSTALL_BINDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}apta${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(
        APTA_PACKAGE_IMPLIB_RELATIVE_PATH
        "${CMAKE_INSTALL_LIBDIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}apta${CMAKE_IMPORT_LIBRARY_SUFFIX}")
    set(
        APTA_PACKAGE_IMPLIB_PROPERTY
        "    set_property(TARGET apta::core PROPERTY IMPORTED_IMPLIB \"\${PACKAGE_PREFIX_DIR}/${APTA_PACKAGE_IMPLIB_RELATIVE_PATH}\")")
    set(
        APTA_BUILD_TREE_IMPLIB_PROPERTY
        "    set_property(TARGET apta::core PROPERTY IMPORTED_IMPLIB \"$<TARGET_LINKER_FILE:apta_core>\")")
elseif(BUILD_SHARED_LIBS)
    set(APTA_PACKAGE_RUNTIME_RELATIVE_DIR "${CMAKE_INSTALL_LIBDIR}")
    set(
        APTA_PACKAGE_LIBRARY_RELATIVE_PATH
        "${CMAKE_INSTALL_LIBDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}apta${CMAKE_SHARED_LIBRARY_SUFFIX}")
else()
    set(APTA_PACKAGE_RUNTIME_RELATIVE_DIR "${CMAKE_INSTALL_LIBDIR}")
    set(
        APTA_PACKAGE_LIBRARY_RELATIVE_PATH
        "${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}apta${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/APTAConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/APTAConfig.install.cmake"
    INSTALL_DESTINATION "${APTA_PACKAGE_CMAKE_INSTALL_DIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/APTAConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/APTABuildTreeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/APTAConfig.build.in.cmake"
    @ONLY)
file(
    GENERATE
    OUTPUT "${APTA_BUILD_TREE_PACKAGE_DIR}/APTAConfig.cmake"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/APTAConfig.build.in.cmake")
file(
    GENERATE
    OUTPUT "${APTA_BUILD_TREE_PACKAGE_DIR}/APTAConfigVersion.cmake"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/APTAConfigVersion.cmake")

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/libapta.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libapta.pc"
    @ONLY)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/apta"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h")

if(BUILD_SHARED_LIBS)
    if(WIN32)
        install(FILES "$<TARGET_FILE:apta_core>" DESTINATION "${CMAKE_INSTALL_BINDIR}")
        install(FILES "$<TARGET_LINKER_FILE:apta_core>" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    else()
        install(
            FILES
                "$<TARGET_FILE:apta_core>"
                "$<TARGET_SONAME_FILE:apta_core>"
                "$<TARGET_LINKER_FILE:apta_core>"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
else()
    install(FILES "$<TARGET_FILE:apta_core>" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/APTAConfig.install.cmake"
    DESTINATION "${APTA_PACKAGE_CMAKE_INSTALL_DIR}"
    RENAME APTAConfig.cmake)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/APTAConfigVersion.cmake"
    DESTINATION "${APTA_PACKAGE_CMAKE_INSTALL_DIR}")
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/libapta.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/libapta")
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/libapta")
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/libapta")

set(CPACK_PACKAGE_NAME "libapta")
set(CPACK_PACKAGE_VENDOR "APTA")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Adaptive Progressive Track Analysis core library")
set(CPACK_PACKAGE_VERSION "${APTA_VERSION_FULL}")
set(
    CPACK_PACKAGE_FILE_NAME
    "libapta-${APTA_VERSION_FULL}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "libapta-${APTA_VERSION_FULL}-source")
set(
    CPACK_SOURCE_IGNORE_FILES
    "/\.git/"
    "/build[^/]*/"
    "/package-test-/"
    "~$"
    "\.swp$")
include(CPack)

set(APTA_BUILD_TREE_RUNTIME_DIR "$<TARGET_FILE_DIR:apta_core>")
set(APTA_BUILD_TREE_RUNTIME_FILE "$<TARGET_FILE:apta_core>")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/package/run_consumer.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/run_package_consumer.in.cmake"
    @ONLY)
file(
    GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/run_package_consumer-$<CONFIG>.cmake"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/run_package_consumer.in.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/package/check_archives.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/check_package_archives.cmake"
    @ONLY)

if(NOT APTA_ENABLE_SANITIZERS AND NOT APTA_BUILD_FUZZING)
    add_test(
        NAME apta.package.build_tree
        COMMAND
            "${CMAKE_COMMAND}"
            -DAPTA_PACKAGE_MODE=build
            -DAPTA_TEST_CONFIG=$<CONFIG>
            -P "${CMAKE_CURRENT_BINARY_DIR}/run_package_consumer-$<CONFIG>.cmake")
    add_test(
        NAME apta.abi.package_install
        COMMAND
            "${CMAKE_COMMAND}"
            -DAPTA_PACKAGE_MODE=install
            -DAPTA_TEST_CONFIG=$<CONFIG>
            -P "${CMAKE_CURRENT_BINARY_DIR}/run_package_consumer-$<CONFIG>.cmake")
    add_test(
        NAME apta.package.archives
        COMMAND
            "${CMAKE_COMMAND}"
            -DAPTA_TEST_CONFIG=$<CONFIG>
            -P "${CMAKE_CURRENT_BINARY_DIR}/check_package_archives.cmake")
endif()
