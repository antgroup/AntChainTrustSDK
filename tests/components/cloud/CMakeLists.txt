# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Cloud Tests
# =============================================================================
# Smoke test requires AWS IoT device credentials under tests/pki/ and AWS
# fleet-provisioning config -- see tests/pki/PKI.md.  When prerequisites are
# absent the smoke test is registered as skipped so the rest of the suite still
# runs.
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(cloud_smoke_test "${CMAKE_CURRENT_LIST_DIR}/cloud_smoke_test.c")
target_link_libraries(
    cloud_smoke_test
    PRIVATE unity cloud crypto kv log adapter
)
target_include_directories(
    cloud_smoke_test
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common"
)
target_compile_definitions(
    cloud_smoke_test
    PRIVATE "TEST_CLOUD_PKI_DIR=\"${ACTRUST_TESTS_RUNTIME_PKI_DIR}\""
)
actrust_use_shared_pki(cloud_smoke_test)

if(ACTRUST_TESTS_AWS_FLEET_AVAILABLE)
    actrust_add_test(NAME cloud_smoke_test COMMAND cloud_smoke_test)
    set_tests_properties(
        cloud_smoke_test
        PROPERTIES LABELS "actrust;component;smoke;integration;aws;pki"
    )
else()
    message(
        STATUS
        "cloud_smoke_test: ${ACTRUST_TESTS_AWS_FLEET_SKIP_REASON}, test will be skipped (see tests/pki/PKI.md)"
    )
    actrust_add_test(
        NAME
        cloud_smoke_test
        COMMAND
        ${CMAKE_COMMAND}
        -E
        echo
        "Skipped: ${ACTRUST_TESTS_AWS_FLEET_SKIP_REASON}"
    )
    set_tests_properties(
        cloud_smoke_test
        PROPERTIES
            LABELS "actrust;component;smoke;integration;aws;pki"
            SKIP_REGULAR_EXPRESSION "Skipped"
    )
endif()

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(cloud_unit_test "${CMAKE_CURRENT_LIST_DIR}/cloud_unit_test.c")
target_link_libraries(
    cloud_unit_test
    PRIVATE unity cloud crypto mqtt adapter
)
set(CLOUD_AWS_INCLUDE_DIR
    "${CMAKE_SOURCE_DIR}/source/components/cloud/source/provider/aws/include"
)
target_include_directories(cloud_unit_test PRIVATE "${CLOUD_AWS_INCLUDE_DIR}")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(
        cloud_unit_test
        PRIVATE ACTRUST_TEST_WRAP_CLOUD_TEARDOWN=1
    )
    target_link_options(
        cloud_unit_test
        PRIVATE
            "-Wl,--wrap=actrust_mqtt_deinit"
            "-Wl,--wrap=actrust_queue_destroy"
            "-Wl,--wrap=actrust_task_join"
    )
endif()
actrust_add_test(NAME cloud_unit_test COMMAND cloud_unit_test)
set_tests_properties(cloud_unit_test PROPERTIES LABELS "actrust;component;unit")
