# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Logging Component
# =============================================================================
# Provides logging functionality for the AntChainTrustSDK framework.
# =============================================================================

# Create the log library
add_library(log STATIC "${CMAKE_CURRENT_LIST_DIR}/source/log.c")

# Set include directories
target_include_directories(log PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

# Link dependencies
target_link_libraries(log PUBLIC actrust_common PRIVATE adapter)
