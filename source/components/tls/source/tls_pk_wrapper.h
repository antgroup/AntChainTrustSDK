// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tls_pk_wrapper.h
 * @brief Internal declarations for the mbedTLS PK opaque-key wrapper.
 */

#ifndef ACTRUST_TLS_PK_WRAPPER_H
#define ACTRUST_TLS_PK_WRAPPER_H

/* Third-party */
#include <mbedtls/pk.h>

/* Component */
#include "crypto/crypto.h"

/**
 * @brief Bind an AntChainTrustSDK private-key handle into an mbedTLS PK
 * context.
 *
 * The PK context must be created with @ref actrust_tls_get_pk_info.
 */
int actrust_tls_bind_actrust_key(mbedtls_pk_context  *pk,
                                 actrust_crypto_ctx_t actrust_crypto_ctx,
                                 actrust_crypto_key_t actrust_priv_key);

/** @brief Get the custom PK vtable used for AntChainTrustSDK ECDSA signing
 * bridge. */
const mbedtls_pk_info_t *actrust_tls_get_pk_info(void);

#endif /* ACTRUST_TLS_PK_WRAPPER_H */
