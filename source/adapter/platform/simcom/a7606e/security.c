// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file security.c
 * @brief SIMCom A7606E-H platform security adapter implementation
 *
 * Implements the AntChainTrustSDK secure storage interface using the Sunsea TEE
 * on the A7606E-H (ASR1806 SoC).  Random bytes are sourced from the
 * getrandom(2) software CSPRNG (the TEE provides no entropy service). Key
 * management and crypto operations return UNSUPPORTED because the TEE provides
 * RSA/AES only while the AntChainTrustSDK interface requires ECDSA.
 */

/* C standard */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

/* Common */
#include "common/common.h"

/* Project */
#include "actrust_errno.h"

/* Adapter */
#include "adapter/security.h"

/** @brief Convenience macro to build security-module errors */
#define SEC_ERR(reason)                                                        \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SECURITY, (reason))

/** @brief Maximum length for a TEE object ID string */
#define SEC_ID_MAX 32

/** @brief Size of the big-endian length prefix prepended to stored data */
#define SEC_LEN_PREFIX 4

/*
 * Declare the Sunsea TEE functions directly to avoid pulling in Sunsea_TEE.h
 * and its type definitions that may conflict with project headers.
 */
extern int Sunsea_TEE_read_secure_data(char *id, char *data, int data_len);
extern int Sunsea_TEE_write_secure_data(char *id, char *data, int data_len);
extern int Sunsea_TEE_delete_secure_data(char *id);

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

/**
 * @brief Build a TEE object ID string from a slot identifier.
 */
static bool sec_build_id(actrust_sec_slot_t slot_id, char *id, size_t size)
{
    int n = snprintf(id, size, "sec_%08x", slot_id);
    return n > 0 && (size_t) n < size;
}

/* ========================================================================
 * Secure Storage — backed by Sunsea TEE
 * ======================================================================== */

actrust_err_t actrust_sec_store_write(actrust_sec_slot_t slot_id,
                                      const uint8_t *data, size_t len)
{
    if (data == NULL && len > 0u) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char id[SEC_ID_MAX];
    if (!sec_build_id(slot_id, id, sizeof(id))) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    /* Prepend a 4-byte big-endian length so we can recover exact size on read
     */
    if (len > SIZE_MAX - SEC_LEN_PREFIX) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    size_t total = SEC_LEN_PREFIX + len;
    if (total > (size_t) INT_MAX) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    uint8_t *buf = (uint8_t *) malloc(total);
    if (buf == NULL) {
        return SEC_ERR(ACTRUST_ERR_NO_MEM);
    }

    buf[0] = (uint8_t) ((len >> 24) & 0xFFu);
    buf[1] = (uint8_t) ((len >> 16) & 0xFFu);
    buf[2] = (uint8_t) ((len >> 8) & 0xFFu);
    buf[3] = (uint8_t) (len & 0xFFu);
    if (len > 0u) {
        memcpy(buf + SEC_LEN_PREFIX, data, len);
    }

    int rc = Sunsea_TEE_write_secure_data(id, (char *) buf, (int) total);
    actrust_secure_free(buf, total);

    if (rc != 0) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    return ACTRUST_OK;
}

