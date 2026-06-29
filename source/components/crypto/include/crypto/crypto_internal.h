// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file crypto_internal.h
 * @brief Shared internal definitions for crypto backends.
 *
 * Provides the backend operations vtable and unified key handle used
 * by the dispatch layer (crypto.c), the SW backend (crypto_sw.c),
 * and the HW backend (crypto_hw.c).  NOT part of the public API.
 */

#ifndef ACTRUST_CRYPTO_INTERNAL_H
#define ACTRUST_CRYPTO_INTERNAL_H

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Third-party */
#include <mbedtls/pk.h>

/* Crypto */
#include "crypto/crypto.h"

/* Adapter */
#include "adapter/security.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AES_KEY_MAX_BYTES      32u
#define CRYPTO_MAX_DIGEST_SIZE 64u

static inline size_t crypto_hash_len_bytes(actrust_crypto_hash_alg_t alg)
{
    switch (alg) {
        case ACTRUST_CRYPTO_HASH_SHA256:
            return 32u;
        case ACTRUST_CRYPTO_HASH_SHA384:
            return 48u;
        case ACTRUST_CRYPTO_HASH_SHA512:
            return 64u;
        default:
            return 0u;
    }
}

/* ========================================================================
 * Key Descriptor Table
 * ======================================================================== */

typedef struct {
    uint32_t                  key_id;
    actrust_crypto_key_type_t type;
    actrust_crypto_backend_t  backend;
    uint8_t                   slot_index;
    union {
        actrust_crypto_ec_curve_t curve;
        size_t                    key_bits;
    } spec;
} actrust_crypto_key_desc_t;

extern const actrust_crypto_key_desc_t g_key_table[ACTRUST_CRYPTO_KEY_ID_COUNT];

typedef struct {
    uint32_t cert_id;
    uint8_t  slot_index;
} actrust_crypto_cert_desc_t;

extern const actrust_crypto_cert_desc_t
    g_cert_table[ACTRUST_CRYPTO_CERT_ID_COUNT];

/* ========================================================================
 * Key Generation Parameters (internal — used by backend vtable)
 * ======================================================================== */

typedef struct {
    actrust_crypto_key_type_t type;
    union {
        actrust_crypto_ec_curve_t curve;
        size_t                    key_bits;
    } spec;
} actrust_crypto_key_gen_params_t;

/* ========================================================================
 * Backend Operations Vtable
 * ======================================================================== */

/**
 * @brief Function-pointer table shared by HW and SW backends.
 */
typedef struct {
    /* Random */
    actrust_err_t (*random)(actrust_crypto_ctx_t ctx, uint8_t *out,
                            size_t out_len);

    /* Hash */
    actrust_err_t (*hash)(actrust_crypto_ctx_t      ctx,
                          actrust_crypto_hash_alg_t alg, const uint8_t *input,
                          size_t input_len, uint8_t *out, size_t out_cap,
                          size_t *out_len);

    /* Key management */
    actrust_err_t (*key_open)(actrust_crypto_ctx_t ctx, uint32_t key_id,
                              actrust_crypto_key_t *out_key);

    actrust_err_t (*key_close)(actrust_crypto_ctx_t  ctx,
                               actrust_crypto_key_t *key);

    actrust_err_t (*key_generate)(actrust_crypto_ctx_t ctx, uint32_t key_id,
                                  const actrust_crypto_key_gen_params_t *params,
                                  actrust_crypto_key_t *out_key);

    actrust_err_t (*key_import)(actrust_crypto_ctx_t ctx, uint32_t key_id,
                                actrust_crypto_format_t format,
                                const uint8_t *key, size_t key_len,
                                actrust_crypto_key_t *out_key);

    actrust_err_t (*key_destroy)(actrust_crypto_ctx_t ctx, uint32_t key_id);

    actrust_err_t (*key_export_public)(actrust_crypto_ctx_t ctx,
                                       actrust_crypto_key_t key, uint8_t *out,
                                       size_t out_cap, size_t *out_len);

    /* ECDSA (dispatch hashes MESSAGE input; backends accept DIGEST input) */
    actrust_err_t (*ecdsa_sign)(actrust_crypto_ctx_t      ctx,
                                actrust_crypto_key_t      key,
                                actrust_crypto_hash_alg_t hash_alg,
                                actrust_crypto_input_t    input_type,
                                const uint8_t *msg, size_t msg_len,
                                uint8_t *sig, size_t sig_cap, size_t *sig_len);

    actrust_err_t (*ecdsa_verify)(actrust_crypto_ctx_t      ctx,
                                  actrust_crypto_key_t      key,
                                  actrust_crypto_hash_alg_t hash_alg,
                                  actrust_crypto_input_t    input_type,
                                  const uint8_t *msg, size_t msg_len,
                                  const uint8_t *sig, size_t sig_len);

    /* AES */
    actrust_err_t (*aes_encrypt)(
        actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
        actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
        const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
        const uint8_t *input, size_t input_len, uint8_t *output,
        size_t output_cap, size_t *output_len, uint8_t *tag, size_t *tag_len);

    actrust_err_t (*aes_decrypt)(
        actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
        actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
        const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
        const uint8_t *input, size_t input_len, uint8_t *output,
        size_t output_cap, size_t *output_len, const uint8_t *tag,
        size_t tag_len);

    /* CSR */
    actrust_err_t (*csr_generate)(actrust_crypto_ctx_t      ctx,
                                  actrust_crypto_key_t      key,
                                  actrust_crypto_hash_alg_t hash_alg,
                                  const char *subject, uint8_t *out,
                                  size_t out_cap, size_t *out_len);
} crypto_backend_ops_t;

extern const crypto_backend_ops_t hw_crypto_ops;
extern const crypto_backend_ops_t sw_crypto_ops;

/* ========================================================================
 * Backend Lifecycle
 * ======================================================================== */

actrust_err_t sw_crypto_init(void **out_ctx);
actrust_err_t sw_crypto_deinit(void *sw_ctx);

actrust_err_t hw_crypto_init(void **out_ctx);
actrust_err_t hw_crypto_deinit(void *hw_ctx);

const actrust_crypto_key_desc_t  *actrust_crypto_key_lookup(uint32_t key_id);
actrust_sec_slot_t                keyid_to_slotid(uint32_t key_id);
const actrust_crypto_cert_desc_t *actrust_crypto_cert_lookup(uint32_t cert_id);
actrust_sec_slot_t                certid_to_slotid(uint32_t cert_id);

/* ========================================================================
 * Crypto Context (internal layout)
 * ======================================================================== */

struct actrust_crypto_ctx {
    void *sw_ctx; /**< Opaque SW backend state, always non-NULL after init */
    void *hw_ctx; /**< Opaque HW backend state, NULL when HW is absent */
    const crypto_backend_ops_t *sw; /**< &sw_crypto_ops */
    const crypto_backend_ops_t *hw; /**< &hw_crypto_ops */
};

/* ========================================================================
 * Unified Key Handle
 * ======================================================================== */

struct actrust_crypto_key {
    actrust_crypto_backend_t    backend; /**< Which backend owns this key */
    const crypto_backend_ops_t *ops;
    actrust_crypto_key_type_t   type; /**< EC or AES */
    union {
        struct {
            union {
                mbedtls_pk_context pk;
                struct {
                    uint8_t data[AES_KEY_MAX_BYTES];
                    size_t  bits;
                } aes;
            } u;
        } sw;
        struct {
            actrust_sec_slot_t slot_id;
        } hw;
    } key;
};

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_CRYPTO_INTERNAL_H */
