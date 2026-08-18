# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK MQTT Tests
# =============================================================================
# Smoke test requires AWS IoT device credentials under tests/pki/ and AWS
# endpoint config -- see tests/pki/PKI.md.  When prerequisites are absent the
# smoke test is registered as skipped so the rest of the suite still runs.
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------
add_executable(mqtt_smoke_test "${CMAKE_CURRENT_LIST_DIR}/mqtt_smoke_test.c")
target_link_libraries(mqtt_smoke_test PRIVATE unity mqtt)
target_include_directories(
    mqtt_smoke_test
    PRIVATE "${CMAKE_SOURCE_DIR}/tests/common"
)
target_compile_definitions(
    mqtt_smoke_test
    PRIVATE "TEST_MQTT_PKI_DIR=\"${ACTRUST_TESTS_RUNTIME_PKI_DIR}\""
)
actrust_use_shared_pki(mqtt_smoke_test)

if(ACTRUST_TESTS_AWS_MTLS_AVAILABLE)
    actrust_add_test(NAME mqtt_smoke_test COMMAND mqtt_smoke_test)
    set_tests_properties(
        mqtt_smoke_test
        PROPERTIES LABELS "actrust;component;smoke;integration;aws;pki"
    )
else()
    message(
        STATUS
        "mqtt_smoke_test: ${ACTRUST_TESTS_AWS_MTLS_SKIP_REASON}, test will be skipped (see tests/pki/PKI.md)"
    )
    actrust_add_test(
        NAME
        mqtt_smoke_test
        COMMAND
        ${CMAKE_COMMAND}
        -E
        echo
        "Skipped: ${ACTRUST_TESTS_AWS_MTLS_SKIP_REASON}"
    )
    set_tests_properties(
        mqtt_smoke_test
        PROPERTIES
            LABELS "actrust;component;smoke;integration;aws;pki"
            SKIP_REGULAR_EXPRESSION "Skipped"
    )
endif()

# -----------------------------------------------------------------------------
# Unit Test
# -----------------------------------------------------------------------------
add_executable(mqtt_unit_test "${CMAKE_CURRENT_LIST_DIR}/mqtt_unit_test.c")
target_link_libraries(mqtt_unit_test PRIVATE unity mqtt)
actrust_add_test(NAME mqtt_unit_test COMMAND mqtt_unit_test)
set_tests_properties(mqtt_unit_test PROPERTIES LABELS "actrust;component;unit")
