# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK JSON Tests
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(json_smoke_test "${CMAKE_CURRENT_LIST_DIR}/json_smoke_test.c")
target_link_libraries(json_smoke_test PRIVATE unity json)
actrust_add_test(NAME json_smoke_test COMMAND json_smoke_test)
set_tests_properties(
    json_smoke_test
    PROPERTIES LABELS "actrust;component;smoke"
)

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(json_unit_test "${CMAKE_CURRENT_LIST_DIR}/json_unit_test.c")
target_link_libraries(json_unit_test PRIVATE unity json)
actrust_add_test(NAME json_unit_test COMMAND json_unit_test)
set_tests_properties(json_unit_test PROPERTIES LABELS "actrust;component;unit")
