# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Core Tests
# =============================================================================
# Smoke test requires AWS IoT device credentials under tests/pki/ and AWS
# fleet-provisioning config -- see tests/pki/PKI.md.  When prerequisites are
# absent the smoke test is registered as skipped so the rest of the suite still
# runs.
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(core_smoke_test "${CMAKE_CURRENT_LIST_DIR}/core_smoke_test.c")
target_link_libraries(core_smoke_test PRIVATE unity core)
target_include_directories(
    core_smoke_test
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common"
)
target_compile_definitions(
    core_smoke_test
    PRIVATE "TEST_CORE_PKI_DIR=\"${ACTRUST_TESTS_RUNTIME_PKI_DIR}\""
)
actrust_use_shared_pki(core_smoke_test)

if(ACTRUST_TESTS_AWS_FLEET_AVAILABLE)
    actrust_add_test(NAME core_smoke_test COMMAND core_smoke_test)
    set_tests_properties(
        core_smoke_test
        PROPERTIES LABELS "actrust;core;smoke;integration;aws;pki"
    )
else()
    message(
        STATUS
        "core_smoke_test: ${ACTRUST_TESTS_AWS_FLEET_SKIP_REASON}, test will be skipped (see tests/pki/PKI.md)"
    )
    actrust_add_test(
        NAME
        core_smoke_test
        COMMAND
        ${CMAKE_COMMAND}
        -E
        echo
        "Skipped: ${ACTRUST_TESTS_AWS_FLEET_SKIP_REASON}"
    )
    set_tests_properties(
        core_smoke_test
        PROPERTIES
            LABELS "actrust;core;smoke;integration;aws;pki"
            SKIP_REGULAR_EXPRESSION "Skipped"
    )
endif()

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(core_unit_test "${CMAKE_CURRENT_LIST_DIR}/core_unit_test.c")
target_link_libraries(core_unit_test PRIVATE unity core)
actrust_add_test(NAME core_unit_test COMMAND core_unit_test)
set_tests_properties(core_unit_test PROPERTIES LABELS "actrust;core;unit")
