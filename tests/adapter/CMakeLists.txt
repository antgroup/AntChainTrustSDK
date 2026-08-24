# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Adapter Tests
# =============================================================================
# One executable per adapter interface (system, network, storage, security,
# device); all link the platform adapter library directly.
# =============================================================================

# -----------------------------------------------------------------------------
# Smoke Tests
# -----------------------------------------------------------------------------

# ---- System ----
add_executable(
    system_adapter_smoke_test
    "${CMAKE_CURRENT_LIST_DIR}/system_smoke_test.c"
)
target_link_libraries(system_adapter_smoke_test PRIVATE unity adapter)
actrust_add_test(NAME system_adapter_smoke_test COMMAND system_adapter_smoke_test)
set_tests_properties(
    system_adapter_smoke_test
    PROPERTIES LABELS "actrust;adapter;smoke"
)

# ---- Network ----
add_executable(
    network_adapter_smoke_test
    "${CMAKE_CURRENT_LIST_DIR}/network_smoke_test.c"
)
target_link_libraries(network_adapter_smoke_test PRIVATE unity adapter)
actrust_add_test(NAME network_adapter_smoke_test COMMAND
              network_adapter_smoke_test
)
set_tests_properties(
    network_adapter_smoke_test
    PROPERTIES LABELS "actrust;adapter;smoke;network"
)

# ---- Storage ----
add_executable(
    storage_adapter_smoke_test
    "${CMAKE_CURRENT_LIST_DIR}/storage_smoke_test.c"
)
target_link_libraries(storage_adapter_smoke_test PRIVATE unity adapter)
actrust_add_test(NAME storage_adapter_smoke_test COMMAND
              storage_adapter_smoke_test
)
set_tests_properties(
    storage_adapter_smoke_test
    PROPERTIES LABELS "actrust;adapter;smoke"
)

# ---- Security ----
add_executable(
    security_adapter_smoke_test
    "${CMAKE_CURRENT_LIST_DIR}/security_smoke_test.c"
)
target_link_libraries(security_adapter_smoke_test PRIVATE unity adapter)
actrust_add_test(NAME security_adapter_smoke_test COMMAND
              security_adapter_smoke_test
)
set_tests_properties(
    security_adapter_smoke_test
    PROPERTIES LABELS "actrust;adapter;smoke"
)

# ---- Device ----
add_executable(
    device_adapter_smoke_test
    "${CMAKE_CURRENT_LIST_DIR}/device_smoke_test.c"
)
target_link_libraries(device_adapter_smoke_test PRIVATE unity adapter)
actrust_add_test(NAME device_adapter_smoke_test COMMAND device_adapter_smoke_test)
set_tests_properties(
    device_adapter_smoke_test
    PROPERTIES LABELS "actrust;adapter;smoke"
)

# -----------------------------------------------------------------------------
# Unit Tests
# -----------------------------------------------------------------------------

# ---- System ----
add_executable(
    system_adapter_unit_test
    "${CMAKE_CURRENT_LIST_DIR}/system_unit_test.c"
)
target_link_libraries(system_adapter_unit_test PRIVATE unity adapter)
actrust_add_test(NAME system_adapter_unit_test COMMAND system_adapter_unit_test)
set_tests_properties(
    system_adapter_unit_test
    PROPERTIES LABELS "actrust;adapter;unit"
)

# ---- Network ----
add_executable(
    network_adapter_unit_test
    "${CMAKE_CURRENT_LIST_DIR}/network_unit_test.c"
)
target_link_libraries(network_adapter_unit_test PRIVATE unity adapter)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(
        network_adapter_unit_test
        PRIVATE ACTRUST_TEST_WRAP_CLOCK_GETTIME=1
    )
    target_link_options(
        network_adapter_unit_test
        PRIVATE "-Wl,--wrap=clock_gettime"
    )
endif()
actrust_add_test(NAME network_adapter_unit_test COMMAND network_adapter_unit_test)
set_tests_properties(
    network_adapter_unit_test
    PROPERTIES LABELS "actrust;adapter;unit"
)

# ---- Storage ----
add_executable(
    storage_adapter_unit_test
    "${CMAKE_CURRENT_LIST_DIR}/storage_unit_test.c"
)
target_link_libraries(storage_adapter_unit_test PRIVATE unity adapter)
actrust_add_test(NAME storage_adapter_unit_test COMMAND storage_adapter_unit_test)
set_tests_properties(
    storage_adapter_unit_test
    PROPERTIES LABELS "actrust;adapter;unit"
)

# ---- Security ----
add_executable(
    security_adapter_unit_test
    "${CMAKE_CURRENT_LIST_DIR}/security_unit_test.c"
)
target_link_libraries(security_adapter_unit_test PRIVATE unity adapter)
actrust_add_test(NAME security_adapter_unit_test COMMAND
              security_adapter_unit_test
)
set_tests_properties(
    security_adapter_unit_test
    PROPERTIES LABELS "actrust;adapter;unit"
)

# ---- Device ----
if(CONFIG_ACTRUST_ADAPTER_PLATFORM_LINUX)
    add_executable(
        device_adapter_unit_test
        "${CMAKE_CURRENT_LIST_DIR}/device_unit_test.c"
        "${ACTRUST_ROOT}/source/adapter/platform/linux/device.c"
    )
    target_include_directories(
        device_adapter_unit_test
        PRIVATE "${ACTRUST_ROOT}/source/adapter/include"
    )
    target_compile_definitions(
        device_adapter_unit_test
        PRIVATE
            ACTRUST_LINUX_DEVICE_TEST_FIXTURE
            "ACTRUST_LINUX_HW_ID_PATH=\"fixtures/machine-id\""
            "ACTRUST_LINUX_DMI_BOARD_NAME_PATH=\"fixtures/board_name\""
            "ACTRUST_LINUX_DEVICE_TREE_MODEL_PATH=\"fixtures/model\""
            "ACTRUST_LINUX_OS_RELEASE_PATH=\"fixtures/os-release\""
    )
    target_link_libraries(device_adapter_unit_test PRIVATE unity actrust_common)
else()
    add_executable(
        device_adapter_unit_test
        "${CMAKE_CURRENT_LIST_DIR}/device_unit_test.c"
    )
    target_link_libraries(device_adapter_unit_test PRIVATE unity adapter)
endif()
actrust_add_test(NAME device_adapter_unit_test COMMAND device_adapter_unit_test)
set_tests_properties(
    device_adapter_unit_test
    PROPERTIES LABELS "actrust;adapter;unit"
)
