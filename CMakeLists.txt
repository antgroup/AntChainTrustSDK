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

# Enable testing support
include(CTest)

# =============================================================================
# Load User Configuration
# =============================================================================

set(ACTRUST_CONFIG_CMAKE "${CMAKE_BINARY_DIR}/config/actrust_config.cmake")
if(EXISTS "${ACTRUST_CONFIG_CMAKE}")
    message(STATUS "Loading configuration from: ${ACTRUST_CONFIG_CMAKE}")
    include("${ACTRUST_CONFIG_CMAKE}")
else()
    message(WARNING "Configuration file not found: ${ACTRUST_CONFIG_CMAKE}")
    message(
        WARNING
        "Project may not build correctly without proper configuration"
    )
endif()

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
