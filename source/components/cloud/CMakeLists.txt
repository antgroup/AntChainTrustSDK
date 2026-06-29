# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Cloud Component
# =============================================================================
# Builds the cloud dispatch layer. Provider implementations are added by
# source/provider/CMakeLists.txt.
# =============================================================================

add_library(cloud STATIC "${CMAKE_CURRENT_LIST_DIR}/source/cloud.c")

target_include_directories(cloud PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

add_subdirectory(source/provider)

target_link_libraries(cloud PUBLIC queue crypto PRIVATE json mqtt log kv)
