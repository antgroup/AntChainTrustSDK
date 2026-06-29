# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Adapter Platform Configuration
# =============================================================================
# This module handles platform-specific adapter configuration for AntChainTrustSDK.
# Kconfig-selectable platforms: linux, android, simcom_a7606e. If no adapter
# platform is set, CMake disables platform support.
# =============================================================================

# Determine the adapter platform based on configuration
if(
    DEFINED CONFIG_ACTRUST_ADAPTER_PLATFORM_LINUX
    AND CONFIG_ACTRUST_ADAPTER_PLATFORM_LINUX
)
    set(ADAPTER_PLATFORM "linux")
elseif(
    DEFINED CONFIG_ACTRUST_ADAPTER_PLATFORM_ANDROID
    AND CONFIG_ACTRUST_ADAPTER_PLATFORM_ANDROID
)
    set(ADAPTER_PLATFORM "android")
elseif(
    DEFINED CONFIG_ACTRUST_ADAPTER_PLATFORM_SIMCOM_A7606E
    AND CONFIG_ACTRUST_ADAPTER_PLATFORM_SIMCOM_A7606E
)
    set(ADAPTER_PLATFORM "simcom_a7606e")
else()
    # Default to none if no platform is explicitly configured
    set(ADAPTER_PLATFORM "none")
endif()

# Include platform-specific subdirectory
if(ADAPTER_PLATFORM STREQUAL "linux")
    message(STATUS "AntChainTrustSDK Adapter: Building for Linux platform")
    add_subdirectory(platform/linux)
elseif(ADAPTER_PLATFORM STREQUAL "android")
    message(STATUS "AntChainTrustSDK Adapter: Building for Android platform")
    add_subdirectory(platform/android)
elseif(ADAPTER_PLATFORM STREQUAL "simcom_a7606e")
    message(
        STATUS
        "AntChainTrustSDK Adapter: Building for SIMCom A7606E-H platform"
    )
    add_subdirectory(platform/simcom/a7606e)
elseif(ADAPTER_PLATFORM STREQUAL "none")
    message(STATUS "AntChainTrustSDK Adapter: Platform support disabled")
else()
    message(
        FATAL_ERROR
        "AntChainTrustSDK Adapter: Unsupported platform '${ADAPTER_PLATFORM}'"
    )
endif()
