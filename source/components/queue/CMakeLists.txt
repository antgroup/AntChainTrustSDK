# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Queue Component
# =============================================================================
# Provides a generic thread-safe bounded queue.
# =============================================================================

add_library(queue STATIC "${CMAKE_CURRENT_LIST_DIR}/source/queue.c")

target_include_directories(queue PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

target_link_libraries(queue PUBLIC actrust_common PRIVATE adapter)
