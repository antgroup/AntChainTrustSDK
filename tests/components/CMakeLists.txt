# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Component Tests
# =============================================================================
# Conditionally includes each component's test directory, matching the same
# Kconfig guards used by source/CMakeLists.txt to build the component itself.
# Each subdirectory registers its own smoke and unit tests.
# =============================================================================

if(CONFIG_ACTRUST_COMPONENTS_LOG)
    add_subdirectory(log)
endif()

if(CONFIG_ACTRUST_COMPONENTS_JSON)
    add_subdirectory(json)
endif()

if(CONFIG_ACTRUST_COMPONENTS_KV)
    add_subdirectory(kv)
endif()

if(CONFIG_ACTRUST_COMPONENTS_NTP)
    add_subdirectory(ntp)
endif()

if(CONFIG_ACTRUST_COMPONENTS_QUEUE)
    add_subdirectory(queue)
endif()

if(CONFIG_ACTRUST_COMPONENTS_CRYPTO)
    add_subdirectory(crypto)
endif()

if(CONFIG_ACTRUST_COMPONENTS_TLS)
    add_subdirectory(tls)
endif()

if(CONFIG_ACTRUST_COMPONENTS_MQTT)
    add_subdirectory(mqtt)
endif()

if(CONFIG_ACTRUST_COMPONENTS_CLOUD)
    add_subdirectory(cloud)
endif()
