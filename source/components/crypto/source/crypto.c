// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file crypto.c
 * @brief Cryptographic operations — dispatch layer.
 *
 * Validates parameters and routes each public API call to the
 * appropriate backend (HW or SW) via the ops vtable, based on
 * compile-time HW capabilities (Kconfig) and key ownership.
 */

/* C standard */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* Common */
#include "common/common.h"

/* Project */
#include "actrust_config.h"

/* Crypto */
#include "crypto/crypto_internal.h"

/* Component */
#include "log/log.h"

#define CRYPTO_ERR(reason)                                                     \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_CRYPTO, (reason))

/* --- Key descriptor table ------------------------------------------------ */
const actrust_crypto_key_desc_t g_key_table[ACTRUST_CRYPTO_KEY_ID_COUNT] = {
    {
        .key_id = ACTRUST_CRYPTO_KEY_ID_EC_0,
        .type   = ACTRUST_CRYPTO_KEY_EC,
#if defined(CONFIG_ACTRUST_CRYPTO_KEY_PROFILE_PRODUCTION)
        .backend = ACTRUST_CRYPTO_BACKEND_HW,
#else
        .backend = ACTRUST_CRYPTO_BACKEND_SW,
#endif
        .slot_index        = 0,
        .requires_key_mgmt = true,
        .requires_ecdsa    = true,
        .spec.curve        = ACTRUST_CRYPTO_EC_SECP256R1,
    },
    {
        .key_id = ACTRUST_CRYPTO_KEY_ID_EC_1,
        .type   = ACTRUST_CRYPTO_KEY_EC,
#if defined(CONFIG_ACTRUST_CRYPTO_KEY_PROFILE_PRODUCTION)
        .backend = ACTRUST_CRYPTO_BACKEND_HW,
#else
        .backend = ACTRUST_CRYPTO_BACKEND_SW,
#endif
        .slot_index        = 1,
        .requires_key_mgmt = true,
        .requires_ecdsa    = true,
        .spec.curve        = ACTRUST_CRYPTO_EC_SECP256R1,
    },
    {
        .key_id = ACTRUST_CRYPTO_KEY_ID_AES_0,
        .type   = ACTRUST_CRYPTO_KEY_AES,
#if defined(CONFIG_ACTRUST_CRYPTO_KEY_PROFILE_PRODUCTION)
        .backend = ACTRUST_CRYPTO_BACKEND_HW,
#else
        .backend = ACTRUST_CRYPTO_BACKEND_SW,
#endif
        .slot_index        = 2,
        .requires_key_mgmt = true,
        .requires_aes      = true,
        .spec.key_bits     = 256,
    },
    {
        .key_id = ACTRUST_CRYPTO_KEY_ID_EC_2,
        .type   = ACTRUST_CRYPTO_KEY_EC,
#if defined(CONFIG_ACTRUST_CRYPTO_KEY_PROFILE_PRODUCTION)
        .backend = ACTRUST_CRYPTO_BACKEND_HW,
#else
        .backend = ACTRUST_CRYPTO_BACKEND_SW,
#endif
        .slot_index        = 3,
        .requires_key_mgmt = true,
        .requires_ecdsa    = true,
        .spec.curve        = ACTRUST_CRYPTO_EC_SECP256R1,
    },
};

const actrust_crypto_cert_desc_t g_cert_table[ACTRUST_CRYPTO_CERT_ID_COUNT] = {
    {
        .cert_id    = ACTRUST_CRYPTO_CERT_ID_X509_0,
        .slot_index = 0u,
    },
    {
        .cert_id    = ACTRUST_CRYPTO_CERT_ID_X509_1,
        .slot_index = 1u,
    },
};

/* --- Key table lookup and backend selection ------------------------------ */
const actrust_crypto_key_desc_t *actrust_crypto_key_lookup(uint32_t key_id)
{
    for (size_t i = 0; i < ACTRUST_CRYPTO_KEY_ID_COUNT; ++i) {
        if (g_key_table[i].key_id == key_id) {
            return &g_key_table[i];
        }
    }
    return NULL;
}

actrust_sec_slot_t keyid_to_slotid(uint32_t key_id)
{
    const actrust_crypto_key_desc_t *desc = actrust_crypto_key_lookup(key_id);
    if (desc != NULL) {
        return ACTRUST_SEC_SLOT_KEY(desc->slot_index);
    }
    return (actrust_sec_slot_t) key_id;
}

