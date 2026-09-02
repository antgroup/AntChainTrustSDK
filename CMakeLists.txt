# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Project
# =============================================================================
# A comprehensive IoT framework providing platform abstraction, component
# libraries, and third-party integrations for embedded systems.
# =============================================================================

cmake_minimum_required(VERSION 3.16)

project(
    AntChainTrustSDK
    VERSION 1.0.0
    LANGUAGES C
    DESCRIPTION "IoT Framework for Embedded Systems"
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# =============================================================================
# Project Configuration
# =============================================================================

# Set C standard for the entire project
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Generate .su files for stack usage analysis
add_compile_options(-fstack-usage)

# Export compile commands for IDE integration and static analysis tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define project root directory for use in subdirectories
set(ACTRUST_ROOT "${CMAKE_SOURCE_DIR}")

# Record the platform identity selected by build.sh so a build directory cannot
# silently reuse a cache created for a different toolchain.
set(ACTRUST_BUILD_PLATFORM "" CACHE STRING "AntChainTrustSDK platform identity")

# Enable testing support
include(CTest)

# =============================================================================
# Load User Configuration
# =============================================================================

set(ACTRUST_CONFIG_DIR "${CMAKE_BINARY_DIR}/config")
set(ACTRUST_CONFIG_HEADER "${ACTRUST_CONFIG_DIR}/actrust_config.h")
set(ACTRUST_CONFIG_CMAKE "${ACTRUST_CONFIG_DIR}/actrust_config.cmake")

foreach(
    config_file
    IN
    ITEMS "${ACTRUST_CONFIG_HEADER}" "${ACTRUST_CONFIG_CMAKE}"
)
    if(NOT EXISTS "${config_file}")
        message(
            FATAL_ERROR
            "Generated configuration file not found: ${config_file}\n"
            "Run ./build.sh <platform> before configuring CMake directly."
        )
    endif()
endforeach()

message(STATUS "Loading configuration from: ${ACTRUST_CONFIG_CMAKE}")
include("${ACTRUST_CONFIG_CMAKE}")

# =============================================================================
# Subdirectories
# =============================================================================

# Add source code subdirectory (adapters, components, core)
add_subdirectory(source)

# Add third-party dependencies
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL
    ON
    CACHE BOOL
    "Do not install third-party package metadata with the SDK"
    FORCE
)
set(INSTALL_MBEDTLS_HEADERS
    OFF
    CACHE BOOL
    "Do not install third-party headers with the SDK"
    FORCE
)
add_subdirectory(3rdparts)

