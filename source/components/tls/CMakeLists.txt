# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

# =============================================================================
# AntChainTrustSDK TLS Component
# =============================================================================
# Provides TLS client transport backed by mbedTLS and AntChainTrustSDK crypto key wrappers.
# =============================================================================

add_library(
    tls
    STATIC
    "${CMAKE_CURRENT_LIST_DIR}/source/tls.c"
    "${CMAKE_CURRENT_LIST_DIR}/source/tls_pk_wrapper.c"
)

target_include_directories(
    tls
    BEFORE
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE
        "${ACTRUST_ROOT}/source/common/include"
        "${ACTRUST_ROOT}/source/adapter/include"
        "${ACTRUST_ROOT}/3rdparts/mbedtls/library"
)

target_link_libraries(tls PUBLIC crypto PRIVATE mbedtls mbedx509 log)
