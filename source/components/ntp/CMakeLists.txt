# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK NTP Component
# =============================================================================
# Provides NTP time synchronisation functionality. Uses the coreSNTP serializer
# for SNTP packet encoding/decoding.
# =============================================================================

# Include coreSNTP file paths (provides CORE_SNTP_INCLUDE_PUBLIC_DIRS)
include(${ACTRUST_ROOT}/3rdparts/coreSNTP/coreSntpFilePaths.cmake)

# Create the ntp library
add_library(ntp STATIC "${CMAKE_CURRENT_LIST_DIR}/source/ntp.c")

# Set include directories
target_include_directories(
    ntp
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE ${CORE_SNTP_INCLUDE_PUBLIC_DIRS}
)

# Use default coreSNTP config
target_compile_definitions(ntp PRIVATE SNTP_DO_NOT_USE_CUSTOM_CONFIG)

# Link dependencies
target_link_libraries(ntp PUBLIC actrust_common PRIVATE adapter core_sntp log)
