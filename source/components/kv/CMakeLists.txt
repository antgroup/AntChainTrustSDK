# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK KV Component
# =============================================================================
# Provides Key-Value storage functionality for the AntChainTrustSDK framework.
# =============================================================================

# Create the kv library
add_library(kv STATIC "${CMAKE_CURRENT_LIST_DIR}/source/kv.c")

# Set include directories
target_include_directories(kv PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

# Link dependencies
target_link_libraries(kv PUBLIC actrust_common PRIVATE adapter log)
