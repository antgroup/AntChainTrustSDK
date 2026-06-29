# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Queue Tests
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(queue_smoke_test "${CMAKE_CURRENT_LIST_DIR}/queue_smoke_test.c")
target_link_libraries(queue_smoke_test PRIVATE unity queue adapter)
actrust_add_test(NAME queue_smoke_test COMMAND queue_smoke_test)
set_tests_properties(
    queue_smoke_test
    PROPERTIES LABELS "actrust;component;smoke"
)

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(queue_unit_test "${CMAKE_CURRENT_LIST_DIR}/queue_unit_test.c")
target_link_libraries(queue_unit_test PRIVATE unity queue adapter)
actrust_add_test(NAME queue_unit_test COMMAND queue_unit_test)
set_tests_properties(queue_unit_test PROPERTIES LABELS "actrust;component;unit")
