# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK JSON Component
# =============================================================================
# Provides JSON parsing/building with pluggable backends (coreJSON/cJSON).
# =============================================================================

# Select backend sources
if(
    DEFINED CONFIG_ACTRUST_JSON_BACKEND_COREJSON
    AND CONFIG_ACTRUST_JSON_BACKEND_COREJSON
)
    set(JSON_SOURCES "${CMAKE_CURRENT_LIST_DIR}/source/core_json/json.c")
    set(JSON_BACKEND_INCLUDE "${ACTRUST_ROOT}/3rdparts/coreJSON/source/include")
    set(JSON_BACKEND_LIB core_json)
else()
    message(
        FATAL_ERROR
        "AntChainTrustSDK JSON: No valid JSON backend configured"
    )
endif()

# Create json library
add_library(json STATIC ${JSON_SOURCES})

# Include directories
target_include_directories(
    json
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE ${JSON_BACKEND_INCLUDE}
)

# Link dependencies
target_link_libraries(
    json
    PUBLIC actrust_common
    PRIVATE adapter ${JSON_BACKEND_LIB}
)
