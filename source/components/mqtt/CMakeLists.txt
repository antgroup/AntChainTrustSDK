# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK MQTT Component
# =============================================================================
# Wraps coreMQTT with AntChainTrustSDK APIs: lifecycle, queued publish, and poll drive.
# =============================================================================

add_library(mqtt STATIC "${CMAKE_CURRENT_LIST_DIR}/source/mqtt.c")

target_include_directories(mqtt PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

target_link_libraries(
    mqtt
    PUBLIC tls
    PRIVATE queue log core_mqtt_agent backoff_algorithm
)
