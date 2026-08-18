# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK NTP Tests
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(ntp_smoke_test "${CMAKE_CURRENT_LIST_DIR}/ntp_smoke_test.c")
target_link_libraries(ntp_smoke_test PRIVATE unity ntp adapter)
actrust_add_test(NAME ntp_smoke_test COMMAND ntp_smoke_test)
set_tests_properties(
    ntp_smoke_test
    PROPERTIES LABELS "actrust;component;smoke;network"
)

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(ntp_unit_test "${CMAKE_CURRENT_LIST_DIR}/ntp_unit_test.c")
target_link_libraries(ntp_unit_test PRIVATE unity ntp adapter)
actrust_add_test(NAME ntp_unit_test COMMAND ntp_unit_test)
set_tests_properties(ntp_unit_test PROPERTIES LABELS "actrust;component;unit")
