// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file crypto_hw.c
 * @brief Cryptographic operations — hardware backend.
 */

/* C standard */
#include <stdlib.h>
#include <string.h>

/* Third-party */
#include <mbedtls/asn1.h>
#include <mbedtls/asn1write.h>
#include <mbedtls/md.h>
#include <mbedtls/oid.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509.h>

/* Common */
#include "common/common.h"

/* Crypto */
#include "crypto/crypto_internal.h"

/* Adapter */
#include "adapter/security.h"

/* mbedtls library-internal symbol in library/x509_internal.h. */
extern int mbedtls_x509_write_names(unsigned char **p, unsigned char *start,
                                    mbedtls_asn1_named_data *first);

#define CRYPTO_ERR(reason)                                                     \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_CRYPTO, (reason))

/* ========================================================================
 * Helpers
 * ======================================================================== */

static actrust_err_t map_ec_key_type(actrust_crypto_ec_curve_t curve,
                                     actrust_sec_key_type_t   *out_type)
{
    if (out_type == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    switch (curve) {
        case ACTRUST_CRYPTO_EC_SECP256R1:
            *out_type = ACTRUST_SEC_KEY_EC_P256;
            return ACTRUST_OK;
        case ACTRUST_CRYPTO_EC_SECP256K1:
            *out_type = ACTRUST_SEC_KEY_EC_SECP256K1;
            return ACTRUST_OK;
        default:
            return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
}

static actrust_err_t map_aes_key_type(size_t                  key_bits,
                                      actrust_sec_key_type_t *out_type)
{
    if (out_type == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    switch (key_bits) {
        case 128u:
            *out_type = ACTRUST_SEC_KEY_AES_128;
            return ACTRUST_OK;
        case 256u:
            *out_type = ACTRUST_SEC_KEY_AES_256;
            return ACTRUST_OK;
        default:
            return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
}

static actrust_sec_hash_alg_t map_hash_alg(actrust_crypto_hash_alg_t alg)
{
    switch (alg) {
        case ACTRUST_CRYPTO_HASH_SHA256:
            return ACTRUST_SEC_HASH_SHA256;
        case ACTRUST_CRYPTO_HASH_SHA384:
            return ACTRUST_SEC_HASH_SHA384;
        case ACTRUST_CRYPTO_HASH_SHA512:
            return ACTRUST_SEC_HASH_SHA512;
        default:
            return (actrust_sec_hash_alg_t) 0;
    }
}

static actrust_sec_sym_alg_t map_sym_alg(actrust_crypto_sym_alg_t alg)
{
    switch (alg) {
        case ACTRUST_CRYPTO_AES_GCM:
            return ACTRUST_SEC_AES_GCM;
        case ACTRUST_CRYPTO_AES_CBC:
            return ACTRUST_SEC_AES_CBC;
        default:
            return (actrust_sec_sym_alg_t) 0;
    }
}

/* ========================================================================
 * HW Backend Lifecycle
 * ======================================================================== */

actrust_err_t hw_crypto_init(void **out_ctx)
{
    (void) out_ctx;
    return ACTRUST_OK;
}

actrust_err_t hw_crypto_deinit(void *hw_ctx)
{
    (void) hw_ctx;
    return ACTRUST_OK;
}

/* ========================================================================
 * Random Number Generation
 * ======================================================================== */

static actrust_err_t hw_crypto_random(actrust_crypto_ctx_t ctx, uint8_t *out,
                                      size_t out_len)
{
    if (ctx == NULL || out == NULL || out_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return actrust_sec_random(out, out_len);
}

/* ========================================================================
 * Hash
 * ======================================================================== */

static actrust_err_t hw_crypto_hash(actrust_crypto_ctx_t      ctx,
                                    actrust_crypto_hash_alg_t alg,
                                    const uint8_t *input, size_t input_len,
                                    uint8_t *out, size_t out_cap,
                                    size_t *out_len)
{
    if (ctx == NULL || (input == NULL && input_len > 0u) || out == NULL ||
        out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_sec_hash_alg_t sec_alg = map_hash_alg(alg);
    if (sec_alg == (actrust_sec_hash_alg_t) 0) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    return actrust_sec_hash(sec_alg, input, input_len, out, out_cap, out_len);
}

/* ========================================================================
 * Key Management
 * ======================================================================== */

static actrust_err_t hw_crypto_key_open(actrust_crypto_ctx_t  ctx,
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

    actrust_crypto_key_t key = ACTRUST_CALLOC(1, sizeof(*key));
    if (key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }

    key->backend        = ACTRUST_CRYPTO_BACKEND_HW;
    key->type           = desc->type;
    key->key.hw.slot_id = keyid_to_slotid(key_id);
    *out_key            = key;
    return ACTRUST_OK;
}

static actrust_err_t hw_crypto_key_close(actrust_crypto_ctx_t  ctx,
                                         actrust_crypto_key_t *key)
{
    if (ctx == NULL || key == NULL || *key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mbedtls_platform_zeroize(*key, sizeof(**key));
    ACTRUST_FREE(*key);
    *key = NULL;
    return ACTRUST_OK;
}

static actrust_err_t hw_crypto_key_generate(
    actrust_crypto_ctx_t ctx, uint32_t key_id,
    const actrust_crypto_key_gen_params_t *params,
    actrust_crypto_key_t                  *out_key)
{
    if (ctx == NULL || params == NULL || out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *out_key = NULL;
    actrust_sec_key_type_t sec_type;
    actrust_err_t          err = ACTRUST_OK;

    switch (params->type) {
        case ACTRUST_CRYPTO_KEY_EC:
            err = map_ec_key_type(params->spec.curve, &sec_type);
            break;
        case ACTRUST_CRYPTO_KEY_AES:
            err = map_aes_key_type(params->spec.key_bits, &sec_type);
            break;
        default:
            return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (err != ACTRUST_OK) {
        return err;
    }

    actrust_sec_slot_t slot_id = keyid_to_slotid(key_id);
    err                        = actrust_sec_key_generate(slot_id, sec_type);
    if (err != ACTRUST_OK) {
        return err;
    }

    actrust_crypto_key_t key = ACTRUST_CALLOC(1, sizeof(*key));
    if (key == NULL) {
        (void) actrust_sec_key_delete(slot_id);
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }

    key->backend        = ACTRUST_CRYPTO_BACKEND_HW;
    key->type           = params->type;
    key->key.hw.slot_id = slot_id;
    *out_key            = key;
    return ACTRUST_OK;
}

static actrust_err_t hw_crypto_key_destroy(actrust_crypto_ctx_t ctx,
                                           uint32_t             key_id)
{
    if (ctx == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return actrust_sec_key_delete(keyid_to_slotid(key_id));
}

static actrust_err_t hw_crypto_key_migrate(actrust_crypto_ctx_t ctx,
                                           uint32_t             source_id,
                                           uint32_t             target_id)
{
    (void) ctx;
    (void) source_id;
    (void) target_id;
    return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
}

static actrust_err_t hw_crypto_key_import(actrust_crypto_ctx_t    ctx,
                                          uint32_t                key_id,
                                          actrust_crypto_format_t format,
                                          const uint8_t *key, size_t key_len,
                                          actrust_crypto_key_t *out_key)
{
    (void) ctx;
    (void) key_id;
    (void) format;
    (void) key;
    (void) key_len;

    if (out_key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *out_key = NULL;
    return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
}

static actrust_err_t hw_crypto_key_export_public(actrust_crypto_ctx_t ctx,
                                                 actrust_crypto_key_t key,
                                                 uint8_t *out, size_t out_cap,
                                                 size_t *out_len)
{
    if (ctx == NULL || key == NULL || out == NULL || out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return actrust_sec_key_get_public(key->key.hw.slot_id, out, out_cap,
                                      out_len);
}

/* ========================================================================
 * ECDSA (DIGEST input only — dispatch layer pre-hashes MESSAGE)
 * ======================================================================== */

static actrust_err_t hw_crypto_ecdsa_sign(actrust_crypto_ctx_t      ctx,
                                          actrust_crypto_key_t      key,
                                          actrust_crypto_hash_alg_t hash_alg,
                                          actrust_crypto_input_t    input_type,
                                          const uint8_t *msg, size_t msg_len,
                                          uint8_t *sig, size_t sig_cap,
                                          size_t *sig_len)
{
    if (ctx == NULL || key == NULL || msg == NULL || sig == NULL ||
        sig_len == NULL || sig_cap == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* HW backend only accepts pre-computed digests */
    if (input_type != ACTRUST_CRYPTO_INPUT_DIGEST) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const size_t expected_digest_len = crypto_hash_len_bytes(hash_alg);
    if (expected_digest_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }
    if (msg_len != expected_digest_len) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return actrust_sec_ecdsa_sign(key->key.hw.slot_id, msg, msg_len, sig,
                                  sig_cap, sig_len);
}

static actrust_err_t hw_crypto_ecdsa_verify(actrust_crypto_ctx_t      ctx,
                                            actrust_crypto_key_t      key,
                                            actrust_crypto_hash_alg_t hash_alg,
                                            actrust_crypto_input_t input_type,
                                            const uint8_t *msg, size_t msg_len,
                                            const uint8_t *sig, size_t sig_len)
{
    if (ctx == NULL || key == NULL || msg == NULL || sig == NULL ||
        sig_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* HW backend only accepts pre-computed digests */
    if (input_type != ACTRUST_CRYPTO_INPUT_DIGEST) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const size_t expected_digest_len = crypto_hash_len_bytes(hash_alg);
    if (expected_digest_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }
    if (msg_len != expected_digest_len) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return actrust_sec_ecdsa_verify(key->key.hw.slot_id, msg, msg_len, sig,
                                    sig_len);
}

/* ========================================================================
 * Symmetric Encryption (AES) — via adapter
 * ======================================================================== */

static actrust_sec_padding_t map_padding(actrust_crypto_padding_t pad)
{
    switch (pad) {
        case ACTRUST_CRYPTO_PAD_PKCS7:
            return ACTRUST_SEC_PAD_PKCS7;
        case ACTRUST_CRYPTO_PAD_ZEROS:
            return ACTRUST_SEC_PAD_ZEROS;
        default:
            return ACTRUST_SEC_PAD_NONE;
    }
}

static actrust_err_t hw_crypto_aes_encrypt(
    actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
    actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
    const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
    const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap,
    size_t *output_len, uint8_t *tag, size_t *tag_len)
{
    if (ctx == NULL || key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_sec_sym_alg_t sec_alg = map_sym_alg(alg);
    if (sec_alg == (actrust_sec_sym_alg_t) 0) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    return actrust_sec_aes_encrypt(key->key.hw.slot_id, sec_alg,
                                   map_padding(padding), iv, iv_len, aad,
                                   aad_len, input, input_len, output,
                                   output_cap, output_len, tag, tag_len);
}

static actrust_err_t hw_crypto_aes_decrypt(
    actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
    actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
    const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
    const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap,
    size_t *output_len, const uint8_t *tag, size_t tag_len)
{
    if (ctx == NULL || key == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_sec_sym_alg_t sec_alg = map_sym_alg(alg);
    if (sec_alg == (actrust_sec_sym_alg_t) 0) {
        return CRYPTO_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    return actrust_sec_aes_decrypt(key->key.hw.slot_id, sec_alg,
                                   map_padding(padding), iv, iv_len, aad,
                                   aad_len, input, input_len, output,
                                   output_cap, output_len, tag, tag_len);
}

/* ========================================================================
 * Certificate Signing Request (CSR)
 *
 * CSR assembly is pure software work.  The TEE provides:
 *   - actrust_sec_key_get_public  → SubjectPublicKeyInfo DER
 *   - actrust_sec_ecdsa_sign            → ECDSA signature over hash
 * This function uses mbedtls ASN.1 write utilities to construct the
 * CertificationRequestInfo, hashes it, signs via the TEE, and wraps
 * everything into a final CertificationRequest DER.
 * ======================================================================== */

#define HW_CSR_SPKI_MAX 256u  /* SubjectPublicKeyInfo DER */
#define HW_CSR_SIG_MAX  128u  /* ECDSA DER signature */
#define HW_CSR_CRI_MAX  1024u /* CertificationRequestInfo scratch */

static actrust_err_t map_csr_sig_oid(actrust_crypto_hash_alg_t alg,
                                     mbedtls_md_type_t        *md_type,
                                     const char **oid, size_t *oid_len)
{
    switch (alg) {
        case ACTRUST_CRYPTO_HASH_SHA256:
            *md_type = MBEDTLS_MD_SHA256;
            *oid     = MBEDTLS_OID_ECDSA_SHA256;
            *oid_len = MBEDTLS_OID_SIZE(MBEDTLS_OID_ECDSA_SHA256);
            return ACTRUST_OK;
        case ACTRUST_CRYPTO_HASH_SHA384:
            *md_type = MBEDTLS_MD_SHA384;
            *oid     = MBEDTLS_OID_ECDSA_SHA384;
            *oid_len = MBEDTLS_OID_SIZE(MBEDTLS_OID_ECDSA_SHA384);
            return ACTRUST_OK;
        case ACTRUST_CRYPTO_HASH_SHA512:
            *md_type = MBEDTLS_MD_SHA512;
            *oid     = MBEDTLS_OID_ECDSA_SHA512;
            *oid_len = MBEDTLS_OID_SIZE(MBEDTLS_OID_ECDSA_SHA512);
            return ACTRUST_OK;
        default:
            return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
}

static actrust_err_t hw_crypto_csr_generate(actrust_crypto_ctx_t      ctx,
                                            actrust_crypto_key_t      key,
                                            actrust_crypto_hash_alg_t hash_alg,
                                            const char *subject, uint8_t *out,
                                            size_t out_cap, size_t *out_len)
{
    if (ctx == NULL || key == NULL || subject == NULL || out == NULL ||
        out_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (out_cap == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    mbedtls_md_type_t md_type;
    const char       *sig_oid     = NULL;
    size_t            sig_oid_len = 0;
    actrust_err_t     err =
        map_csr_sig_oid(hash_alg, &md_type, &sig_oid, &sig_oid_len);
    if (err != ACTRUST_OK) {
        return err;
    }

    actrust_err_t            result       = CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
    int                      ret          = 0;
    mbedtls_asn1_named_data *subject_list = NULL;
    uint8_t                 *tmp          = NULL;

    /* ---- 1. Export SubjectPublicKeyInfo DER from TEE ---- */
    uint8_t spki[HW_CSR_SPKI_MAX];
    size_t  spki_len = 0;
    err = actrust_sec_key_get_public(key->key.hw.slot_id, spki, sizeof(spki),
                                     &spki_len);
    if (err != ACTRUST_OK) {
        return err;
    }

    /* ---- 2. Parse subject DN string ---- */
    ret = mbedtls_x509_string_to_names(&subject_list, subject);
    if (ret != 0) {
        result = CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
        goto cleanup;
    }

    /* ---- 3. Build CertificationRequestInfo (back-to-front) ---- */
    uint8_t        cri_buf[HW_CSR_CRI_MAX];
    unsigned char *c   = cri_buf + sizeof(cri_buf);
    size_t         len = 0;

    /* attributes [0] IMPLICIT SET OF Attribute (empty) */
    ret = mbedtls_asn1_write_len(&c, cri_buf, 0);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    ret = mbedtls_asn1_write_tag(
        &c, cri_buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_CONTEXT_SPECIFIC);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    /* subjectPKInfo (raw DER from TEE) */
    ret = mbedtls_asn1_write_raw_buffer(&c, cri_buf, spki, spki_len);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    /* subject DN */
    ret = mbedtls_x509_write_names(&c, cri_buf, subject_list);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    /* version INTEGER 0 */
    ret = mbedtls_asn1_write_int(&c, cri_buf, 0);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    /* outer SEQUENCE for CertificationRequestInfo */
    ret = mbedtls_asn1_write_len(&c, cri_buf, len);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    ret = mbedtls_asn1_write_tag(
        &c, cri_buf, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    /* c → start of CRI, len = total CRI length */
    unsigned char *cri_der     = c;
    size_t         cri_der_len = len;

    /* ---- 4. Hash the CertificationRequestInfo ---- */
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
    if (md_info == NULL) {
        goto cleanup;
    }
    size_t  digest_len = mbedtls_md_get_size(md_info);
    uint8_t digest[MBEDTLS_MD_MAX_SIZE];
    ret = mbedtls_md(md_info, cri_der, cri_der_len, digest);
    if (ret != 0) {
        goto cleanup;
    }

    /* ---- 5. Sign via TEE ---- */
    uint8_t sig[HW_CSR_SIG_MAX];
    size_t  sig_len = 0;
    err = actrust_sec_ecdsa_sign(key->key.hw.slot_id, digest, digest_len, sig,
                                 sizeof(sig), &sig_len);
    if (err != ACTRUST_OK) {
        result = err;
        goto cleanup;
    }

    /* ---- 6. Assemble final CertificationRequest (back-to-front) ---- */
    tmp = (uint8_t *) ACTRUST_CALLOC(1, out_cap);
    if (tmp == NULL) {
        result = CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
        goto cleanup;
    }

    c   = tmp + out_cap;
    len = 0;

    /* signature BIT STRING (0 unused bits + DER-encoded ECDSA sig) */
    ret = mbedtls_asn1_write_bitstring(&c, tmp, sig, sig_len * 8u);
    if (ret < 0) {
        result = (ret == MBEDTLS_ERR_ASN1_BUF_TOO_SMALL)
                     ? CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL)
                     : CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
        goto cleanup;
    }
    len += (size_t) ret;

    /* signatureAlgorithm  (ECDSA-with-SHA*, no parameters) */
    ret = mbedtls_asn1_write_algorithm_identifier_ext(&c, tmp, sig_oid,
                                                      sig_oid_len, 0, 0);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    /* certificationRequestInfo (raw DER) */
    ret = mbedtls_asn1_write_raw_buffer(&c, tmp, cri_der, cri_der_len);
    if (ret < 0) {
        result = (ret == MBEDTLS_ERR_ASN1_BUF_TOO_SMALL)
                     ? CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL)
                     : CRYPTO_ERR(ACTRUST_ERR_HW_FAILURE);
        goto cleanup;
    }
    len += (size_t) ret;

    /* outer SEQUENCE */
    ret = mbedtls_asn1_write_len(&c, tmp, len);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    ret = mbedtls_asn1_write_tag(
        &c, tmp, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
    if (ret < 0) {
        goto cleanup;
    }
    len += (size_t) ret;

    /* ---- 7. Copy to caller's buffer ---- */
    if (len > out_cap) {
        result = CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
        goto cleanup;
    }

    memcpy(out, c, len);
    *out_len = len;
    result   = ACTRUST_OK;

cleanup:
    mbedtls_asn1_free_named_data_list(&subject_list);
    if (tmp != NULL) {
        mbedtls_platform_zeroize(tmp, out_cap);
        ACTRUST_FREE(tmp);
    }
    return result;
}

/* ========================================================================
 * Backend Ops Table
 * ======================================================================== */

const crypto_backend_ops_t hw_crypto_ops = {
    .random            = hw_crypto_random,
    .hash              = hw_crypto_hash,
    .key_open          = hw_crypto_key_open,
    .key_close         = hw_crypto_key_close,
    .key_generate      = hw_crypto_key_generate,
    .key_import        = hw_crypto_key_import,
    .key_destroy       = hw_crypto_key_destroy,
    .key_migrate       = hw_crypto_key_migrate,
    .key_export_public = hw_crypto_key_export_public,
    .ecdsa_sign        = hw_crypto_ecdsa_sign,
    .ecdsa_verify      = hw_crypto_ecdsa_verify,
    .aes_encrypt       = hw_crypto_aes_encrypt,
    .aes_decrypt       = hw_crypto_aes_decrypt,
    .csr_generate      = hw_crypto_csr_generate,
};
