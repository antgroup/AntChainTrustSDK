# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Android Platform Adapter
# =============================================================================
# Platform-specific implementation of the AntChainTrustSDK adapter for Android
# devices and Android NDK builds.
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# Find required system dependencies
find_package(Threads REQUIRED)
find_library(ANDROID_LOG_LIB log)
if(NOT ANDROID_LOG_LIB)
    message(FATAL_ERROR "Android log library not found")
endif()

# Adapter sources — listed explicitly so CMake re-runs are deterministic and
# new files are deliberately added (no stale GLOB cache).
add_library(
    adapter
    STATIC
    "${CMAKE_CURRENT_LIST_DIR}/device.c"
    "${CMAKE_CURRENT_LIST_DIR}/network.c"
    "${CMAKE_CURRENT_LIST_DIR}/security.c"
    "${CMAKE_CURRENT_LIST_DIR}/storage.c"
    "${CMAKE_CURRENT_LIST_DIR}/system.c"
)

# Set include directories
target_include_directories(
    adapter
    PUBLIC "${ACTRUST_ROOT}/source/adapter/include"
)

# Link required libraries
target_link_libraries(
    adapter
    PUBLIC actrust_common
    PRIVATE Threads::Threads ${ANDROID_LOG_LIB}
)
