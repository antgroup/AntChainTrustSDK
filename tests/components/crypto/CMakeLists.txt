# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Crypto Tests
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(
    crypto_smoke_test
    "${CMAKE_CURRENT_LIST_DIR}/crypto_smoke_test.c"
)
target_link_libraries(crypto_smoke_test PRIVATE unity crypto adapter)
actrust_add_test(NAME crypto_smoke_test COMMAND crypto_smoke_test)
set_tests_properties(
    crypto_smoke_test
    PROPERTIES LABELS "actrust;component;smoke"
)

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(crypto_unit_test "${CMAKE_CURRENT_LIST_DIR}/crypto_unit_test.c")
target_link_libraries(crypto_unit_test PRIVATE unity crypto adapter)
actrust_add_test(NAME crypto_unit_test COMMAND crypto_unit_test)
set_tests_properties(
    crypto_unit_test
    PROPERTIES LABELS "actrust;component;unit"
)
