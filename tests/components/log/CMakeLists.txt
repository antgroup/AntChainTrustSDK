# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Log Tests
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(log_smoke_test "${CMAKE_CURRENT_LIST_DIR}/log_smoke_test.c")
target_link_libraries(log_smoke_test PRIVATE unity log adapter)
actrust_add_test(NAME log_smoke_test COMMAND log_smoke_test)
set_tests_properties(log_smoke_test PROPERTIES LABELS "actrust;component;smoke")

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(log_unit_test "${CMAKE_CURRENT_LIST_DIR}/log_unit_test.c")
target_link_libraries(log_unit_test PRIVATE unity log adapter)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(
        log_unit_test
        PRIVATE ACTRUST_TEST_WRAP_LOG_OUT=1
    )
    target_link_options(log_unit_test PRIVATE "-Wl,--wrap=actrust_log_out")
endif()
actrust_add_test(NAME log_unit_test COMMAND log_unit_test)
set_tests_properties(log_unit_test PROPERTIES LABELS "actrust;component;unit")