const actrust_crypto_cert_desc_t *actrust_crypto_cert_lookup(uint32_t cert_id)
{
    for (size_t i = 0; i < ACTRUST_CRYPTO_CERT_ID_COUNT; ++i) {
        if (g_cert_table[i].cert_id == cert_id) {
            return &g_cert_table[i];
        }
    }
    return NULL;
}

actrust_sec_slot_t certid_to_slotid(uint32_t cert_id)
{
    const actrust_crypto_cert_desc_t *desc =
        actrust_crypto_cert_lookup(cert_id);
    if (desc != NULL) {
        return ACTRUST_SEC_SLOT_CERT(desc->slot_index);
    }
    return (actrust_sec_slot_t) cert_id;
}

actrust_err_t actrust_crypto_cert_write(uint32_t cert_id, const uint8_t *cert,
                                        size_t cert_len)
{
    const actrust_crypto_cert_desc_t *desc =
        actrust_crypto_cert_lookup(cert_id);
    if (desc == NULL || cert == NULL || cert_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return actrust_sec_store_write(ACTRUST_SEC_SLOT_CERT(desc->slot_index),
                                   cert, cert_len);
}

actrust_err_t actrust_crypto_cert_read(uint32_t cert_id, uint8_t *out,
                                       size_t out_cap, size_t *out_len)
{
    const actrust_crypto_cert_desc_t *desc =
        actrust_crypto_cert_lookup(cert_id);
    if (desc == NULL || out == NULL || out_cap == 0u || out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return actrust_sec_store_read(ACTRUST_SEC_SLOT_CERT(desc->slot_index), out,
                                  out_cap, out_len);
}

actrust_err_t actrust_crypto_cert_delete(uint32_t cert_id)
{
    const actrust_crypto_cert_desc_t *desc =
        actrust_crypto_cert_lookup(cert_id);
    if (desc == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return actrust_sec_store_delete(ACTRUST_SEC_SLOT_CERT(desc->slot_index));
}

static actrust_err_t actrust_crypto_validate_descriptors(void)
{
    const actrust_sec_capabilities_t caps = actrust_sec_get_capabilities();

    for (size_t i = 0; i < ACTRUST_CRYPTO_KEY_ID_COUNT; ++i) {
        const actrust_crypto_key_desc_t *desc = &g_key_table[i];
        if (desc->slot_index >= ACTRUST_SEC_SLOT_KEY_COUNT) {
            return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
        }
        if (desc->backend == ACTRUST_CRYPTO_BACKEND_HW) {
            if (desc->requires_key_mgmt &&
                (caps & ACTRUST_SEC_CAP_KEY_MGMT) == 0u) {
                return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
            }
            if (desc->requires_ecdsa && (caps & ACTRUST_SEC_CAP_ECDSA) == 0u) {
                return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
            }
            if (desc->requires_aes && (caps & ACTRUST_SEC_CAP_AES) == 0u) {
                return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
            }
        }
    }

    return ACTRUST_OK;
}

static inline const crypto_backend_ops_t *actrust_crypto_get_ops(
    actrust_crypto_ctx_t ctx, const actrust_crypto_key_desc_t *desc)
{
    return (desc->backend == ACTRUST_CRYPTO_BACKEND_HW) ? ctx->hw : ctx->sw;
}

/* ========================================================================
 * Context Lifecycle
 * ======================================================================== */

actrust_err_t actrust_crypto_init(actrust_crypto_ctx_t *ctx)
{
    if (ctx == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *ctx = NULL;

    actrust_err_t descriptor_err = actrust_crypto_validate_descriptors();
    if (descriptor_err != ACTRUST_OK) {
        LOG_ERROR(
            "crypto descriptor capability validation failed: 0x%08" PRIx32,
            descriptor_err);
        return descriptor_err;
    }

    actrust_crypto_ctx_t new_ctx = ACTRUST_CALLOC(1, sizeof(*new_ctx));
    if (new_ctx == NULL) {
        LOG_ERROR("crypto context allocation failed");
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }

    new_ctx->sw = &sw_crypto_ops;
    new_ctx->hw = &hw_crypto_ops;

    actrust_err_t err = sw_crypto_init(&new_ctx->sw_ctx);
    if (err != ACTRUST_OK) {
        LOG_ERROR("software crypto init failed: 0x%08" PRIx32, err);
        ACTRUST_FREE(new_ctx);
        return err;
    }

    err = hw_crypto_init(&new_ctx->hw_ctx);
    if (err != ACTRUST_OK) {
        LOG_ERROR("hardware crypto init failed: 0x%08" PRIx32, err);
        sw_crypto_deinit(new_ctx->sw_ctx);
        ACTRUST_FREE(new_ctx);
        return err;
    }

    *ctx = new_ctx;
    LOG_INFO("crypto initialized");
    return ACTRUST_OK;
}

actrust_err_t actrust_crypto_deinit(actrust_crypto_ctx_t *ctx)
{
    if (ctx == NULL || *ctx == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_crypto_ctx_t c = *ctx;

    sw_crypto_deinit(c->sw_ctx);
    hw_crypto_deinit(c->hw_ctx);

    ACTRUST_FREE(c);
    *ctx = NULL;
    LOG_INFO("crypto deinitialized");
    return ACTRUST_OK;
}

/* ========================================================================
 * Random Number Generation
 * ======================================================================== */

actrust_err_t actrust_crypto_random(actrust_crypto_ctx_t ctx, uint8_t *out,
                                    size_t out_len)
{
    if (ctx == NULL || out == NULL || out_len == 0) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

#ifdef CONFIG_ACTRUST_SEC_HW_RANDOM
    return ctx->hw->random(ctx, out, out_len);
#else
    return ctx->sw->random(ctx, out, out_len);
#endif
}

/* ========================================================================
 * Hash
 * ======================================================================== */

actrust_err_t actrust_crypto_hash(actrust_crypto_ctx_t      ctx,
                                  actrust_crypto_hash_alg_t alg,
                                  const uint8_t *input, size_t input_len,
                                  uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (ctx == NULL || (input == NULL && input_len > 0u) || out == NULL ||
        out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (crypto_hash_len_bytes(alg) == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

#ifdef CONFIG_ACTRUST_SEC_HW_HASH
    return ctx->hw->hash(ctx, alg, input, input_len, out, out_cap, out_len);
#else
    return ctx->sw->hash(ctx, alg, input, input_len, out, out_cap, out_len);
#endif
}

/* ========================================================================
 * Key Management
 * ======================================================================== */

actrust_err_t actrust_crypto_key_open(actrust_crypto_ctx_t ctx, uint32_t key_id,
                                      actrust_crypto_key_t *out_key)
{
    if (ctx == NULL || out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *out_key = NULL;

    const actrust_crypto_key_desc_t *desc = actrust_crypto_key_lookup(key_id);
    if (desc == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const crypto_backend_ops_t *ops = actrust_crypto_get_ops(ctx, desc);
    actrust_err_t               err = ops->key_open(ctx, key_id, out_key);
    if (err != ACTRUST_OK) {
        LOG_DEBUG("crypto key open failed: key_id=%" PRIu32 " err=0x%08" PRIx32,
                  key_id, err);
        return err;
    }
    (*out_key)->ops = ops;
    return ACTRUST_OK;
}

actrust_err_t actrust_crypto_key_close(actrust_crypto_ctx_t  ctx,
                                       actrust_crypto_key_t *key)
{
    if (ctx == NULL || key == NULL || *key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return (*key)->ops->key_close(ctx, key);
}

actrust_err_t actrust_crypto_key_generate(actrust_crypto_ctx_t  ctx,
                                          uint32_t              key_id,
                                          actrust_crypto_key_t *out_key)
{
    if (ctx == NULL || out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *out_key = NULL;

    const actrust_crypto_key_desc_t *desc = actrust_crypto_key_lookup(key_id);
    if (desc == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const crypto_backend_ops_t *ops = actrust_crypto_get_ops(ctx, desc);

    actrust_crypto_key_gen_params_t params;
    params.type = desc->type;
    if (desc->type == ACTRUST_CRYPTO_KEY_EC) {
        params.spec.curve = desc->spec.curve;
    } else {
        params.spec.key_bits = desc->spec.key_bits;
    }

    LOG_DEBUG("crypto key generate: key_id=%" PRIu32, key_id);
    actrust_err_t err = ops->key_generate(ctx, key_id, &params, out_key);
    if (err != ACTRUST_OK) {
        LOG_ERROR("crypto key generate failed: key_id=%" PRIu32
                  " err=0x%08" PRIx32,
                  key_id, err);
        return err;
    }
    (*out_key)->ops = ops;
    return ACTRUST_OK;
}

actrust_err_t actrust_crypto_key_migrate(actrust_crypto_ctx_t ctx,
                                         uint32_t source_id, uint32_t target_id)
{
    if (ctx == NULL || source_id == target_id) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const actrust_crypto_key_desc_t *source =
        actrust_crypto_key_lookup(source_id);
    const actrust_crypto_key_desc_t *target =
        actrust_crypto_key_lookup(target_id);
    if (source == NULL || target == NULL || source->type != target->type ||
        source->backend != target->backend) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    const crypto_backend_ops_t *ops = actrust_crypto_get_ops(ctx, source);
    if (ops->key_migrate == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }
    return ops->key_migrate(ctx, source_id, target_id);
}

actrust_err_t actrust_crypto_key_import(actrust_crypto_ctx_t    ctx,
                                        uint32_t                key_id,
                                        actrust_crypto_format_t format,
                                        const uint8_t *key, size_t key_len,
                                        actrust_crypto_key_t *out_key)
{
    if (ctx == NULL || key == NULL || key_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (out_key != NULL) {
        *out_key = NULL;
    }

    const actrust_crypto_key_desc_t *desc = actrust_crypto_key_lookup(key_id);
    if (desc == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const crypto_backend_ops_t *ops = actrust_crypto_get_ops(ctx, desc);
    actrust_crypto_key_t        imported_key = NULL;
    actrust_err_t               err =
        ops->key_import(ctx, key_id, format, key, key_len, &imported_key);
    if (err != ACTRUST_OK) {
        LOG_ERROR("crypto key import failed: key_id=%" PRIu32
                  " err=0x%08" PRIx32,
                  key_id, err);
        return err;
    }
    if (imported_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    imported_key->ops = ops;
    if (out_key != NULL) {
        *out_key = imported_key;
        return ACTRUST_OK;
    } else {
        err = ops->key_close(ctx, &imported_key);
        if (err != ACTRUST_OK) {
            LOG_WARN("crypto imported key close failed after persist: "
                     "key_id=%" PRIu32 " err=0x%08" PRIx32,
                     key_id, err);
        }
    }
    return ACTRUST_OK;
}

actrust_err_t actrust_crypto_key_destroy(actrust_crypto_ctx_t ctx,
                                         uint32_t             key_id)
{
    if (ctx == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const actrust_crypto_key_desc_t *desc = actrust_crypto_key_lookup(key_id);
    if (desc == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const crypto_backend_ops_t *ops = actrust_crypto_get_ops(ctx, desc);
    actrust_err_t               err = ops->key_destroy(ctx, key_id);
    if (err != ACTRUST_OK) {
        LOG_WARN("crypto key destroy failed: key_id=%" PRIu32
                 " err=0x%08" PRIx32,
                 key_id, err);
    } else {
        LOG_DEBUG("crypto key destroyed: key_id=%" PRIu32, key_id);
    }
    return err;
}

actrust_err_t actrust_crypto_key_export_public(actrust_crypto_ctx_t ctx,
                                               actrust_crypto_key_t key,
                                               uint8_t *out, size_t out_cap,
                                               size_t *out_len)
{
    if (ctx == NULL || key == NULL || out == NULL || out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return key->ops->key_export_public(ctx, key, out, out_cap, out_len);
}

/* ========================================================================
 * ECDSA (dispatch layer pre-hashes MESSAGE input)
 * ======================================================================== */

actrust_err_t actrust_crypto_ecdsa_sign(actrust_crypto_ctx_t      ctx,
                                        actrust_crypto_key_t      priv_key,
                                        actrust_crypto_hash_alg_t hash_alg,
                                        actrust_crypto_input_t    input_type,
                                        const uint8_t *msg, size_t msg_len,
                                        uint8_t *sig, size_t sig_cap,
                                        size_t *sig_len)
{
    if (ctx == NULL || priv_key == NULL || msg == NULL || sig == NULL ||
        sig_len == NULL || sig_cap == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    uint8_t        digest[CRYPTO_MAX_DIGEST_SIZE];
    size_t         digest_len = 0;
    const uint8_t *hash_ptr   = msg;
    size_t         hash_len   = msg_len;
    actrust_err_t  err        = ACTRUST_OK;

    if (input_type == ACTRUST_CRYPTO_INPUT_MESSAGE) {
        err = actrust_crypto_hash(ctx, hash_alg, msg, msg_len, digest,
                                  sizeof(digest), &digest_len);
        if (err != ACTRUST_OK) {
            goto cleanup;
        }
        hash_ptr = digest;
        hash_len = digest_len;
    } else if (input_type == ACTRUST_CRYPTO_INPUT_DIGEST) {
        const size_t expected_hash_len = crypto_hash_len_bytes(hash_alg);
        if (expected_hash_len == 0u) {
            err = CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
            goto cleanup;
        }
        if (msg_len != expected_hash_len) {
            err = CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
            goto cleanup;
        }
    } else {
        err = CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
        goto cleanup;
    }

    err = priv_key->ops->ecdsa_sign(ctx, priv_key, hash_alg,
                                    ACTRUST_CRYPTO_INPUT_DIGEST, hash_ptr,
                                    hash_len, sig, sig_cap, sig_len);

cleanup:
    actrust_secure_zeroize(digest, sizeof(digest));
    return err;
}

actrust_err_t actrust_crypto_ecdsa_verify(actrust_crypto_ctx_t      ctx,
                                          actrust_crypto_key_t      pub_key,
                                          actrust_crypto_hash_alg_t hash_alg,
                                          actrust_crypto_input_t    input_type,
                                          const uint8_t *msg, size_t msg_len,
                                          const uint8_t *sig, size_t sig_len)
{
    if (ctx == NULL || pub_key == NULL || msg == NULL || sig == NULL ||
        sig_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    uint8_t        digest[CRYPTO_MAX_DIGEST_SIZE];
    size_t         digest_len = 0;
    const uint8_t *hash_ptr   = msg;
    size_t         hash_len   = msg_len;
    actrust_err_t  err        = ACTRUST_OK;

    if (input_type == ACTRUST_CRYPTO_INPUT_MESSAGE) {
        err = actrust_crypto_hash(ctx, hash_alg, msg, msg_len, digest,
                                  sizeof(digest), &digest_len);
        if (err != ACTRUST_OK) {
            goto cleanup;
        }
        hash_ptr = digest;
        hash_len = digest_len;
    } else if (input_type == ACTRUST_CRYPTO_INPUT_DIGEST) {
        const size_t expected_hash_len = crypto_hash_len_bytes(hash_alg);
        if (expected_hash_len == 0u) {
            err = CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
            goto cleanup;
        }
        if (msg_len != expected_hash_len) {
            err = CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
            goto cleanup;
        }
    } else {
        err = CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
        goto cleanup;
    }

    err = pub_key->ops->ecdsa_verify(ctx, pub_key, hash_alg,
                                     ACTRUST_CRYPTO_INPUT_DIGEST, hash_ptr,
                                     hash_len, sig, sig_len);

cleanup:
    actrust_secure_zeroize(digest, sizeof(digest));
    return err;
}

/* ========================================================================
 * Symmetric Encryption (AES)
 * ======================================================================== */

actrust_err_t actrust_crypto_aes_encrypt(
    actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
    actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
    const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
    const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap,
    size_t *output_len, uint8_t *tag, size_t *tag_len)
{
    if (ctx == NULL || key == NULL || iv == NULL || input == NULL ||
        output == NULL || output_len == NULL || (aad == NULL && aad_len > 0u)) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (alg != ACTRUST_CRYPTO_AES_GCM && alg != ACTRUST_CRYPTO_AES_CBC) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    if (alg == ACTRUST_CRYPTO_AES_GCM &&
        (tag == NULL || tag_len == NULL || *tag_len < 4u || *tag_len > 16u)) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return key->ops->aes_encrypt(ctx, key, alg, padding, iv, iv_len, aad,
                                 aad_len, input, input_len, output, output_cap,
                                 output_len, tag, tag_len);
}

actrust_err_t actrust_crypto_aes_decrypt(
    actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
    actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
    const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
    const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap,
    size_t *output_len, const uint8_t *tag, size_t tag_len)
{
    if (ctx == NULL || key == NULL || iv == NULL || input == NULL ||
        output == NULL || output_len == NULL || (aad == NULL && aad_len > 0u)) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (alg != ACTRUST_CRYPTO_AES_GCM && alg != ACTRUST_CRYPTO_AES_CBC) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    if (alg == ACTRUST_CRYPTO_AES_GCM &&
        (tag == NULL || tag_len < 4u || tag_len > 16u)) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return key->ops->aes_decrypt(ctx, key, alg, padding, iv, iv_len, aad,
                                 aad_len, input, input_len, output, output_cap,
                                 output_len, tag, tag_len);
}

/* ========================================================================
 * Certificate Signing Request (CSR)
 * ======================================================================== */

actrust_err_t actrust_crypto_csr_generate(actrust_crypto_ctx_t      ctx,
                                          actrust_crypto_key_t      key,
                                          actrust_crypto_hash_alg_t hash_alg,
                                          const char *subject, uint8_t *out,
                                          size_t out_cap, size_t *out_len)
{
    if (ctx == NULL || key == NULL || subject == NULL || out == NULL ||
        out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return key->ops->csr_generate(ctx, key, hash_alg, subject, out, out_cap,
                                  out_len);
}