actrust_err_t actrust_sec_store_read(actrust_sec_slot_t slot_id, uint8_t *out,
                                     size_t out_len, size_t *actual_len)
{
    if (out == NULL || actual_len == NULL) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char id[SEC_ID_MAX];
    if (!sec_build_id(slot_id, id, sizeof(id))) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    /* Read into a temporary buffer that includes the length prefix */
    if (out_len > SIZE_MAX - SEC_LEN_PREFIX) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    size_t tmp_len = SEC_LEN_PREFIX + out_len;
    if (tmp_len > (size_t) INT_MAX) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    uint8_t *tmp = (uint8_t *) calloc(1u, tmp_len);
    if (tmp == NULL) {
        return SEC_ERR(ACTRUST_ERR_NO_MEM);
    }

    int rc = Sunsea_TEE_read_secure_data(id, (char *) tmp, (int) tmp_len);
    if (rc != 0) {
        actrust_secure_free(tmp, tmp_len);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    /* Decode the 4-byte big-endian length prefix */
    size_t stored_len = ((size_t) tmp[0] << 24) | ((size_t) tmp[1] << 16) |
                        ((size_t) tmp[2] << 8) | (size_t) tmp[3];

    if (stored_len > out_len) {
        *actual_len = stored_len;
        actrust_secure_free(tmp, tmp_len);
        return SEC_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    memcpy(out, tmp + SEC_LEN_PREFIX, stored_len);
    *actual_len = stored_len;
    actrust_secure_free(tmp, tmp_len);
    return ACTRUST_OK;
}

actrust_err_t actrust_sec_store_delete(actrust_sec_slot_t slot_id)
{
    char id[SEC_ID_MAX];
    if (!sec_build_id(slot_id, id, sizeof(id))) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    if (Sunsea_TEE_delete_secure_data(id) != 0) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Random number generation - getrandom(2) software CSPRNG
 * ======================================================================== */

actrust_err_t actrust_sec_random(uint8_t *out, size_t len)
{
    if (out == NULL || len == 0) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t total = 0;
    while (total < len) {
        ssize_t n = getrandom(out + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return SEC_ERR(ACTRUST_ERR_IO);
        }
        if (n == 0) {
            return SEC_ERR(ACTRUST_ERR_IO);
        }
        total += (size_t) n;
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Secure Key Management — not available without ECDSA TEE support
 * ======================================================================== */

actrust_err_t actrust_sec_key_generate(actrust_sec_slot_t     slot_id,
                                       actrust_sec_key_type_t type)
{
    (void) slot_id;
    (void) type;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_sec_key_delete(actrust_sec_slot_t slot_id)
{
    (void) slot_id;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_sec_key_get_public(actrust_sec_slot_t slot_id,
                                         uint8_t *out, size_t out_cap,
                                         size_t *out_len)
{
    (void) slot_id;
    (void) out;
    (void) out_cap;
    (void) out_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

/* ========================================================================
 * Secure Hash — not available on this TEE
 * ======================================================================== */

actrust_err_t actrust_sec_hash(actrust_sec_hash_alg_t alg, const uint8_t *input,
                               size_t input_len, uint8_t *out, size_t out_cap,
                               size_t *out_len)
{
    (void) alg;
    (void) input;
    (void) input_len;
    (void) out;
    (void) out_cap;
    (void) out_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

/* ========================================================================
 * Secure AES — not available on this TEE
 * ======================================================================== */

actrust_err_t actrust_sec_aes_encrypt(
    actrust_sec_slot_t key_slot, actrust_sec_sym_alg_t alg,
    actrust_sec_padding_t padding, const uint8_t *iv, size_t iv_len,
    const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t input_len,
    uint8_t *output, size_t output_cap, size_t *output_len, uint8_t *tag,
    size_t *tag_len)
{
    (void) key_slot;
    (void) alg;
    (void) padding;
    (void) iv;
    (void) iv_len;
    (void) aad;
    (void) aad_len;
    (void) input;
    (void) input_len;
    (void) output;
    (void) output_cap;
    (void) output_len;
    (void) tag;
    (void) tag_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_sec_aes_decrypt(
    actrust_sec_slot_t key_slot, actrust_sec_sym_alg_t alg,
    actrust_sec_padding_t padding, const uint8_t *iv, size_t iv_len,
    const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t input_len,
    uint8_t *output, size_t output_cap, size_t *output_len, const uint8_t *tag,
    size_t tag_len)
{
    (void) key_slot;
    (void) alg;
    (void) padding;
    (void) iv;
    (void) iv_len;
    (void) aad;
    (void) aad_len;
    (void) input;
    (void) input_len;
    (void) output;
    (void) output_cap;
    (void) output_len;
    (void) tag;
    (void) tag_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

/* ========================================================================
 * Secure Crypto — not available without ECDSA TEE support
 * ======================================================================== */

actrust_err_t actrust_sec_ecdsa_sign(actrust_sec_slot_t slot_id,
                                     const uint8_t *digest, size_t dig_len,
                                     uint8_t *sig, size_t sig_cap,
                                     size_t *sig_len)
{
    (void) slot_id;
    (void) digest;
    (void) dig_len;
    (void) sig;
    (void) sig_cap;
    (void) sig_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_sec_ecdsa_verify(actrust_sec_slot_t slot_id,
                                       const uint8_t *digest, size_t dig_len,
                                       const uint8_t *sig, size_t sig_len)
{
    (void) slot_id;
    (void) digest;
    (void) dig_len;
    (void) sig;
    (void) sig_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}
