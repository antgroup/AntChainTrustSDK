// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file crypto_sw.c
 * @brief Cryptographic operations — software backend (Mbed TLS 3.x).
 */

/* C standard */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Third-party */
#include <mbedtls/aes.h>
#include <mbedtls/constant_time.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509_csr.h>

/* Common */
#include "common/common.h"

/* Crypto */
#include "crypto/crypto_internal.h"

/* Adapter */
#include "adapter/security.h"
#include "adapter/system.h"

#define CRYPTO_ERR(reason)                                                     \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_CRYPTO, (reason))

/* ========================================================================
 * Internal Constants
 * ======================================================================== */

#define SPKI_DER_MAX_SIZE 256
#define EC_DER_MAX_SIZE   256
#define AES_BLOCK_SIZE    16

/* ========================================================================
 * Internal Types
 * ======================================================================== */

typedef struct {
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    actrust_mutex_t          rng_lock;
} sw_crypto_ctx_t;

typedef struct {
    uint8_t type;
    union {
        uint8_t ec_der[EC_DER_MAX_SIZE];
        struct {
            uint8_t bits_le[2];
            uint8_t data[AES_KEY_MAX_BYTES];
        } aes;
    } u;
} key_blob_t;

/* ========================================================================
 * SW Backend Lifecycle
 * ======================================================================== */

actrust_err_t sw_crypto_init(void **out_ctx)
{
    if (out_ctx == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    sw_crypto_ctx_t *sc = ACTRUST_CALLOC(1, sizeof(*sc));
    if (sc == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }

    mbedtls_entropy_init(&sc->entropy);
    mbedtls_ctr_drbg_init(&sc->ctr_drbg);

    static const char pers[] = "actrust_crypto";
    int               ret =
        mbedtls_ctr_drbg_seed(&sc->ctr_drbg, mbedtls_entropy_func, &sc->entropy,
                              (const unsigned char *) pers, sizeof(pers) - 1);
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&sc->ctr_drbg);
        mbedtls_entropy_free(&sc->entropy);
        ACTRUST_FREE(sc);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    actrust_err_t err = actrust_mutex_create(&sc->rng_lock);
    if (err != ACTRUST_OK) {
        mbedtls_ctr_drbg_free(&sc->ctr_drbg);
        mbedtls_entropy_free(&sc->entropy);
        ACTRUST_FREE(sc);
        return err;
    }

    *out_ctx = sc;
    return ACTRUST_OK;
}

actrust_err_t sw_crypto_deinit(void *sw_ctx)
{
    if (sw_ctx == NULL) {
        return ACTRUST_OK;
    }

    sw_crypto_ctx_t *sc       = sw_ctx;
    actrust_mutex_t  rng_lock = sc->rng_lock;

    (void) actrust_mutex_destroy(rng_lock);
    mbedtls_ctr_drbg_free(&sc->ctr_drbg);
    mbedtls_entropy_free(&sc->entropy);
    mbedtls_platform_zeroize(sc, sizeof(*sc));
    ACTRUST_FREE(sc);
    return ACTRUST_OK;
}

