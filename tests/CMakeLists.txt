# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Tests
# =============================================================================
# Aggregates all test targets (smoke + unit). Gated by BUILD_TESTING at the
# root.  Mirrors the source/ layout so a test for `<foo>` lives next to where
# one would expect to find it.
#
# Shared PKI material (e.g. Amazon Root CA 1) and local private test material
# live under tests/pki/ and are copied once into build/tests/pki/.
# Private material (client.*) is not committed -- see tests/pki/PKI.md.
# =============================================================================

# -----------------------------------------------------------------------------
# Shared PKI helpers
# -----------------------------------------------------------------------------
set(ACTRUST_TESTS_SHARED_PKI_DIR
    "${CMAKE_CURRENT_LIST_DIR}/pki"
    CACHE INTERNAL
    "Path to shared test PKI material"
)
set(ACTRUST_TESTS_RUNTIME_PKI_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/pki"
    CACHE INTERNAL
    "Path to runtime test PKI material"
)
set(ACTRUST_TESTS_RUNTIME_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/runtime"
    CACHE INTERNAL
    "Path to isolated runtime state for tests"
)

# Test result labels:
#   unit        deterministic host-only checks
#   network     tests that access local or external network endpoints
#   integration tests that require external services or credentials

set(ACTRUST_TESTS_PKI_FILES AmazonRootCA.pem client.crt client.key)

foreach(ACTRUST_TESTS_PKI_FILE IN LISTS ACTRUST_TESTS_PKI_FILES)
    set(ACTRUST_TESTS_PKI_SOURCE
        "${ACTRUST_TESTS_SHARED_PKI_DIR}/${ACTRUST_TESTS_PKI_FILE}"
    )
    if(EXISTS "${ACTRUST_TESTS_PKI_SOURCE}")
        set(ACTRUST_TESTS_PKI_OUTPUT
            "${ACTRUST_TESTS_RUNTIME_PKI_DIR}/${ACTRUST_TESTS_PKI_FILE}"
        )
        add_custom_command(
            OUTPUT "${ACTRUST_TESTS_PKI_OUTPUT}"
            COMMAND
                ${CMAKE_COMMAND} -E make_directory
                "${ACTRUST_TESTS_RUNTIME_PKI_DIR}"
            COMMAND
                ${CMAKE_COMMAND} -E copy_if_different
                "${ACTRUST_TESTS_PKI_SOURCE}" "${ACTRUST_TESTS_PKI_OUTPUT}"
            DEPENDS "${ACTRUST_TESTS_PKI_SOURCE}"
            VERBATIM
        )
        list(APPEND ACTRUST_TESTS_PKI_OUTPUTS "${ACTRUST_TESTS_PKI_OUTPUT}")
    endif()
endforeach()

add_custom_target(actrust_test_pki DEPENDS ${ACTRUST_TESTS_PKI_OUTPUTS})

function(actrust_use_shared_pki TARGET)
    add_dependencies(${TARGET} actrust_test_pki)
endfunction()

set(ACTRUST_TESTS_HAS_AWS_PKI OFF)
if(
    EXISTS "${ACTRUST_TESTS_SHARED_PKI_DIR}/client.key"
    AND EXISTS "${ACTRUST_TESTS_SHARED_PKI_DIR}/client.crt"
)
    set(ACTRUST_TESTS_HAS_AWS_PKI ON)
endif()

set(ACTRUST_TESTS_AWS_ENDPOINT_CONFIGURED OFF)
if(
    DEFINED CONFIG_ACTRUST_CLOUD_AWS_ENDPOINT
    AND NOT CONFIG_ACTRUST_CLOUD_AWS_ENDPOINT STREQUAL ""
)
    set(ACTRUST_TESTS_AWS_ENDPOINT_CONFIGURED ON)
endif()

set(ACTRUST_TESTS_AWS_TEMPLATE_CONFIGURED OFF)
if(
    DEFINED CONFIG_ACTRUST_CLOUD_AWS_TEMPLATE_NAME
    AND NOT CONFIG_ACTRUST_CLOUD_AWS_TEMPLATE_NAME STREQUAL ""
)
    set(ACTRUST_TESTS_AWS_TEMPLATE_CONFIGURED ON)
endif()

set(ACTRUST_TESTS_AWS_PRODUCT_KEY_CONFIGURED OFF)
if(
    DEFINED CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY
    AND NOT CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY STREQUAL ""
)
    set(ACTRUST_TESTS_AWS_PRODUCT_KEY_CONFIGURED ON)
