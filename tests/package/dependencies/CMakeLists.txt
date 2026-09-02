# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

project(AntChainTrustSDKExternalDependencies LANGUAGES C)

add_library(backoff_algorithm STATIC IMPORTED GLOBAL)
set_target_properties(
    backoff_algorithm
    PROPERTIES
        IMPORTED_LOCATION
            "${ACTRUST_SDK_BUILD_DIR}/3rdparts/backoffAlgorithm/libbackoff_algorithm.a"
)

add_library(core_json STATIC IMPORTED GLOBAL)
set_target_properties(
    core_json
    PROPERTIES
        IMPORTED_LOCATION
            "${ACTRUST_SDK_BUILD_DIR}/3rdparts/coreJSON/libcore_json.a"
)

add_library(core_sntp STATIC IMPORTED GLOBAL)
set_target_properties(
    core_sntp
    PROPERTIES
        IMPORTED_LOCATION "${ACTRUST_SDK_BUILD_DIR}/3rdparts/libcore_sntp.a"
)

add_library(core_mqtt STATIC IMPORTED GLOBAL)
set_target_properties(
    core_mqtt
    PROPERTIES
        IMPORTED_LOCATION "${ACTRUST_SDK_BUILD_DIR}/3rdparts/libcore_mqtt.a"
)

add_library(core_mqtt_agent STATIC IMPORTED GLOBAL)
set_target_properties(
    core_mqtt_agent
    PROPERTIES
        IMPORTED_LOCATION
            "${ACTRUST_SDK_BUILD_DIR}/3rdparts/libcore_mqtt_agent.a"
        INTERFACE_LINK_LIBRARIES core_mqtt
)

add_library(MbedTLS::everest STATIC IMPORTED GLOBAL)
set_target_properties(
    MbedTLS::everest
    PROPERTIES
        IMPORTED_LOCATION
            "${ACTRUST_SDK_BUILD_DIR}/3rdparts/mbedtls/3rdparty/everest/libeverest.a"
)

add_library(MbedTLS::p256m STATIC IMPORTED GLOBAL)
set_target_properties(
    MbedTLS::p256m
    PROPERTIES
        IMPORTED_LOCATION
            "${ACTRUST_SDK_BUILD_DIR}/3rdparts/mbedtls/3rdparty/p256-m/libp256m.a"
)

add_library(MbedTLS::mbedcrypto STATIC IMPORTED GLOBAL)
set_target_properties(
    MbedTLS::mbedcrypto
    PROPERTIES
        IMPORTED_LOCATION
            "${ACTRUST_SDK_BUILD_DIR}/3rdparts/mbedtls/library/libmbedcrypto.a"
        INTERFACE_LINK_LIBRARIES "MbedTLS::everest;MbedTLS::p256m"
)

add_library(MbedTLS::mbedx509 STATIC IMPORTED GLOBAL)
set_target_properties(
    MbedTLS::mbedx509
    PROPERTIES
        IMPORTED_LOCATION
            "${ACTRUST_SDK_BUILD_DIR}/3rdparts/mbedtls/library/libmbedx509.a"
        INTERFACE_LINK_LIBRARIES MbedTLS::mbedcrypto
)

add_library(MbedTLS::mbedtls STATIC IMPORTED GLOBAL)
set_target_properties(
    MbedTLS::mbedtls
    PROPERTIES
        IMPORTED_LOCATION
            "${ACTRUST_SDK_BUILD_DIR}/3rdparts/mbedtls/library/libmbedtls.a"
        INTERFACE_LINK_LIBRARIES MbedTLS::mbedx509
)
