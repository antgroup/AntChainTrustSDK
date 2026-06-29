// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tls.h
 * @brief Generic TLS client interface.
 *
 * Public API is intentionally minimal:
 * - connect: create TCP + TLS session
 * - close: close TLS session and underlying TCP connection
 * - read: receive plaintext via TLS
 * - write: send plaintext via TLS
 *
 * Unless otherwise noted, TLS-internal failures are returned as:
 * @code
 * ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_TLS, reason)
 * @endcode
 *
 * Errors returned by the underlying network adapter API
 * (@ref actrust_net_open / @ref actrust_net_dns_resolve /
 * @ref actrust_net_connect / @ref actrust_net_close) may be
 * propagated as-is.
 *
 * Current default policy in this component restricts negotiation to
 * secp256r1 + SHA-256 based signature/cipher settings.
 *
 * Typical usage:
 * @code{.c}
 *   actrust_tls_t tls;
 *   actrust_tls_config_t cfg = {
 *       .host = "broker.example.com",
 *       .port = 8883,
 *       .ca = ca_pem,
 *       .ca_len = ca_pem_len,
 *       .ca_format = ACTRUST_TLS_CERT_FORMAT_PEM,
 *       .crypto_ctx = crypto_ctx,
 *       .client_cert = client_cert_pem,
 *       .client_cert_len = client_cert_pem_len,
 *       .client_cert_format = ACTRUST_TLS_CERT_FORMAT_PEM,
 *       .client_key = client_key,
 *   };
 *
 *   actrust_tls_connect(&tls, &cfg, 5000);
 *   actrust_tls_write(tls, tx_buf, tx_len, 3000, &sent);
 *   actrust_tls_read(tls, rx_buf, sizeof(rx_buf), 3000, &read_len);
 *   actrust_tls_close(&tls, 1000);
 * @endcode
 */

#ifndef ACTRUST_TLS_H
#define ACTRUST_TLS_H

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_errno.h"

/* Component */
#include "crypto/crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Certificate data format. */
typedef enum {
    ACTRUST_TLS_CERT_FORMAT_PEM = 1, /**< PEM (base64 text) */
    ACTRUST_TLS_CERT_FORMAT_DER = 2, /**< DER (binary ASN.1) */
} actrust_tls_cert_format_t;

/**
 * @brief TLS connection configuration.
 */
typedef struct {
    /** @brief Remote host (domain or IPv4 string). */
    const char *host;
    /** @brief Remote TCP port. */
    uint16_t port;

    /**
     * @brief Crypto context used by TLS RNG and mTLS signing.
     *
     * Must be created by caller via @ref actrust_crypto_init and remain valid
     * for the whole TLS session lifetime.
     */
    actrust_crypto_ctx_t crypto_ctx;

    /**
     * @brief Root CA certificate(s).
     *
     * Required by default. May be NULL only when @p insecure is set to
     * @c true (test-only mode).
     */
    const uint8_t *ca;
    /** @brief Length of @p ca in bytes. */
    size_t ca_len;
    /** @brief Format of @p ca. */
    actrust_tls_cert_format_t ca_format;

    /**
     * @brief Disable server certificate verification.
     *
     * When @c true AND @p ca is NULL, the TLS handshake proceeds without
     * verifying the server certificate. When @c false (the default),
     * @ref actrust_tls_connect refuses to connect without a CA and returns
     * @c ACTRUST_ERR_INVALID_ARG.
     *
     * @warning Insecure mode disables authentication and exposes the
     *          session to active man-in-the-middle attacks. It is intended
     *          solely for local development and smoke tests, never for
     *          production deployments.
     */
    bool insecure;

    /** @brief Optional client certificate for mutual TLS. */
    const uint8_t *client_cert;
    /** @brief Length of @p client_cert in bytes. */
    size_t client_cert_len;
    /** @brief Format of @p client_cert. */
    actrust_tls_cert_format_t client_cert_format;

    /**
     * @brief Optional client private key handle for mutual TLS.
     *
     * When provided, this handle should be opened from the same
     * @ref crypto_ctx.
     */
    actrust_crypto_key_t client_key;
} actrust_tls_config_t;

/** @brief Opaque TLS handle. */
typedef struct actrust_tls_ctx *actrust_tls_t;