endif()

set(ACTRUST_TESTS_AWS_MTLS_AVAILABLE OFF)
set(ACTRUST_TESTS_AWS_MTLS_SKIP_REASON "")
if(NOT ACTRUST_TESTS_HAS_AWS_PKI)
    set(ACTRUST_TESTS_AWS_MTLS_SKIP_REASON "missing tests/pki/client.{crt,key}")
elseif(NOT ACTRUST_TESTS_AWS_ENDPOINT_CONFIGURED)
    set(ACTRUST_TESTS_AWS_MTLS_SKIP_REASON
        "missing CONFIG_ACTRUST_CLOUD_AWS_ENDPOINT"
    )
else()
    set(ACTRUST_TESTS_AWS_MTLS_AVAILABLE ON)
endif()

set(ACTRUST_TESTS_AWS_FLEET_AVAILABLE OFF)
set(ACTRUST_TESTS_AWS_FLEET_SKIP_REASON "")
if(NOT ACTRUST_TESTS_AWS_MTLS_AVAILABLE)
    set(ACTRUST_TESTS_AWS_FLEET_SKIP_REASON
        "${ACTRUST_TESTS_AWS_MTLS_SKIP_REASON}"
    )
elseif(NOT ACTRUST_TESTS_AWS_TEMPLATE_CONFIGURED)
    set(ACTRUST_TESTS_AWS_FLEET_SKIP_REASON
        "missing CONFIG_ACTRUST_CLOUD_AWS_TEMPLATE_NAME"
    )
elseif(NOT ACTRUST_TESTS_AWS_PRODUCT_KEY_CONFIGURED)
    set(ACTRUST_TESTS_AWS_FLEET_SKIP_REASON
        "missing CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY"
    )
else()
    set(ACTRUST_TESTS_AWS_FLEET_AVAILABLE ON)
endif()

# -----------------------------------------------------------------------------
# Runtime state isolation
# -----------------------------------------------------------------------------
function(actrust_configure_test_runtime TEST_NAME)
    set(ACTRUST_TEST_RUNTIME_DIR "${ACTRUST_TESTS_RUNTIME_DIR}/${TEST_NAME}")
    file(MAKE_DIRECTORY "${ACTRUST_TEST_RUNTIME_DIR}")
    set_tests_properties(
        ${TEST_NAME}
        PROPERTIES WORKING_DIRECTORY "${ACTRUST_TEST_RUNTIME_DIR}"
    )
endfunction()

function(actrust_add_test)
    cmake_parse_arguments(ACTRUST_ADD_TEST "" "NAME" "" ${ARGV})
    if(NOT ACTRUST_ADD_TEST_NAME)
        message(
            FATAL_ERROR
            "actrust_add_test requires add_test(NAME ...) syntax"
        )
    endif()

    add_test(${ARGV})
    actrust_configure_test_runtime(${ACTRUST_ADD_TEST_NAME})
endfunction()

# -----------------------------------------------------------------------------
# Configuration schema validation
# -----------------------------------------------------------------------------
add_test(
    NAME config_validate_test
    COMMAND bash "${CMAKE_CURRENT_LIST_DIR}/config_validate_test.sh"
)
set_tests_properties(
    config_validate_test
    PROPERTIES LABELS "actrust;config;unit"
)

# -----------------------------------------------------------------------------
# Shared test helper unit test
# -----------------------------------------------------------------------------
add_executable(
    actrust_test_unit_test
    "${CMAKE_CURRENT_LIST_DIR}/common/actrust_test_unit_test.c"
)
target_include_directories(
    actrust_test_unit_test
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/common"
)
target_link_libraries(actrust_test_unit_test PRIVATE unity)
actrust_add_test(NAME actrust_test_unit_test COMMAND actrust_test_unit_test)
set_tests_properties(
    actrust_test_unit_test
    PROPERTIES LABELS "actrust;common;unit"
)

# -----------------------------------------------------------------------------
# Installed package consumer
# -----------------------------------------------------------------------------
add_test(
    NAME package_consumer_test
    COMMAND
        "${CMAKE_COMMAND}" "-DACTRUST_PACKAGE_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" -P
        "${CMAKE_CURRENT_LIST_DIR}/package/package_consumer_test.cmake"
)
set_tests_properties(
    package_consumer_test
    PROPERTIES LABELS "actrust;package;smoke"
)

add_subdirectory(adapter)
add_subdirectory(components)
add_subdirectory(core)
