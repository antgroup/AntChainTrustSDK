# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Examples
# =============================================================================
# Standalone, runnable usage examples. Built only when ACTRUST_BUILD_EXAMPLES is
# enabled so they never interfere with the default library/test build.
# =============================================================================

find_package(Threads REQUIRED)

add_executable(
    actrust_example_hello
    "${CMAKE_CURRENT_LIST_DIR}/hello_actrust.c"
)

target_link_libraries(actrust_example_hello PRIVATE core Threads::Threads)