/**
 * @brief Establish TCP connection and complete TLS handshake.
 *
 * This function internally performs DNS resolve (if needed), TCP connect,
 * TLS context setup, certificate loading and TLS handshake.
 *
 * @param[out] out_tls     Receives TLS handle on success.
 * @param[in]  cfg         TLS connection configuration.
 * @param[in]  timeout_ms  Timeout budget in milliseconds for connect/handshake.
 *
 * @return @c ACTRUST_OK on success.
 * @return Error with reason @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return Error with reason @c ACTRUST_ERR_TIMEOUT on connect or handshake
 * timeout.
 * @return Error with reason @c ACTRUST_ERR_TLS_HANDSHAKE_FAILED when handshake
 * fails.
 * @return Error with reason @c ACTRUST_ERR_TLS_CERT_VERIFY_FAILED when server
 * cert verification fails.
 * @return Error with reason @c ACTRUST_ERR_TLS_CONN_RESET when transport resets
 * the connection.
 * @return Error with reason @c ACTRUST_ERR_TLS_PEER_CLOSED when peer closes
 * during handshake.
 * @return Error with reason @c ACTRUST_ERR_TLS_INVALID_RECORD on malformed TLS
 * record.
 * @return Error with reason @c ACTRUST_ERR_IO on other TLS transport failures.
 * @return Errors from @ref actrust_net_open / @ref actrust_net_dns_resolve /
 * @ref actrust_net_connect may be returned directly.
 * @return Error with reason @c ACTRUST_ERR_NO_MEM on allocation failure.
 */
actrust_err_t actrust_tls_connect(actrust_tls_t              *out_tls,
                                  const actrust_tls_config_t *cfg,
                                  uint32_t                    timeout_ms);

/**
 * @brief Close TLS session and underlying TCP connection, then release handle.
 *
 * When @p tls references a valid handle, @p *tls is set to NULL after local
 * cleanup even if the underlying socket close reports an error.
 *
 * @param[in,out] tls         Pointer to TLS handle.
 * @param[in]     timeout_ms  Timeout in milliseconds for close_notify/close.
 *
 * @return @c ACTRUST_OK on success.
 * @return Error with reason @c ACTRUST_ERR_INVALID_ARG for invalid handle.
 * @return Errors from @ref actrust_net_close may be returned after cleanup.
 */
actrust_err_t actrust_tls_close(actrust_tls_t *tls, uint32_t timeout_ms);

/**
 * @brief Write plaintext data through TLS.
 *
 * A successful call may write fewer than @p len bytes; caller should retry
 * remaining bytes as needed.
 *
 * @param[in]  tls            TLS handle.
 * @param[in]  buf            Source buffer.
 * @param[in]  len            Bytes requested to write.
 * @param[in]  timeout_ms     Operation timeout in milliseconds.
 * @param[out] bytes_written  Receives number of bytes actually written.
 *
 * @return @c ACTRUST_OK on success (including partial write).
 * @return Error with reason @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return Error with reason @c ACTRUST_ERR_BAD_STATE if connection is not
 * ready.
 * @return Error with reason @c ACTRUST_ERR_TIMEOUT on timeout.
 * @return Error with reason @c ACTRUST_ERR_TLS_CONN_RESET when transport resets
 * the connection.
 * @return Error with reason @c ACTRUST_ERR_TLS_INVALID_RECORD on malformed TLS
 * record.
 * @return Error with reason @c ACTRUST_ERR_IO on other transport failures.
 */
actrust_err_t actrust_tls_write(actrust_tls_t tls, const uint8_t *buf,
                                size_t len, uint32_t timeout_ms,
                                size_t *bytes_written);

/**
 * @brief Read plaintext data through TLS.
 *
 * A successful call may return fewer than @p len bytes.
 * @c ACTRUST_OK with @p *bytes_read == 0 may indicate peer orderly shutdown.
 *
 * @param[in]  tls         TLS handle.
 * @param[out] buf         Destination buffer.
 * @param[in]  len         Buffer capacity in bytes.
 * @param[in]  timeout_ms  Operation timeout in milliseconds.
 * @param[out] bytes_read  Receives number of bytes actually read.
 *
 * @return @c ACTRUST_OK on success (including partial read).
 * @return Error with reason @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return Error with reason @c ACTRUST_ERR_BAD_STATE if connection is not
 * ready.
 * @return Error with reason @c ACTRUST_ERR_TIMEOUT on timeout.
 * @return Error with reason @c ACTRUST_ERR_TLS_CONN_RESET when transport resets
 * the connection.
 * @return Error with reason @c ACTRUST_ERR_TLS_INVALID_RECORD on malformed TLS
 * record.
 * @return Error with reason @c ACTRUST_ERR_IO on other transport failures.
 */
actrust_err_t actrust_tls_read(actrust_tls_t tls, uint8_t *buf, size_t len,
                               uint32_t timeout_ms, size_t *bytes_read);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_TLS_H */
