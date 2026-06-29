# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Source Directory
# =============================================================================
# Main source directory containing adapters, components, and core modules.
# =============================================================================

# =============================================================================
# Compiler Options
# =============================================================================
# Strict warnings + defensive hardening for all targets in this directory and
# subdirectories. These flags do not propagate to 3rdparts/ — that subtree is
# vendored upstream code and is intentionally compiled with its own flags.

add_compile_options(
    # ----- Diagnostics -----
    -Wall # Standard warning set
    -Wextra # Additional useful warnings
    -Werror # Treat warnings as errors
    -Wpedantic # Strict ISO C99 compliance
    -Wshadow # Catch identifier shadowing
    -Wformat=2 # Strict format-string checking
    -Wformat-security # Reject format strings whose arg is not a literal
    -Wnull-dereference # Detect statically-provable NULL dereferences
    -Wstrict-prototypes # Require K&R-style prototypes (no implicit int)
    -Wmissing-prototypes # Require declarations before definition
    # ----- Hardening -----
    -fstack-protector-strong # Guard functions with vulnerable stack frames
    -fno-omit-frame-pointer # Keep frame pointer for crash diagnostics
)

# _FORTIFY_SOURCE requires an optimization level >= 1; emitting it under
# -O0 only produces a "#warning" and is otherwise inert, so scope it to
# non-Debug configurations.
add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>)

# =============================================================================
# Optional: AddressSanitizer + UndefinedBehaviorSanitizer
# =============================================================================
# Enable with `cmake -DACTRUST_ENABLE_SANITIZERS=ON ...`.  Catches memory errors,
# leaks, signed overflow, alignment violations, and other UB at runtime.
# Incompatible with cross-compilation; intended for local Linux development.

option(
    ACTRUST_ENABLE_SANITIZERS
    "Enable AddressSanitizer + UndefinedBehaviorSanitizer"
    OFF
)

if(ACTRUST_ENABLE_SANITIZERS)
    message(
        STATUS
        "AntChainTrustSDK: AddressSanitizer + UndefinedBehaviorSanitizer enabled"
    )
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

# =============================================================================
# Subdirectories
# =============================================================================

# Project-wide public headers and dependency-free common helpers.
add_library(
    actrust_common
    STATIC
    "${CMAKE_CURRENT_LIST_DIR}/common/source/encoding.c"
    "${CMAKE_CURRENT_LIST_DIR}/common/source/memory.c"
)
target_include_directories(
    actrust_common
    PUBLIC
        "${ACTRUST_ROOT}/include"
        "${CMAKE_BINARY_DIR}/config"
        "${ACTRUST_ROOT}/source/adapter/include"
        "${CMAKE_CURRENT_LIST_DIR}/common/include"
)

# Platform adapter layer
add_subdirectory(adapter)

# Optional components
if(CONFIG_ACTRUST_COMPONENTS_LOG)
    message(STATUS "AntChainTrustSDK: Enabling log component")
    add_subdirectory(components/log)
endif()

if(CONFIG_ACTRUST_COMPONENTS_JSON)
    message(STATUS "AntChainTrustSDK: Enabling JSON component")
    add_subdirectory(components/json)
endif()

if(CONFIG_ACTRUST_COMPONENTS_KV)
    message(STATUS "AntChainTrustSDK: Enabling KV component")
    add_subdirectory(components/kv)
endif()

if(CONFIG_ACTRUST_COMPONENTS_NTP)
    message(STATUS "AntChainTrustSDK: Enabling NTP component")
    add_subdirectory(components/ntp)
endif()

if(CONFIG_ACTRUST_COMPONENTS_QUEUE)
    message(STATUS "AntChainTrustSDK: Enabling Queue component")
    add_subdirectory(components/queue)
endif()

if(CONFIG_ACTRUST_COMPONENTS_CRYPTO)
    message(STATUS "AntChainTrustSDK: Enabling Crypto component")
    add_subdirectory(components/crypto)
endif()

if(CONFIG_ACTRUST_COMPONENTS_TLS)
    message(STATUS "AntChainTrustSDK: Enabling TLS component")
    add_subdirectory(components/tls)
endif()

if(CONFIG_ACTRUST_COMPONENTS_MQTT)
    message(STATUS "AntChainTrustSDK: Enabling MQTT component")
    add_subdirectory(components/mqtt)
endif()

if(CONFIG_ACTRUST_COMPONENTS_CLOUD)
    message(STATUS "AntChainTrustSDK: Enabling Cloud component")
    add_subdirectory(components/cloud)
endif()

# Core module
foreach(
    component
    LOG
    JSON
    KV
    NTP
    QUEUE
    CRYPTO
    TLS
    MQTT
    CLOUD
)
    if(NOT CONFIG_ACTRUST_COMPONENTS_${component})
        message(
            FATAL_ERROR
            "AntChainTrustSDK Core requires CONFIG_ACTRUST_COMPONENTS_${component}=y; "
            "building Core without this component is not currently supported"
        )
    endif()
endforeach()

message(STATUS "AntChainTrustSDK: Enabling Core module")
add_subdirectory(core)
