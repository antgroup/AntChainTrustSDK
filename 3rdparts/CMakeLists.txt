# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Third-Party Dependencies
# =============================================================================
# This module manages external libraries and dependencies used by AntChainTrustSDK. All
# third-party components are conditionally included based on configuration.
# =============================================================================

function(actrust_require_submodule name path entry)
    if(NOT IS_DIRECTORY "${path}" OR NOT EXISTS "${path}/${entry}")
        message(
            FATAL_ERROR
            "AntChainTrustSDK submodule '${name}' is not initialized: ${path}\n"
            "Expected entry point: ${path}/${entry}\n"
            "Run: git submodule update --init --recursive"
        )
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Backoff Algorithm
# -----------------------------------------------------------------------------
if(CONFIG_ACTRUST_3RDPARTS_BACKOFF_BACKOFFALGO)
    actrust_require_submodule(
        "backoffAlgorithm"
        "${CMAKE_CURRENT_LIST_DIR}/backoffAlgorithm"
        CMakeLists.txt
    )
    message(STATUS "AntChainTrustSDK: Including backoffAlgorithm library")
    add_subdirectory(backoffAlgorithm)
endif()

# -----------------------------------------------------------------------------
# JSON Parser (coreJSON)
# -----------------------------------------------------------------------------
if(CONFIG_ACTRUST_3RDPARTS_JSON_COREJSON)
    actrust_require_submodule(
        "coreJSON"
        "${CMAKE_CURRENT_LIST_DIR}/coreJSON"
        CMakeLists.txt
    )
    message(STATUS "AntChainTrustSDK: Including coreJSON library")
    add_subdirectory(coreJSON)
endif()

# -----------------------------------------------------------------------------
# SNTP Client (coreSNTP)
# -----------------------------------------------------------------------------
if(CONFIG_ACTRUST_3RDPARTS_NTP_CORESNTP)
    actrust_require_submodule(
        "coreSNTP"
        "${CMAKE_CURRENT_LIST_DIR}/coreSNTP"
        CMakeLists.txt
    )
    message(STATUS "AntChainTrustSDK: Including coreSNTP library")
    add_subdirectory(coreSNTP)
endif()

# -----------------------------------------------------------------------------
# Cryptography Library (Mbed TLS)
# -----------------------------------------------------------------------------
if(CONFIG_ACTRUST_3RDPARTS_CRYPTO_MBEDTLS)
    actrust_require_submodule(
        "mbedTLS"
        "${CMAKE_CURRENT_LIST_DIR}/mbedtls"
        CMakeLists.txt
    )
    message(STATUS "AntChainTrustSDK: Including mbedTLS library")
    # Disable Mbed TLS tests and programs to reduce build time
    set(ENABLE_TESTING
        OFF
        CACHE BOOL "Disable Mbed TLS tests")
    set(ENABLE_PROGRAMS
        OFF
        CACHE BOOL "Disable Mbed TLS programs")
    add_subdirectory(mbedtls)
endif()

# -----------------------------------------------------------------------------
# MQTT Client (coreMQTT)
# -----------------------------------------------------------------------------
if(CONFIG_ACTRUST_3RDPARTS_MQTT_COREMQTT)
    actrust_require_submodule(
        "coreMQTT"
        "${CMAKE_CURRENT_LIST_DIR}/coreMQTT"
        mqttFilePaths.cmake
    )
    actrust_require_submodule(
        "coreMQTT-Agent"
        "${CMAKE_CURRENT_LIST_DIR}/coreMQTT-Agent"
        mqttAgentFilePaths.cmake
    )
    message(STATUS "AntChainTrustSDK: Including coreMQTT library")

    # Include MQTT source file paths
    include(${CMAKE_CURRENT_LIST_DIR}/coreMQTT/mqttFilePaths.cmake)

    # Create MQTT library
    add_library(core_mqtt STATIC ${MQTT_SOURCES} ${MQTT_SERIALIZER_SOURCES})

    # Set include directories
    target_include_directories(core_mqtt PUBLIC ${MQTT_INCLUDE_PUBLIC_DIRS})

    # Configure to use default config
    target_compile_definitions(core_mqtt PUBLIC MQTT_DO_NOT_USE_CUSTOM_CONFIG)

    # Include MQTT Agent source file paths
    include(${CMAKE_CURRENT_LIST_DIR}/coreMQTT-Agent/mqttAgentFilePaths.cmake)

    # Create MQTT Agent library
    add_library(core_mqtt_agent STATIC ${MQTT_AGENT_SOURCES})

    # Set include directories
    target_include_directories(core_mqtt_agent PUBLIC ${MQTT_AGENT_INCLUDE_PUBLIC_DIRS})

    # Depend on coreMQTT public headers/config
    target_link_libraries(core_mqtt_agent PUBLIC core_mqtt)

    # Configure to use default config
    target_compile_definitions(core_mqtt_agent PUBLIC MQTT_AGENT_DO_NOT_USE_CUSTOM_CONFIG)
endif()

# -----------------------------------------------------------------------------
# Unity (test framework)
# -----------------------------------------------------------------------------
if(BUILD_TESTING)
    actrust_require_submodule(
        "Unity"
        "${CMAKE_CURRENT_LIST_DIR}/Unity"
        CMakeLists.txt
    )
    message(STATUS "AntChainTrustSDK: Including Unity test framework")
    add_subdirectory(Unity)
endif()
