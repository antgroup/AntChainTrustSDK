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
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(
        queue_unit_test
        PRIVATE ACTRUST_TEST_WRAP_QUEUE_TEARDOWN=1
    )
    target_link_options(
        queue_unit_test
        PRIVATE
            "-Wl,--wrap=actrust_sem_destroy"
            "-Wl,--wrap=actrust_mutex_destroy"
    )
endif()
actrust_add_test(NAME queue_unit_test COMMAND queue_unit_test)
set_tests_properties(queue_unit_test PROPERTIES LABELS "actrust;component;unit")