# Optional usage examples (off by default; require the host/Linux build)
option(ACTRUST_BUILD_EXAMPLES "Build the examples under examples/" OFF)
if(ACTRUST_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests live under tests/ and are gated centrally by BUILD_TESTING.
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# =============================================================================
# SDK Installation and Package Export
# =============================================================================

set(ACTRUST_SDK_ARCHIVE "${CMAKE_BINARY_DIR}/lib/libAntChainTrustSDK.a")
if(CONFIG_ACTRUST_ADAPTER_PLATFORM_LINUX)
    set(ACTRUST_ADAPTER_ARCHIVE
        "${CMAKE_BINARY_DIR}/source/adapter/platform/linux/libadapter.a"
    )
elseif(CONFIG_ACTRUST_ADAPTER_PLATFORM_ANDROID)
    set(ACTRUST_ADAPTER_ARCHIVE
        "${CMAKE_BINARY_DIR}/source/adapter/platform/android/libadapter.a"
    )
elseif(CONFIG_ACTRUST_ADAPTER_PLATFORM_SIMCOM_A7606E)
    set(ACTRUST_ADAPTER_ARCHIVE
        "${CMAKE_BINARY_DIR}/source/adapter/platform/simcom/a7606e/libadapter.a"
    )
else()
    message(FATAL_ERROR "No supported adapter platform is configured")
endif()

set(ACTRUST_SDK_ARCHIVES
    "${CMAKE_BINARY_DIR}/source/libactrust_common.a"
    "${ACTRUST_ADAPTER_ARCHIVE}"
    "${CMAKE_BINARY_DIR}/source/components/log/liblog.a"
    "${CMAKE_BINARY_DIR}/source/components/json/libjson.a"
    "${CMAKE_BINARY_DIR}/source/components/kv/libkv.a"
    "${CMAKE_BINARY_DIR}/source/components/ntp/libntp.a"
    "${CMAKE_BINARY_DIR}/source/components/queue/libqueue.a"
    "${CMAKE_BINARY_DIR}/source/components/crypto/libcrypto.a"
    "${CMAKE_BINARY_DIR}/source/components/tls/libtls.a"
    "${CMAKE_BINARY_DIR}/source/components/mqtt/libmqtt.a"
    "${CMAKE_BINARY_DIR}/source/components/cloud/libcloud.a"
    "${CMAKE_BINARY_DIR}/source/core/libcore.a"
)
set(ACTRUST_SDK_TARGETS
    actrust_common
    adapter
    log
    json
    kv
    ntp
    queue
    crypto
    tls
    mqtt
    cloud
    core
)

list(LENGTH ACTRUST_SDK_ARCHIVES ACTRUST_ARCHIVE_COUNT)
set(ACTRUST_ARCHIVE_ARGS)
math(EXPR ACTRUST_ARCHIVE_LAST "${ACTRUST_ARCHIVE_COUNT} - 1")
foreach(index RANGE ${ACTRUST_ARCHIVE_LAST})
    list(GET ACTRUST_SDK_ARCHIVES ${index} archive)
    list(APPEND ACTRUST_ARCHIVE_ARGS "-DACTRUST_ARCHIVE_${index}=${archive}")
endforeach()

add_custom_command(
    OUTPUT "${ACTRUST_SDK_ARCHIVE}"
    COMMAND
        "${CMAKE_COMMAND}" "-DACTRUST_ARCHIVE_OUTPUT=${ACTRUST_SDK_ARCHIVE}"
        "-DACTRUST_ARCHIVE_AR=${CMAKE_AR}"
        "-DACTRUST_ARCHIVE_RANLIB=${CMAKE_RANLIB}"
        "-DACTRUST_ARCHIVE_COUNT=${ACTRUST_ARCHIVE_COUNT}"
        ${ACTRUST_ARCHIVE_ARGS} -P
        "${CMAKE_CURRENT_LIST_DIR}/cmake/merge_static_libraries.cmake"
    DEPENDS ${ACTRUST_SDK_TARGETS}
    VERBATIM
)
add_custom_target(AntChainTrustSDK ALL DEPENDS "${ACTRUST_SDK_ARCHIVE}")

install(
    FILES
        "${CMAKE_CURRENT_LIST_DIR}/include/actrust.h"
        "${CMAKE_CURRENT_LIST_DIR}/include/actrust_errno.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    CODE
        "execute_process(
        COMMAND \"${CMAKE_COMMAND}\" --build \"${CMAKE_BINARY_DIR}\"
            --target AntChainTrustSDK
        RESULT_VARIABLE actrust_install_build_result
    )
    if(NOT actrust_install_build_result EQUAL 0)
        message(FATAL_ERROR \"Failed to build AntChainTrustSDK archive before install\")
    endif()"
)

install(FILES "${ACTRUST_SDK_ARCHIVE}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/AntChainTrustSDKConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AntChainTrustSDKConfig.cmake"
    @ONLY
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/AntChainTrustSDKConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/AntChainTrustSDKConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/AntChainTrustSDKConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/AntChainTrustSDK"
)

# =============================================================================
# SDK Bundle Metadata
# =============================================================================

set(ACTRUST_BUNDLE_METADATA_DIR "${CMAKE_BINARY_DIR}/bundle-metadata")
set(ACTRUST_THIRD_PARTY_MANIFEST
    "${ACTRUST_BUNDLE_METADATA_DIR}/THIRD_PARTY.json"
)
set(ACTRUST_SPDX_SBOM "${ACTRUST_BUNDLE_METADATA_DIR}/sbom.spdx.json")
set(ACTRUST_BUNDLE_METADATA_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/cmake/generate_bundle_metadata.cmake"
)

add_custom_command(
    OUTPUT "${ACTRUST_THIRD_PARTY_MANIFEST}" "${ACTRUST_SPDX_SBOM}"
    COMMAND
        "${CMAKE_COMMAND}" "-DACTRUST_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}"
        "-DACTRUST_OUTPUT_DIR=${ACTRUST_BUNDLE_METADATA_DIR}"
        "-DACTRUST_SDK_VERSION=${PROJECT_VERSION}" -P
        "${ACTRUST_BUNDLE_METADATA_SCRIPT}"
    DEPENDS
        "${ACTRUST_BUNDLE_METADATA_SCRIPT}"
        "${CMAKE_CURRENT_LIST_DIR}/NOTICE"
        "${CMAKE_CURRENT_LIST_DIR}/LICENSE"
    VERBATIM
)
add_custom_target(
    actrust_bundle_metadata
    DEPENDS "${ACTRUST_THIRD_PARTY_MANIFEST}" "${ACTRUST_SPDX_SBOM}"
)

install(
    CODE
        "execute_process(
        COMMAND \"${CMAKE_COMMAND}\" --build \"${CMAKE_BINARY_DIR}\"
            --target actrust_bundle_metadata
        RESULT_VARIABLE actrust_metadata_result
    )
    if(NOT actrust_metadata_result EQUAL 0)
        message(FATAL_ERROR \"Failed to build SDK bundle metadata before install\")
    endif()"
)

install(
    FILES
        "${CMAKE_CURRENT_LIST_DIR}/LICENSE"
        "${CMAKE_CURRENT_LIST_DIR}/NOTICE"
        "${ACTRUST_THIRD_PARTY_MANIFEST}"
        "${ACTRUST_SPDX_SBOM}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/AntChainTrustSDK"
    COMPONENT sdk
)

install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/3rdparts/backoffAlgorithm/LICENSE"
    RENAME "backoffAlgorithm-LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/AntChainTrustSDK/licenses"
    COMPONENT sdk
)
install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/3rdparts/coreJSON/LICENSE"
    RENAME "coreJSON-LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/AntChainTrustSDK/licenses"
    COMPONENT sdk
)
install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/3rdparts/coreMQTT/LICENSE"
    RENAME "coreMQTT-LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/AntChainTrustSDK/licenses"
    COMPONENT sdk
)
install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/3rdparts/coreMQTT-Agent/LICENSE"
    RENAME "coreMQTT-Agent-LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/AntChainTrustSDK/licenses"
    COMPONENT sdk
)
install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/3rdparts/coreSNTP/LICENSE"
    RENAME "coreSNTP-LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/AntChainTrustSDK/licenses"
    COMPONENT sdk
)
install(
    FILES "${CMAKE_CURRENT_LIST_DIR}/3rdparts/mbedtls/LICENSE"
    RENAME "mbedTLS-LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/AntChainTrustSDK/licenses"
    COMPONENT sdk
)

