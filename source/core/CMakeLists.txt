# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Core Module
# =============================================================================
# Core API / Service framework with async, callback-driven public API.
# =============================================================================

add_library(
    core
    STATIC
    "${CMAKE_CURRENT_LIST_DIR}/source/core_api.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/core_job.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/core_ops.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/core_service.c"
)

target_include_directories(core PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

target_link_libraries(core PUBLIC cloud crypto ntp PRIVATE json kv log)
