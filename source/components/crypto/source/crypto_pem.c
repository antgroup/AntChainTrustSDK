// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file crypto_pem.c
 * @brief PEM/DER encoding conversion utilities.
 */

/* C standard */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Third-party */
#include <mbedtls/pem.h>

/* Common */
#include "common/common.h"

/* Project */
#include "actrust_errno.h"

/* Crypto */
#include "crypto/crypto.h"

#define CRYPTO_ERR(reason)                                                     \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_CRYPTO, (reason))

/* ========================================================================
 * PEM boundary markers
 * ======================================================================== */

#define PEM_CSR_HEADER         "-----BEGIN CERTIFICATE REQUEST-----\n"
#define PEM_CSR_FOOTER         "-----END CERTIFICATE REQUEST-----\n"
#define PEM_CERT_HEADER        "-----BEGIN CERTIFICATE-----\n"
#define PEM_CERT_FOOTER        "-----END CERTIFICATE-----\n"
#define PEM_PUBLIC_KEY_HEADER  "-----BEGIN PUBLIC KEY-----\n"
#define PEM_PUBLIC_KEY_FOOTER  "-----END PUBLIC KEY-----\n"
#define PEM_PRIVATE_KEY_HEADER "-----BEGIN PRIVATE KEY-----\n"
#define PEM_PRIVATE_KEY_FOOTER "-----END PRIVATE KEY-----\n"

#define PEM_CSR_BEGIN         "-----BEGIN CERTIFICATE REQUEST-----"
#define PEM_CSR_END           "-----END CERTIFICATE REQUEST-----"
#define PEM_CERT_BEGIN        "-----BEGIN CERTIFICATE-----"
#define PEM_CERT_END          "-----END CERTIFICATE-----"
#define PEM_PUBLIC_KEY_BEGIN  "-----BEGIN PUBLIC KEY-----"
#define PEM_PUBLIC_KEY_END    "-----END PUBLIC KEY-----"
#define PEM_PRIVATE_KEY_BEGIN "-----BEGIN PRIVATE KEY-----"
#define PEM_PRIVATE_KEY_END   "-----END PRIVATE KEY-----"

static actrust_err_t pem_get_bounds(actrust_crypto_pem_object_t object,
                                    bool with_newline, const char **header,
                                    const char **footer)
{
    switch (object) {
        case ACTRUST_CRYPTO_PEM_OBJECT_CSR:
            *header = with_newline ? PEM_CSR_HEADER : PEM_CSR_BEGIN;
            *footer = with_newline ? PEM_CSR_FOOTER : PEM_CSR_END;
            return ACTRUST_OK;
        case ACTRUST_CRYPTO_PEM_OBJECT_CERTIFICATE:
            *header = with_newline ? PEM_CERT_HEADER : PEM_CERT_BEGIN;
            *footer = with_newline ? PEM_CERT_FOOTER : PEM_CERT_END;
            return ACTRUST_OK;
        case ACTRUST_CRYPTO_PEM_OBJECT_PUBLIC_KEY:
            *header =
                with_newline ? PEM_PUBLIC_KEY_HEADER : PEM_PUBLIC_KEY_BEGIN;
            *footer = with_newline ? PEM_PUBLIC_KEY_FOOTER : PEM_PUBLIC_KEY_END;
            return ACTRUST_OK;
        case ACTRUST_CRYPTO_PEM_OBJECT_PRIVATE_KEY:
            *header =
                with_newline ? PEM_PRIVATE_KEY_HEADER : PEM_PRIVATE_KEY_BEGIN;
            *footer =
                with_newline ? PEM_PRIVATE_KEY_FOOTER : PEM_PRIVATE_KEY_END;
            return ACTRUST_OK;
        default:
            return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

actrust_err_t actrust_crypto_der_to_pem(actrust_crypto_pem_object_t object,
                                        const uint8_t *der, size_t der_len,
                                        char *pem_out, size_t pem_cap,
                                        size_t *pem_len)
{
    if (der == NULL || der_len == 0u || pem_out == NULL || pem_cap == 0u ||
        pem_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const char   *header = NULL;
    const char   *footer = NULL;
    actrust_err_t err    = pem_get_bounds(object, true, &header, &footer);
    if (err != ACTRUST_OK) {
        return err;
    }

    size_t write_len = 0u;
    int    ret       = mbedtls_pem_write_buffer(header, footer, der, der_len,
                                                (unsigned char *) pem_out, pem_cap,
                                                &write_len);
    if (ret != 0) {
        return CRYPTO_ERR(ACTRUST_ERR_IO);
    }
    if (write_len == 0u) {
        return CRYPTO_ERR(ACTRUST_ERR_IO);
    }

    *pem_len = (pem_out[write_len - 1u] == '\0') ? write_len - 1u : write_len;
    return ACTRUST_OK;
}

actrust_err_t actrust_crypto_pem_to_der(actrust_crypto_pem_object_t object,
                                        const char *pem, size_t pem_len,
                                        uint8_t *der_out, size_t der_cap,
                                        size_t *der_len)
{
    if (pem == NULL || pem_len == 0u || der_out == NULL || der_cap == 0u ||
        der_len == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const char   *header = NULL;
    const char   *footer = NULL;
    actrust_err_t err    = pem_get_bounds(object, false, &header, &footer);
    if (err != ACTRUST_OK) {
        return err;
    }

    if (pem_len > SIZE_MAX - 1u) {
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char *pem_copy = (char *) ACTRUST_CALLOC(1u, pem_len + 1u);
    if (pem_copy == NULL) {
        return CRYPTO_ERR(ACTRUST_ERR_NO_MEM);
    }
    memcpy(pem_copy, pem, pem_len);

    mbedtls_pem_context pem_ctx;
    mbedtls_pem_init(&pem_ctx);

    size_t use_len = 0u;
    int    ret     = mbedtls_pem_read_buffer(&pem_ctx, header, footer,
                                             (const unsigned char *) pem_copy, NULL,
                                             0u, &use_len);
    (void) use_len;
    actrust_secure_free(pem_copy, pem_len + 1u);
    if (ret != 0) {
        mbedtls_pem_free(&pem_ctx);
        return CRYPTO_ERR(ACTRUST_ERR_IO);
    }

    size_t               decoded_len = 0u;
    const unsigned char *decoded =
        mbedtls_pem_get_buffer(&pem_ctx, &decoded_len);
    if (decoded == NULL || decoded_len == 0u) {
        mbedtls_pem_free(&pem_ctx);
        return CRYPTO_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (decoded_len > der_cap) {
        *der_len = decoded_len;
        actrust_secure_zeroize((void *) decoded, decoded_len);
        mbedtls_pem_free(&pem_ctx);
        return CRYPTO_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    memcpy(der_out, decoded, decoded_len);
    *der_len = decoded_len;
    actrust_secure_zeroize((void *) decoded, decoded_len);
    mbedtls_pem_free(&pem_ctx);
    return ACTRUST_OK;
}
