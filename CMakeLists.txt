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
