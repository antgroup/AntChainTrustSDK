// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tls_pk_wrapper.c
 * @brief mbedTLS PK opaque-key wrapper.
 *
 * Bridges the AntChainTrustSDK crypto layer's key handles (which may reside in
 * a TEE / secure element) into the mbedtls PK interface so the TLS handshake
 * can sign without exposing raw key material.
 */

/* C standard */
#include <stdint.h>
#include <string.h>

/* Third-party */
#include <mbedtls/error.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h>
#include <pk_wrap.h>

/* TLS */
#include "tls_pk_wrapper.h"

/* Component */
#include "crypto/crypto.h"

/* Current TLS client-key bridge supports only P-256 ECDSA with SHA-256. */
#define ACTRUST_CRYPTO_ECDSA_CURVE_BITS 256

typedef struct actrust_crypto_ecdsa_pk_ctx {
    actrust_crypto_ctx_t ctx;
    actrust_crypto_key_t priv_key;
} actrust_crypto_ecdsa_pk_ctx;

static size_t actrust_crypto_get_bitlen(mbedtls_pk_context *pk)
{
    if (pk == NULL) {
        return 0;
    } else {
        return ACTRUST_CRYPTO_ECDSA_CURVE_BITS;
    }
}

static int actrust_crypto_can_do(mbedtls_pk_type_t type)
{
    if (type == MBEDTLS_PK_ECDSA) {
        return 1;
    } else {
        return 0;
    }
}

static int actrust_crypto_verify(mbedtls_pk_context  *pk,
                                 mbedtls_md_type_t    md_alg,
                                 const unsigned char *hash, size_t hash_len,
                                 const unsigned char *sig, size_t sig_len)
{
    /* This wrapper is only used for client-side ECDSA signing in mTLS. */
    (void) pk;
    (void) md_alg;
    (void) hash;
    (void) hash_len;
    (void) sig;
    (void) sig_len;
    return MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
}

static int actrust_crypto_sign(mbedtls_pk_context *pk, mbedtls_md_type_t md_alg,
                               const unsigned char *hash, size_t hash_len,
                               unsigned char *sig, size_t sig_cap,
                               size_t *sig_len,
                               int (*f_rng)(void *, unsigned char *, size_t),
                               void *p_rng)
{
    (void) f_rng;
    (void) p_rng;

    if (pk == NULL || sig == NULL || sig_len == NULL || hash == NULL) {
        return MBEDTLS_ERR_PK_BAD_INPUT_DATA;
    }

    actrust_crypto_ecdsa_pk_ctx *c =
        (actrust_crypto_ecdsa_pk_ctx *) pk->MBEDTLS_PRIVATE(pk_ctx);
    if (c == NULL || c->ctx == NULL || c->priv_key == NULL) {
        return MBEDTLS_ERR_PK_BAD_INPUT_DATA;
    }

    actrust_crypto_hash_alg_t hash_alg;
    switch (md_alg) {
        case MBEDTLS_MD_SHA256:
            hash_alg = ACTRUST_CRYPTO_HASH_SHA256;
            break;
        default:
            return MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
    }

    *sig_len          = 0;
    actrust_err_t err = actrust_crypto_ecdsa_sign(
        c->ctx, c->priv_key, hash_alg, ACTRUST_CRYPTO_INPUT_DIGEST,
        (const uint8_t *) hash, hash_len, (uint8_t *) sig, sig_cap, sig_len);

    if (err == ACTRUST_OK) {
        return 0;
    } else if (err == ACTRUST_ERR_BUF_TOO_SMALL) {
        return MBEDTLS_ERR_PK_BUFFER_TOO_SMALL;
    } else if (err == ACTRUST_ERR_INVALID_ARG) {
        return MBEDTLS_ERR_PK_BAD_INPUT_DATA;
    } else {
        return MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
    }
}