static actrust_err_t sw_rng_lock(sw_crypto_ctx_t *sc)
{
    if (sc == NULL || sc->rng_lock == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return actrust_mutex_lock(sc->rng_lock);
}

static void sw_rng_unlock(sw_crypto_ctx_t *sc)
{
    if (sc != NULL && sc->rng_lock != NULL) {
        (void) actrust_mutex_unlock(sc->rng_lock);
    }
}

/* ========================================================================
 * Random Number Generation
 * ======================================================================== */

static actrust_err_t sw_crypto_random(actrust_crypto_ctx_t ctx, uint8_t *out,
                                      size_t out_len)
{
    if (ctx == NULL || ctx->sw_ctx == NULL || out == NULL || out_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    sw_crypto_ctx_t *sc  = (sw_crypto_ctx_t *) ctx->sw_ctx;
    actrust_err_t    err = sw_rng_lock(sc);
    if (err != ACTRUST_OK) {
        return err;
    }

    size_t total = 0u;
    while (total < out_len) {
        size_t chunk = out_len - total;
        if (chunk > MBEDTLS_CTR_DRBG_MAX_REQUEST) {
            chunk = MBEDTLS_CTR_DRBG_MAX_REQUEST;
        }

        if (mbedtls_ctr_drbg_random(&sc->ctr_drbg, out + total, chunk) != 0) {
            sw_rng_unlock(sc);
            return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
        }
        total += chunk;
    }
    sw_rng_unlock(sc);
    return ACTRUST_OK;
}

/* ========================================================================
 * Hash
 * ======================================================================== */

static mbedtls_md_type_t map_mbedtls_hash(actrust_crypto_hash_alg_t alg)
{
    switch (alg) {
        case ACTRUST_CRYPTO_HASH_SHA256:
            return MBEDTLS_MD_SHA256;
        case ACTRUST_CRYPTO_HASH_SHA384:
            return MBEDTLS_MD_SHA384;
        case ACTRUST_CRYPTO_HASH_SHA512:
            return MBEDTLS_MD_SHA512;
        default:
            return MBEDTLS_MD_NONE;
    }
}

static actrust_err_t sw_crypto_hash(actrust_crypto_ctx_t      ctx,
                                    actrust_crypto_hash_alg_t alg,
                                    const uint8_t *input, size_t input_len,
                                    uint8_t *out, size_t out_cap,
                                    size_t *out_len)
{
    (void) ctx;
    if ((input == NULL && input_len > 0u) || out == NULL || out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const mbedtls_md_type_t md_type = map_mbedtls_hash(alg);
    if (md_type == MBEDTLS_MD_NONE) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    const size_t need = crypto_hash_len_bytes(alg);

    if (out_cap < need) {
        *out_len = need;
        return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(md_type);
    if (info == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    int ret = mbedtls_md(info, input, input_len, out);
    if (ret != 0) {
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    *out_len = need;
    return ACTRUST_OK;
}

/* ========================================================================
 * Key Management
 * ======================================================================== */

static mbedtls_ecp_group_id map_mbedtls_curve(actrust_crypto_ec_curve_t curve)
{
    switch (curve) {
        case ACTRUST_CRYPTO_EC_SECP256R1:
            return MBEDTLS_ECP_DP_SECP256R1;
        case ACTRUST_CRYPTO_EC_SECP256K1:
            return MBEDTLS_ECP_DP_SECP256K1;
        default:
            return MBEDTLS_ECP_DP_NONE;
    }
}

static actrust_err_t key_serialize(actrust_crypto_key_t key, key_blob_t *blob,
                                   size_t *out_len)
{
    if (key == NULL || blob == NULL || out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    blob->type = (uint8_t) key->type;

    switch (key->type) {
        case ACTRUST_CRYPTO_KEY_EC: {
            uint8_t tmp[EC_DER_MAX_SIZE];
            int     ret =
                mbedtls_pk_write_key_der(&key->key.sw.u.pk, tmp, sizeof(tmp));
            if (ret < 0) {
                return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
            }
            size_t der_len = (size_t) ret;
            memcpy(blob->u.ec_der, tmp + sizeof(tmp) - der_len, der_len);
            *out_len = sizeof(blob->type) + der_len;
            mbedtls_platform_zeroize(tmp, sizeof(tmp));
            return ACTRUST_OK;
        }
        case ACTRUST_CRYPTO_KEY_AES: {
            size_t key_bytes       = key->key.sw.u.aes.bits / 8u;
            blob->u.aes.bits_le[0] = (uint8_t) (key->key.sw.u.aes.bits & 0xFFu);
            blob->u.aes.bits_le[1] =
                (uint8_t) ((key->key.sw.u.aes.bits >> 8) & 0xFFu);
            memcpy(blob->u.aes.data, key->key.sw.u.aes.data, key_bytes);
            *out_len =
                sizeof(blob->type) + sizeof(blob->u.aes.bits_le) + key_bytes;
            return ACTRUST_OK;
        }
        default:
            return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
}

static actrust_err_t key_deserialize(const key_blob_t *blob, size_t blob_len,
                                     sw_crypto_ctx_t      *sw_ctx,
                                     actrust_crypto_key_t *out_key)
{
    if (blob == NULL || sw_ctx == NULL || out_key == NULL ||
        blob_len < sizeof(blob->type)) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_crypto_key_t k = ACTRUST_CALLOC(1, sizeof(*k));
    if (k == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }

    size_t payload_len = blob_len - sizeof(blob->type);

    k->backend = ACTRUST_CRYPTO_BACKEND_SW;

    switch ((actrust_crypto_key_type_t) blob->type) {
        case ACTRUST_CRYPTO_KEY_EC: {
            k->type = ACTRUST_CRYPTO_KEY_EC;
            mbedtls_pk_init(&k->key.sw.u.pk);
            actrust_err_t err = sw_rng_lock(sw_ctx);
            if (err != ACTRUST_OK) {
                mbedtls_pk_free(&k->key.sw.u.pk);
                ACTRUST_FREE(k);
                return err;
            }
            int ret = mbedtls_pk_parse_key(
                &k->key.sw.u.pk, blob->u.ec_der, payload_len, NULL, 0,
                mbedtls_ctr_drbg_random, &sw_ctx->ctr_drbg);
            sw_rng_unlock(sw_ctx);
            if (ret != 0) {
                mbedtls_pk_free(&k->key.sw.u.pk);
                ACTRUST_FREE(k);
                return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
            }
            break;
        }
        case ACTRUST_CRYPTO_KEY_AES: {
            if (payload_len < sizeof(blob->u.aes.bits_le)) {
                ACTRUST_FREE(k);
                return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
            }
            k->type              = ACTRUST_CRYPTO_KEY_AES;
            k->key.sw.u.aes.bits = (size_t) blob->u.aes.bits_le[0] |
                                   ((size_t) blob->u.aes.bits_le[1] << 8);
            size_t key_bytes = k->key.sw.u.aes.bits / 8u;
            if (key_bytes > AES_KEY_MAX_BYTES ||
                payload_len != sizeof(blob->u.aes.bits_le) + key_bytes) {
                ACTRUST_FREE(k);
                return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
            }
            memcpy(k->key.sw.u.aes.data, blob->u.aes.data, key_bytes);
            break;
        }
        default:
            ACTRUST_FREE(k);
            return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *out_key = k;
    return ACTRUST_OK;
}

static actrust_err_t sw_store_key_blob(actrust_crypto_key_t key,
                                       uint32_t             key_id)
{
    if (key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    key_blob_t blob;
    size_t     blob_len = 0;

    actrust_err_t err = key_serialize(key, &blob, &blob_len);
    if (err != ACTRUST_OK) {
        return err;
    }

    err = actrust_sec_store_write(keyid_to_slotid(key_id),
                                  (const uint8_t *) &blob, blob_len);
    mbedtls_platform_zeroize(&blob, sizeof(blob));
    return err;
}

static actrust_err_t sw_crypto_key_open(actrust_crypto_ctx_t  ctx,
                                        uint32_t              key_id,
                                        actrust_crypto_key_t *out_key)
{
    if (ctx == NULL || ctx->sw_ctx == NULL || out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    key_blob_t blob;
    size_t     blob_len = 0;
    *out_key            = NULL;

    actrust_err_t err = actrust_sec_store_read(
        keyid_to_slotid(key_id), (uint8_t *) &blob, sizeof(blob), &blob_len);
    if (err != ACTRUST_OK) {
        return err;
    }

    err = key_deserialize(&blob, blob_len, (sw_crypto_ctx_t *) ctx->sw_ctx,
                          out_key);
    mbedtls_platform_zeroize(&blob, sizeof(blob));
    return err;
}

static actrust_err_t sw_crypto_key_close(actrust_crypto_ctx_t  ctx,
                                         actrust_crypto_key_t *key)
{
    if (ctx == NULL || key == NULL || *key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if ((*key)->type == ACTRUST_CRYPTO_KEY_EC) {
        mbedtls_pk_free(&(*key)->key.sw.u.pk);
    }
    mbedtls_platform_zeroize(*key, sizeof(**key));
    ACTRUST_FREE(*key);
    *key = NULL;
    return ACTRUST_OK;
}

static actrust_err_t sw_crypto_key_destroy(actrust_crypto_ctx_t ctx,
                                           uint32_t             key_id)
{
    if (ctx == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return actrust_sec_store_delete(keyid_to_slotid(key_id));
}

static actrust_err_t sw_crypto_key_migrate(actrust_crypto_ctx_t ctx,
                                           uint32_t             source_id,
                                           uint32_t             target_id)
{
    if (ctx == NULL || source_id == target_id) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_crypto_key_t source_key = NULL;
    actrust_err_t        err = sw_crypto_key_open(ctx, source_id, &source_key);
    if (err != ACTRUST_OK) {
        return err;
    }

    key_blob_t blob;
    size_t     blob_len = 0u;
    memset(&blob, 0, sizeof(blob));
    err = key_serialize(source_key, &blob, &blob_len);
    if (err == ACTRUST_OK) {
        err = actrust_sec_store_write(keyid_to_slotid(target_id),
                                      (const uint8_t *) &blob, blob_len);
    }
    mbedtls_platform_zeroize(&blob, sizeof(blob));
    (void) sw_crypto_key_close(ctx, &source_key);
    return err;
}

static actrust_err_t sw_crypto_key_generate_ec_impl(
    actrust_crypto_ctx_t ctx, actrust_crypto_ec_curve_t curve, uint32_t key_id,
    actrust_crypto_key_t *out_key)
{
    if (ctx == NULL || ctx->sw_ctx == NULL || out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *out_key = NULL;

    mbedtls_ecp_group_id grp = map_mbedtls_curve(curve);
    if (grp == MBEDTLS_ECP_DP_NONE) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    actrust_crypto_key_t k = ACTRUST_CALLOC(1, sizeof(*k));
    if (k == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }

    k->backend = ACTRUST_CRYPTO_BACKEND_SW;
    k->type    = ACTRUST_CRYPTO_KEY_EC;
    mbedtls_pk_init(&k->key.sw.u.pk);

    int ret = mbedtls_pk_setup(&k->key.sw.u.pk,
                               mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (ret != 0) {
        mbedtls_pk_free(&k->key.sw.u.pk);
        ACTRUST_FREE(k);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    sw_crypto_ctx_t *sc  = (sw_crypto_ctx_t *) ctx->sw_ctx;
    actrust_err_t    err = sw_rng_lock(sc);
    if (err != ACTRUST_OK) {
        mbedtls_pk_free(&k->key.sw.u.pk);
        ACTRUST_FREE(k);
        return err;
    }

    ret = mbedtls_ecp_gen_key(grp, mbedtls_pk_ec(k->key.sw.u.pk),
                              mbedtls_ctr_drbg_random, &sc->ctr_drbg);
    sw_rng_unlock(sc);
    if (ret != 0) {
        mbedtls_pk_free(&k->key.sw.u.pk);
        ACTRUST_FREE(k);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    err = sw_store_key_blob(k, key_id);
    if (err != ACTRUST_OK) {
        (void) sw_crypto_key_close(ctx, &k);
        return err;
    }

    *out_key = k;
    return ACTRUST_OK;
}

static actrust_err_t sw_crypto_key_generate_sym_impl(
    actrust_crypto_ctx_t ctx, size_t key_bits, uint32_t key_id,
    actrust_crypto_key_t *out_key)
{
    if (ctx == NULL || ctx->sw_ctx == NULL || out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *out_key = NULL;

    actrust_crypto_key_t k = ACTRUST_CALLOC(1, sizeof(*k));
    if (k == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }

    k->backend           = ACTRUST_CRYPTO_BACKEND_SW;
    k->type              = ACTRUST_CRYPTO_KEY_AES;
    k->key.sw.u.aes.bits = key_bits;

    sw_crypto_ctx_t *sc  = (sw_crypto_ctx_t *) ctx->sw_ctx;
    actrust_err_t    err = sw_rng_lock(sc);
    if (err != ACTRUST_OK) {
        mbedtls_platform_zeroize(k, sizeof(*k));
        ACTRUST_FREE(k);
        return err;
    }

    int ret = mbedtls_ctr_drbg_random(&sc->ctr_drbg, k->key.sw.u.aes.data,
                                      key_bits / 8u);
    sw_rng_unlock(sc);
    if (ret != 0) {
        mbedtls_platform_zeroize(k, sizeof(*k));
        ACTRUST_FREE(k);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    err = sw_store_key_blob(k, key_id);
    if (err != ACTRUST_OK) {
        (void) sw_crypto_key_close(ctx, &k);
        return err;
    }

    *out_key = k;
    return ACTRUST_OK;
}

static actrust_err_t sw_crypto_key_generate(
    actrust_crypto_ctx_t ctx, uint32_t key_id,
    const actrust_crypto_key_gen_params_t *params,
    actrust_crypto_key_t                  *out_key)
{
    if (ctx == NULL || ctx->sw_ctx == NULL || params == NULL ||
        out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    switch (params->type) {
        case ACTRUST_CRYPTO_KEY_EC:
            return sw_crypto_key_generate_ec_impl(ctx, params->spec.curve,
                                                  key_id, out_key);
        case ACTRUST_CRYPTO_KEY_AES:
            return sw_crypto_key_generate_sym_impl(ctx, params->spec.key_bits,
                                                   key_id, out_key);
        default:
            return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
}

static actrust_err_t sw_crypto_key_import(actrust_crypto_ctx_t    ctx,
                                          uint32_t                key_id,
                                          actrust_crypto_format_t format,
                                          const uint8_t *key, size_t key_len,
                                          actrust_crypto_key_t *out_key)
{
    if (ctx == NULL || ctx->sw_ctx == NULL || key == NULL || key_len == 0u ||
        out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *out_key = NULL;

    const actrust_crypto_key_desc_t *desc = actrust_crypto_key_lookup(key_id);
    if (desc == NULL || desc->type != ACTRUST_CRYPTO_KEY_EC) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    key_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.type = (uint8_t) ACTRUST_CRYPTO_KEY_EC;

    size_t der_len = 0u;
    if (format == ACTRUST_CRYPTO_FORMAT_PRIVATE_PEM) {
        actrust_err_t err = actrust_crypto_pem_to_der(
            ACTRUST_CRYPTO_PEM_OBJECT_PRIVATE_KEY, (const char *) key, key_len,
            blob.u.ec_der, sizeof(blob.u.ec_der), &der_len);
        if (err != ACTRUST_OK) {
            mbedtls_platform_zeroize(&blob, sizeof(blob));
            return err;
        }
    } else if (format == ACTRUST_CRYPTO_FORMAT_PRIVATE_DER) {
        if (key_len > sizeof(blob.u.ec_der)) {
            return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
        }
        memcpy(blob.u.ec_der, key, key_len);
        der_len = key_len;
    } else {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_crypto_key_t imported_key = NULL;
    actrust_err_t        err =
        key_deserialize(&blob, sizeof(blob.type) + der_len,
                        (sw_crypto_ctx_t *) ctx->sw_ctx, &imported_key);
    mbedtls_platform_zeroize(&blob, sizeof(blob));
    if (err != ACTRUST_OK) {
        return err;
    }

    mbedtls_ecp_keypair *ec = mbedtls_pk_ec(imported_key->key.sw.u.pk);
    if (ec == NULL || mbedtls_ecp_keypair_get_group_id(ec) !=
                          map_mbedtls_curve(desc->spec.curve)) {
        (void) sw_crypto_key_close(ctx, &imported_key);
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    err = sw_store_key_blob(imported_key, key_id);
    if (err != ACTRUST_OK) {
        (void) sw_crypto_key_close(ctx, &imported_key);
        return err;
    }

    *out_key = imported_key;
    return ACTRUST_OK;
}

static actrust_err_t sw_crypto_key_export_public(actrust_crypto_ctx_t ctx,
                                                 actrust_crypto_key_t key,
                                                 uint8_t *out, size_t out_cap,
                                                 size_t *out_len)
{
    (void) ctx;
    if (key == NULL || out == NULL || out_len == NULL ||
        key->type != ACTRUST_CRYPTO_KEY_EC) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    uint8_t tmp[SPKI_DER_MAX_SIZE];
    int ret = mbedtls_pk_write_pubkey_der(&key->key.sw.u.pk, tmp, sizeof(tmp));
    if (ret < 0) {
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    size_t len = (size_t) ret;
    if (len > out_cap) {
        *out_len = len;
        mbedtls_platform_zeroize(tmp, sizeof(tmp));
        return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    /* mbedtls_pk_write_pubkey_der writes from the end of the buffer */
    memcpy(out, tmp + sizeof(tmp) - len, len);
    *out_len = len;
    mbedtls_platform_zeroize(tmp, sizeof(tmp));
    return ACTRUST_OK;
}

/* ========================================================================
 * ECDSA
 * ======================================================================== */

static actrust_err_t sw_crypto_ecdsa_sign(actrust_crypto_ctx_t      ctx,
                                          actrust_crypto_key_t      key,
                                          actrust_crypto_hash_alg_t hash_alg,
                                          actrust_crypto_input_t    input_type,
                                          const uint8_t *msg, size_t msg_len,
                                          uint8_t *sig, size_t sig_cap,
                                          size_t *sig_len)
{
    if (ctx == NULL || ctx->sw_ctx == NULL || key == NULL || msg == NULL ||
        sig == NULL || sig_len == NULL || sig_cap == 0u ||
        key->type != ACTRUST_CRYPTO_KEY_EC) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mbedtls_md_type_t md = map_mbedtls_hash(hash_alg);
    if (md == MBEDTLS_MD_NONE) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    if (input_type != ACTRUST_CRYPTO_INPUT_DIGEST) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const size_t expected_hash_len = crypto_hash_len_bytes(hash_alg);
    if (msg_len != expected_hash_len) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    sw_crypto_ctx_t *sc  = (sw_crypto_ctx_t *) ctx->sw_ctx;
    actrust_err_t    err = sw_rng_lock(sc);
    if (err != ACTRUST_OK) {
        return err;
    }

    int ret = mbedtls_pk_sign(&key->key.sw.u.pk, md, msg, msg_len, sig, sig_cap,
                              sig_len, mbedtls_ctr_drbg_random, &sc->ctr_drbg);
    sw_rng_unlock(sc);
    if (ret != 0) {
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    return ACTRUST_OK;
}

static actrust_err_t sw_crypto_ecdsa_verify(actrust_crypto_ctx_t      ctx,
                                            actrust_crypto_key_t      key,
                                            actrust_crypto_hash_alg_t hash_alg,
                                            actrust_crypto_input_t input_type,
                                            const uint8_t *msg, size_t msg_len,
                                            const uint8_t *sig, size_t sig_len)
{
    if (ctx == NULL || key == NULL || msg == NULL || sig == NULL ||
        sig_len == 0u || key->type != ACTRUST_CRYPTO_KEY_EC) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mbedtls_md_type_t md = map_mbedtls_hash(hash_alg);
    if (md == MBEDTLS_MD_NONE) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    if (input_type != ACTRUST_CRYPTO_INPUT_DIGEST) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const size_t expected_hash_len = crypto_hash_len_bytes(hash_alg);
    if (msg_len != expected_hash_len) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    int ret =
        mbedtls_pk_verify(&key->key.sw.u.pk, md, msg, msg_len, sig, sig_len);
    if (ret != 0) {
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * AES
 * ======================================================================== */

static actrust_err_t aes_gcm_encrypt(const struct actrust_crypto_key *key,
                                     const uint8_t *iv, size_t iv_len,
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *in, size_t in_len,
                                     uint8_t *out, uint8_t *tag, size_t tag_len)
{
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret =
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key->key.sw.u.aes.data,
                           (unsigned int) key->key.sw.u.aes.bits);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    ret =
        mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, in_len, iv, iv_len,
                                  aad, aad_len, in, out, tag_len, tag);
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        mbedtls_platform_zeroize(out, in_len);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    return ACTRUST_OK;
}

static actrust_err_t aes_gcm_decrypt(const struct actrust_crypto_key *key,
                                     const uint8_t *iv, size_t iv_len,
                                     const uint8_t *aad, size_t aad_len,
                                     const uint8_t *in, size_t in_len,
                                     uint8_t *out, const uint8_t *tag,
                                     size_t tag_len)
{
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret =
        mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key->key.sw.u.aes.data,
                           (unsigned int) key->key.sw.u.aes.bits);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    ret = mbedtls_gcm_auth_decrypt(&gcm, in_len, iv, iv_len, aad, aad_len, tag,
                                   tag_len, in, out);
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        mbedtls_platform_zeroize(out, in_len);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    return ACTRUST_OK;
}

static actrust_err_t aes_cbc_crypt(const struct actrust_crypto_key *key,
                                   int mode, const uint8_t *iv, size_t iv_len,
                                   const uint8_t *in, size_t in_len,
                                   uint8_t *out)
{
    if (iv_len != AES_BLOCK_SIZE || (in_len % AES_BLOCK_SIZE) != 0) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    int ret;
    if (mode == MBEDTLS_AES_ENCRYPT) {
        ret = mbedtls_aes_setkey_enc(&aes, key->key.sw.u.aes.data,
                                     (unsigned int) key->key.sw.u.aes.bits);
    } else if (mode == MBEDTLS_AES_DECRYPT) {
        ret = mbedtls_aes_setkey_dec(&aes, key->key.sw.u.aes.data,
                                     (unsigned int) key->key.sw.u.aes.bits);
    } else {
        mbedtls_aes_free(&aes);
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    uint8_t iv_tmp[AES_BLOCK_SIZE];
    memcpy(iv_tmp, iv, AES_BLOCK_SIZE);

    ret = mbedtls_aes_crypt_cbc(&aes, mode, in_len, iv_tmp, in, out);
    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(iv_tmp, sizeof(iv_tmp));

    if (ret != 0) {
        mbedtls_platform_zeroize(out, in_len);
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    return ACTRUST_OK;
}

static actrust_err_t sw_crypto_aes_encrypt(
    actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
    actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
    const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
    const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap,
    size_t *output_len, uint8_t *tag, size_t *tag_len)
{
    (void) ctx;

    if (key == NULL || iv == NULL || input == NULL || output == NULL ||
        output_len == NULL || (aad == NULL && aad_len > 0u) ||
        key->type != ACTRUST_CRYPTO_KEY_AES) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (alg != ACTRUST_CRYPTO_AES_GCM && alg != ACTRUST_CRYPTO_AES_CBC) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    if (alg == ACTRUST_CRYPTO_AES_GCM &&
        (tag == NULL || tag_len == NULL || *tag_len < 4u || *tag_len > 16u)) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    switch (alg) {
        case ACTRUST_CRYPTO_AES_GCM:
            if (output_cap < input_len) {
                *output_len = input_len;
                return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
            }
            {
                actrust_err_t err =
                    aes_gcm_encrypt(key, iv, iv_len, aad, aad_len, input,
                                    input_len, output, tag, *tag_len);
                if (err == ACTRUST_OK) {
                    *output_len = input_len;
                }
                return err;
            }

        case ACTRUST_CRYPTO_AES_CBC: {
            /* Compute padded length */
            size_t  padded_len;
            uint8_t pad_val;

            switch (padding) {
                case ACTRUST_CRYPTO_PAD_NONE:
                    if ((input_len % AES_BLOCK_SIZE) != 0u) {
                        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
                    }
                    padded_len = input_len;
                    pad_val    = 0u;
                    break;
                case ACTRUST_CRYPTO_PAD_PKCS7:
                    pad_val = (uint8_t) (AES_BLOCK_SIZE -
                                         (input_len % AES_BLOCK_SIZE));
                    if (input_len > SIZE_MAX - (size_t) pad_val) {
                        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
                    }
                    padded_len = input_len + pad_val;
                    break;
                case ACTRUST_CRYPTO_PAD_ZEROS:
                    if (input_len > SIZE_MAX - AES_BLOCK_SIZE + 1u) {
                        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
                    }
                    padded_len = (input_len + AES_BLOCK_SIZE - 1u) &
                                 ~((size_t) AES_BLOCK_SIZE - 1u);
                    if (padded_len == 0u) {
                        padded_len = AES_BLOCK_SIZE;
                    }
                    pad_val = 0u;
                    break;
                default:
                    return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
            }

            if (output_cap < padded_len) {
                *output_len = padded_len;
                return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
            }

            /* Build padded input in the output buffer */
            memcpy(output, input, input_len);
            if (padded_len > input_len) {
                memset(output + input_len, pad_val, padded_len - input_len);
            }

            actrust_err_t err =
                aes_cbc_crypt(key, MBEDTLS_AES_ENCRYPT, iv, iv_len, output,
                              padded_len, output);
            if (err == ACTRUST_OK) {
                *output_len = padded_len;
            }
            return err;
        }

        default:
            return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }
}

static actrust_err_t sw_crypto_aes_decrypt(
    actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
    actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
    const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
    const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap,
    size_t *output_len, const uint8_t *tag, size_t tag_len)
{
    (void) ctx;

    if (key == NULL || iv == NULL || input == NULL || output == NULL ||
        output_len == NULL || (aad == NULL && aad_len > 0u) ||
        key->type != ACTRUST_CRYPTO_KEY_AES) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (alg != ACTRUST_CRYPTO_AES_GCM && alg != ACTRUST_CRYPTO_AES_CBC) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    if (alg == ACTRUST_CRYPTO_AES_GCM &&
        (tag == NULL || tag_len < 4u || tag_len > 16u)) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (output_cap < input_len) {
        *output_len = input_len;
        return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    switch (alg) {
        case ACTRUST_CRYPTO_AES_GCM: {
            actrust_err_t err =
                aes_gcm_decrypt(key, iv, iv_len, aad, aad_len, input, input_len,
                                output, tag, tag_len);
            if (err == ACTRUST_OK) {
                *output_len = input_len;
            }
            return err;
        }

        case ACTRUST_CRYPTO_AES_CBC: {
            if ((input_len % AES_BLOCK_SIZE) != 0u || input_len == 0u) {
                return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
            }

            actrust_err_t err = aes_cbc_crypt(key, MBEDTLS_AES_DECRYPT, iv,
                                              iv_len, input, input_len, output);
            if (err != ACTRUST_OK) {
                return err;
            }

            /* Strip padding */
            switch (padding) {
                case ACTRUST_CRYPTO_PAD_NONE:
                    *output_len = input_len;
                    break;
                case ACTRUST_CRYPTO_PAD_PKCS7: {
                    uint8_t pad_val = output[input_len - 1u];
                    if (pad_val == 0u || pad_val > AES_BLOCK_SIZE) {
                        mbedtls_platform_zeroize(output, input_len);
                        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
                    }
                    uint8_t expected[AES_BLOCK_SIZE];
                    memset(expected, pad_val, sizeof(expected));
                    if (mbedtls_ct_memcmp(output + input_len - pad_val,
                                          expected, pad_val) != 0) {
                        mbedtls_platform_zeroize(output, input_len);
                        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
                    }
                    *output_len = input_len - pad_val;
                    break;
                }
                case ACTRUST_CRYPTO_PAD_ZEROS: {
                    size_t len = input_len;
                    while (len > 0u && output[len - 1u] == 0u) {
                        len--;
                    }
                    *output_len = len;
                    break;
                }
                default:
                    mbedtls_platform_zeroize(output, input_len);
                    return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
            }
            return ACTRUST_OK;
        }

        default:
            return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }
}

/* ========================================================================
 * Certificate Signing Request (CSR)
 * ======================================================================== */

static actrust_err_t sw_crypto_csr_generate(actrust_crypto_ctx_t      ctx,
                                            actrust_crypto_key_t      key,
                                            actrust_crypto_hash_alg_t hash_alg,
                                            const char *subject, uint8_t *out,
                                            size_t out_cap, size_t *out_len)
{
    if (ctx == NULL || ctx->sw_ctx == NULL || key == NULL || subject == NULL ||
        out == NULL || out_len == NULL || key->type != ACTRUST_CRYPTO_KEY_EC) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (out_cap == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    mbedtls_md_type_t md = map_mbedtls_hash(hash_alg);
    if (md == MBEDTLS_MD_NONE) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mbedtls_x509write_csr csr;
    mbedtls_x509write_csr_init(&csr);
    mbedtls_x509write_csr_set_md_alg(&csr, md);
    mbedtls_x509write_csr_set_key(&csr, &key->key.sw.u.pk);

    int ret = mbedtls_x509write_csr_set_subject_name(&csr, subject);
    if (ret != 0) {
        mbedtls_x509write_csr_free(&csr);
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* mbedtls_x509write_csr_der writes from the end of the buffer */
    uint8_t *tmp = (uint8_t *) ACTRUST_CALLOC(1, out_cap);
    if (tmp == NULL) {
        mbedtls_x509write_csr_free(&csr);
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }

    sw_crypto_ctx_t *sc  = (sw_crypto_ctx_t *) ctx->sw_ctx;
    actrust_err_t    err = sw_rng_lock(sc);
    if (err != ACTRUST_OK) {
        mbedtls_x509write_csr_free(&csr);
        mbedtls_platform_zeroize(tmp, out_cap);
        ACTRUST_FREE(tmp);
        return err;
    }

    ret = mbedtls_x509write_csr_der(&csr, tmp, out_cap, mbedtls_ctr_drbg_random,
                                    &sc->ctr_drbg);
    sw_rng_unlock(sc);
    mbedtls_x509write_csr_free(&csr);

    if (ret < 0) {
        mbedtls_platform_zeroize(tmp, out_cap);
        ACTRUST_FREE(tmp);
        if (ret == MBEDTLS_ERR_ASN1_BUF_TOO_SMALL) {
            return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
        }
        return CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    size_t len = (size_t) ret;
    memcpy(out, tmp + out_cap - len, len);
    *out_len = len;

    mbedtls_platform_zeroize(tmp, out_cap);
    ACTRUST_FREE(tmp);
    return ACTRUST_OK;
}

/* ========================================================================
 * Backend Ops Table
 * ======================================================================== */

const crypto_backend_ops_t sw_crypto_ops = {
    .random            = sw_crypto_random,
    .hash              = sw_crypto_hash,
    .key_open          = sw_crypto_key_open,
    .key_close         = sw_crypto_key_close,
    .key_generate      = sw_crypto_key_generate,
    .key_import        = sw_crypto_key_import,
    .key_destroy       = sw_crypto_key_destroy,
    .key_migrate       = sw_crypto_key_migrate,
    .key_export_public = sw_crypto_key_export_public,
    .ecdsa_sign        = sw_crypto_ecdsa_sign,
    .ecdsa_verify      = sw_crypto_ecdsa_verify,
    .aes_encrypt       = sw_crypto_aes_encrypt,
    .aes_decrypt       = sw_crypto_aes_decrypt,
    .csr_generate      = sw_crypto_csr_generate,
};
