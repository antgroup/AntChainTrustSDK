# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK Crypto Component
# =============================================================================
# Provides cryptographic operations backed by Mbed TLS or platform security.
# =============================================================================

add_library(
    crypto
    STATIC
    "${CMAKE_CURRENT_LIST_DIR}/source/crypto.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/crypto_pem.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/crypto_sw.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/crypto_hw.c"
)

target_include_directories(crypto PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

target_link_libraries(
    crypto
    PUBLIC actrust_common adapter mbedcrypto
    PRIVATE mbedx509 log
)