static int actrust_crypto_decrypt(mbedtls_pk_context  *pk,
                                  const unsigned char *input, size_t ilen,
                                  unsigned char *output, size_t *olen,
                                  size_t osize,
                                  int (*f_rng)(void *, unsigned char *, size_t),
                                  void *p_rng)
{
    /* Not an RSA wrapper; decrypt path is intentionally unsupported. */
    (void) pk;
    (void) input;
    (void) ilen;
    (void) output;
    (void) olen;
    (void) osize;
    (void) f_rng;
    (void) p_rng;
    return MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
}

static int actrust_crypto_encrypt(mbedtls_pk_context  *pk,
                                  const unsigned char *input, size_t ilen,
                                  unsigned char *output, size_t *olen,
                                  size_t osize,
                                  int (*f_rng)(void *, unsigned char *, size_t),
                                  void *p_rng)
{
    /* Not an RSA wrapper; encrypt path is intentionally unsupported. */
    (void) pk;
    (void) input;
    (void) ilen;
    (void) output;
    (void) olen;
    (void) osize;
    (void) f_rng;
    (void) p_rng;
    return MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
}

static int actrust_crypto_check_pair(
    mbedtls_pk_context *pub, mbedtls_pk_context         *prv,
    int (*f_rng)(void *, unsigned char *, size_t), void *p_rng)
{
    /* Public/private pair checks are not needed by current TLS call path. */
    (void) pub;
    (void) prv;
    (void) f_rng;
    (void) p_rng;
    return MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
}

static void *actrust_crypto_ctx_alloc(void)
{
    actrust_crypto_ecdsa_pk_ctx *c =
        (actrust_crypto_ecdsa_pk_ctx *) mbedtls_calloc(1, sizeof(*c));
    return c;
}

static void actrust_crypto_ctx_free(void *ctx)
{
    if (ctx != NULL) {
        mbedtls_free(ctx);
    }
}

static void actrust_crypto_debug(mbedtls_pk_context    *pk,
                                 mbedtls_pk_debug_item *items)
{
    (void) pk;
    (void) items;
}

const mbedtls_pk_info_t actrust_crypto_ecdsa_pk_info = {
    .type            = MBEDTLS_PK_ECDSA,
    .name            = "ACTRUST-CRYPTO-ECDSA",
    .get_bitlen      = actrust_crypto_get_bitlen,
    .can_do          = actrust_crypto_can_do,
    .verify_func     = actrust_crypto_verify,
    .sign_func       = actrust_crypto_sign,
    .decrypt_func    = actrust_crypto_decrypt,
    .encrypt_func    = actrust_crypto_encrypt,
    .check_pair_func = actrust_crypto_check_pair,
    .ctx_alloc_func  = actrust_crypto_ctx_alloc,
    .ctx_free_func   = actrust_crypto_ctx_free,
    .debug_func      = actrust_crypto_debug
};

int actrust_tls_bind_actrust_key(mbedtls_pk_context  *pk,
                                 actrust_crypto_ctx_t actrust_crypto_ctx,
                                 actrust_crypto_key_t actrust_priv_key)
{
    if (pk == NULL || actrust_crypto_ctx == NULL || actrust_priv_key == NULL ||
        pk->MBEDTLS_PRIVATE(pk_info) != &actrust_crypto_ecdsa_pk_info ||
        pk->MBEDTLS_PRIVATE(pk_ctx) == NULL) {
        return MBEDTLS_ERR_PK_BAD_INPUT_DATA;
    }

    actrust_crypto_ecdsa_pk_ctx *c =
        (actrust_crypto_ecdsa_pk_ctx *) pk->MBEDTLS_PRIVATE(pk_ctx);
    c->ctx      = actrust_crypto_ctx;
    c->priv_key = actrust_priv_key;
    return 0;
}

const mbedtls_pk_info_t *actrust_tls_get_pk_info(void)
{
    return &actrust_crypto_ecdsa_pk_info;
}
