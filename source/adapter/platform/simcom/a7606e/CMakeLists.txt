# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK SIMCom A7606E Platform Adapter
# =============================================================================
# Platform-specific implementation of the AntChainTrustSDK adapter for SIMCom A7606E-H
# Cat-1 module (ASR1806 SoC, OpenWrt Linux, musl libc).
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# Find required system dependencies
find_package(Threads REQUIRED)

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

# Locate the SIMCom SDK static library and its dependencies.
find_library(SIMCOM_SDK_LIB NAMES sdk)
find_library(SIMCOM_LOG_LIB NAMES log)
find_library(SIMCOM_UCI_LIB NAMES uci)
find_library(SUNSEA_SDK_LIB NAMES sunseasdk)
find_library(TEEC_LIB NAMES teec)
find_library(OPENSSL_CRYPTO_LIB NAMES crypto)

# Link required libraries.
target_link_libraries(
    adapter
    PUBLIC
        actrust_common
        ${SIMCOM_SDK_LIB}
        ${SIMCOM_LOG_LIB}
        ${SIMCOM_UCI_LIB}
        ${SUNSEA_SDK_LIB}
        ${TEEC_LIB}
        ${OPENSSL_CRYPTO_LIB}
    PRIVATE Threads::Threads
)
