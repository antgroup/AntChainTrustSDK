# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK KV Tests
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(kv_smoke_test "${CMAKE_CURRENT_LIST_DIR}/kv_smoke_test.c")
target_include_directories(
    kv_smoke_test
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common"
)
target_link_libraries(kv_smoke_test PRIVATE unity kv adapter)
actrust_add_test(NAME kv_smoke_test COMMAND kv_smoke_test)
set_tests_properties(kv_smoke_test PROPERTIES LABELS "actrust;component;smoke")

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(kv_unit_test "${CMAKE_CURRENT_LIST_DIR}/kv_unit_test.c")
target_include_directories(
    kv_unit_test
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common"
)
target_link_libraries(kv_unit_test PRIVATE unity kv adapter)
actrust_add_test(NAME kv_unit_test COMMAND kv_unit_test)
set_tests_properties(kv_unit_test PROPERTIES LABELS "actrust;component;unit")