set(ACTRUST_BUNDLE_ARCHIVE
    "${CMAKE_BINARY_DIR}/AntChainTrustSDK-${PROJECT_VERSION}-${ACTRUST_BUILD_PLATFORM}.tar.gz"
)
set(ACTRUST_BUNDLE_CHECKSUM "${ACTRUST_BUNDLE_ARCHIVE}.sha256")

set(ACTRUST_BUNDLE_CHECKSUM_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/cmake/generate_bundle_checksum.cmake"
)

set(CPACK_PACKAGE_NAME "AntChainTrustSDK")
set(CPACK_PACKAGE_VENDOR "Antchain (SHANGHAI) Digital Technology Co., Ltd.")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AntChainTrustSDK development bundle")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "AntChainTrustSDK-${PROJECT_VERSION}-${ACTRUST_BUILD_PLATFORM}"
)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "AntChainTrustSDK")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY TRUE)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_VERBATIM_VARIABLES YES)

include(CPack)

add_custom_target(
    actrust_sdk_bundle
    COMMAND
        "${CMAKE_CPACK_COMMAND}" --config
        "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    COMMAND
        "${CMAKE_COMMAND}" "-DACTRUST_BUNDLE_ARCHIVE=${ACTRUST_BUNDLE_ARCHIVE}"
        "-DACTRUST_BUNDLE_CHECKSUM=${ACTRUST_BUNDLE_CHECKSUM}" -P
        "${ACTRUST_BUNDLE_CHECKSUM_SCRIPT}"
    DEPENDS actrust_bundle_metadata AntChainTrustSDK
    USES_TERMINAL
)

if(BUILD_TESTING)
    add_test(
        NAME package_bundle_test
        COMMAND
            "${CMAKE_COMMAND}" "-DACTRUST_PACKAGE_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "-P"
            "${CMAKE_CURRENT_LIST_DIR}/tests/package/package_bundle_test.cmake"
    )
    set_tests_properties(
        package_bundle_test
        PROPERTIES LABELS "actrust;package;unit"
    )
endif()
