// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file tls.c
 * @brief TLS client component.
 */

/* C standard */
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Third-party */
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <mbedtls/x509_crt.h>

/* Common */
#include "common/common.h"

/* Project */
#include "actrust_config.h"

/* TLS */
#include "tls/tls.h"
#include "tls_pk_wrapper.h"

/* Component */
#include "crypto/crypto.h"
#include "log/log.h"

/* Adapter */
#include "adapter/network.h"

/** @brief Convenience macro to build TLS-module errors */
#define TLS_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_TLS, (reason))

#if defined(CONFIG_ACTRUST_TLS_MIN_VERSION_1_3)
#define TLS_MIN_VERSION MBEDTLS_SSL_VERSION_TLS1_3
#else
#define TLS_MIN_VERSION MBEDTLS_SSL_VERSION_TLS1_2
#endif

/* Negotiate secp256r1 key exchange group only. */
static const uint16_t g_tls_groups[] = {
    MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1, 0 /* mbedtls needs to end with 0 */
};

/* Prefer SHA-256 based ECDSA signatures. */
static const uint16_t g_tls_sig_algs[] = {
#if defined(CONFIG_ACTRUST_TLS_MIN_VERSION_1_2)
    (uint16_t) ((MBEDTLS_SSL_HASH_SHA256 << 8) | MBEDTLS_SSL_SIG_ECDSA),
#endif
    MBEDTLS_TLS1_3_SIG_ECDSA_SECP256R1_SHA256,
    0 /* mbedtls needs to end with 0 */
};

/* Restrict negotiated cipher suites to SHA-256 variants. */
static const int g_tls_ciphersuites[] = {
#if defined(CONFIG_ACTRUST_TLS_MIN_VERSION_1_2)
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
#endif
    MBEDTLS_TLS1_3_AES_128_GCM_SHA256, 0 /* mbedtls needs to end with 0 */
};

/* ========================================================================
 * Private Types
 * ======================================================================== */

struct actrust_tls_ctx {
    actrust_net_t net;
    void         *backend_ctx;
};

typedef struct tls_mbedtls_ctx { /* backend-specific context (mbedTLS) */
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config  conf;
    mbedtls_x509_crt    ca;
    mbedtls_x509_crt    client_cert;
    mbedtls_pk_context  client_key;
    actrust_net_t       net;
    uint32_t            io_timeout_ms;
} tls_mbedtls_ctx_t;

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

static actrust_err_t tls_map_mbedtls_err(int ret)
{
    switch (ret) {
        case 0:
            return ACTRUST_OK;
        case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
            return TLS_ERR(ACTRUST_ERR_TLS_HANDSHAKE_FAILED);
        case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
            return TLS_ERR(ACTRUST_ERR_TLS_CERT_VERIFY_FAILED);
        case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
        case MBEDTLS_ERR_SSL_CONN_EOF:
            return TLS_ERR(ACTRUST_ERR_TLS_PEER_CLOSED);
        case MBEDTLS_ERR_NET_CONN_RESET:
            return TLS_ERR(ACTRUST_ERR_TLS_CONN_RESET);
        case MBEDTLS_ERR_SSL_INVALID_RECORD:
            return TLS_ERR(ACTRUST_ERR_TLS_INVALID_RECORD);
        case MBEDTLS_ERR_SSL_TIMEOUT:
            return TLS_ERR(ACTRUST_ERR_TIMEOUT);
        case MBEDTLS_ERR_SSL_ALLOC_FAILED:
        case MBEDTLS_ERR_X509_ALLOC_FAILED:
        case MBEDTLS_ERR_PK_ALLOC_FAILED:
            return TLS_ERR(ACTRUST_ERR_NO_MEM);
        case MBEDTLS_ERR_SSL_BAD_INPUT_DATA:
        case MBEDTLS_ERR_X509_BAD_INPUT_DATA:
        case MBEDTLS_ERR_PK_BAD_INPUT_DATA:
            return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
        default:
            return TLS_ERR(ACTRUST_ERR_IO);
    }
}

static void tls_mbedtls_ctx_free(tls_mbedtls_ctx_t *mbedtls_ctx)
{
    if (mbedtls_ctx == NULL) {
        return;
    }
    /* Release mbedTLS members */
    mbedtls_ssl_free(&mbedtls_ctx->ssl);
    mbedtls_ssl_config_free(&mbedtls_ctx->conf);
    mbedtls_x509_crt_free(&mbedtls_ctx->ca);
    mbedtls_x509_crt_free(&mbedtls_ctx->client_cert);
    mbedtls_pk_free(&mbedtls_ctx->client_key);
}

static int tls_rng_cb(void *p_rng, unsigned char *output, size_t output_len)
{
    if (output == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    actrust_crypto_ctx_t crypto_ctx = (actrust_crypto_ctx_t) p_rng;
    if (crypto_ctx == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    actrust_err_t err = actrust_crypto_random(crypto_ctx, output, output_len);
    if (err != ACTRUST_OK) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    return 0;
}

static int tls_net_send_cb(void *bio_ctx, const unsigned char *buf, size_t len)
{
    tls_mbedtls_ctx_t *mbedtls_ctx = (tls_mbedtls_ctx_t *) bio_ctx;
    if (mbedtls_ctx == NULL || mbedtls_ctx->net == NULL || buf == NULL ||
        len == 0u) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    if (len > (size_t) INT_MAX) {
        len = (size_t) INT_MAX;
    }

    size_t        sent = 0u;
    actrust_err_t err  = actrust_net_send(mbedtls_ctx->net, buf, len,
                                          mbedtls_ctx->io_timeout_ms, &sent);
    if (err == ACTRUST_OK) {
        if (sent == 0u) {
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }
        return (sent > (size_t) INT_MAX) ? INT_MAX : (int) sent;
    }

    switch (ACTRUST_ERR_CODE(err)) {
        case ACTRUST_ERR_TIMEOUT:
            return MBEDTLS_ERR_SSL_TIMEOUT;
        case ACTRUST_ERR_WOULD_BLOCK:
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        default:
            return MBEDTLS_ERR_NET_SEND_FAILED;
    }
}

static int tls_net_recv_cb(void *bio_ctx, unsigned char *buf, size_t len)
{
    tls_mbedtls_ctx_t *mbedtls_ctx = (tls_mbedtls_ctx_t *) bio_ctx;
    if (mbedtls_ctx == NULL || mbedtls_ctx->net == NULL || buf == NULL ||
        len == 0u) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    if (len > (size_t) INT_MAX) {
        len = (size_t) INT_MAX;
    }

    size_t        recvd = 0u;
    actrust_err_t err   = actrust_net_recv(mbedtls_ctx->net, buf, len,
                                           mbedtls_ctx->io_timeout_ms, &recvd);
    if (err == ACTRUST_OK) {
        if (recvd == 0u) {
            return MBEDTLS_ERR_SSL_CONN_EOF;
        }
        return (recvd > (size_t) INT_MAX) ? INT_MAX : (int) recvd;
    }

    switch (ACTRUST_ERR_CODE(err)) {
        case ACTRUST_ERR_TIMEOUT:
            return MBEDTLS_ERR_SSL_TIMEOUT;
        case ACTRUST_ERR_WOULD_BLOCK:
            return MBEDTLS_ERR_SSL_WANT_READ;
        default:
            return MBEDTLS_ERR_NET_RECV_FAILED;
    }
}

static int tls_parse_cert(mbedtls_x509_crt *crt, const uint8_t *cert,
                          size_t cert_len, actrust_tls_cert_format_t format)
{
    if (crt == NULL || cert == NULL || cert_len == 0u) {
        return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
    }

    if (format == ACTRUST_TLS_CERT_FORMAT_DER) {
        return mbedtls_x509_crt_parse_der(crt, cert, cert_len);
    }

    if (format == ACTRUST_TLS_CERT_FORMAT_PEM) {
        /* mbedtls_x509_crt_parse() expects NUL-terminated PEM input. */
        if (cert_len > SIZE_MAX - 1u) {
            return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
        }

        unsigned char *pem = ACTRUST_CALLOC(1u, cert_len + 1u);
        if (pem == NULL) {
            return MBEDTLS_ERR_X509_ALLOC_FAILED;
        }
        memcpy(pem, cert, cert_len);
        pem[cert_len] = '\0';
        int ret       = mbedtls_x509_crt_parse(crt, pem, cert_len + 1u);
        ACTRUST_FREE(pem);
        return ret;
    }

    return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
}

static int tls_backend_init_ctx(tls_mbedtls_ctx_t          *mbedtls_ctx,
                                actrust_tls_t               tls,
                                const actrust_tls_config_t *cfg,
                                uint32_t                    timeout_ms)
{
    if (mbedtls_ctx == NULL || tls == NULL || cfg == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    if ((cfg->ca == NULL && cfg->ca_len > 0u) ||
        (cfg->ca != NULL && cfg->ca_len == 0u) ||
        (cfg->client_cert == NULL && cfg->client_cert_len > 0u) ||
        (cfg->client_cert != NULL && cfg->client_cert_len == 0u) ||
        ((cfg->client_cert == NULL) != (cfg->client_key == NULL))) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    /* initialize mbedTLS context */
    mbedtls_ssl_init(&mbedtls_ctx->ssl);
    mbedtls_ssl_config_init(&mbedtls_ctx->conf);
    mbedtls_x509_crt_init(&mbedtls_ctx->ca);
    mbedtls_x509_crt_init(&mbedtls_ctx->client_cert);
    mbedtls_pk_init(&mbedtls_ctx->client_key);

    mbedtls_ctx->net           = tls->net;
    mbedtls_ctx->io_timeout_ms = timeout_ms;

    int ret = mbedtls_ssl_config_defaults(
        &mbedtls_ctx->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        return ret;
    }

    /* Set TLS version */
    mbedtls_ssl_conf_min_tls_version(&mbedtls_ctx->conf, TLS_MIN_VERSION);
    mbedtls_ssl_conf_max_tls_version(&mbedtls_ctx->conf,
                                     MBEDTLS_SSL_VERSION_TLS1_3);
    /* Set key exchange group */
    mbedtls_ssl_conf_groups(&mbedtls_ctx->conf, g_tls_groups);
    /* Set signature algorithm */
    mbedtls_ssl_conf_sig_algs(&mbedtls_ctx->conf, g_tls_sig_algs);
    /* Set cipher suite preferences. */
    mbedtls_ssl_conf_ciphersuites(&mbedtls_ctx->conf, g_tls_ciphersuites);
    /* Set RNG callback. */
    mbedtls_ssl_conf_rng(&mbedtls_ctx->conf, tls_rng_cb, cfg->crypto_ctx);

    if (cfg->ca != NULL) {
        /* Set CA certificate */
        ret = tls_parse_cert(&mbedtls_ctx->ca, cfg->ca, cfg->ca_len,
                             cfg->ca_format);
        if (ret != 0) {
            return ret;
        }
        /* set CA chain */
        mbedtls_ssl_conf_ca_chain(&mbedtls_ctx->conf, &mbedtls_ctx->ca, NULL);
        /* Require the server certificate to chain to the supplied CA. */
        mbedtls_ssl_conf_authmode(&mbedtls_ctx->conf,
                                  MBEDTLS_SSL_VERIFY_REQUIRED);
    } else if (cfg->insecure) {
        LOG_WARN("tls server certificate verification disabled");
        /* Caller explicitly opted out of server certificate verification.
         * Disables authentication; for test scaffolding only. */
        mbedtls_ssl_conf_authmode(&mbedtls_ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
    } else {
        /* No CA and no explicit insecure opt-in: refuse silently disabling
         * certificate verification. */
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    if (cfg->client_cert != NULL) { /* Mutual TLS */
        /* Set client certificate */
        ret = tls_parse_cert(&mbedtls_ctx->client_cert, cfg->client_cert,
                             cfg->client_cert_len, cfg->client_cert_format);
        if (ret != 0) {
            return ret;
        }
        /* Set client private key */
        ret = mbedtls_pk_setup(&mbedtls_ctx->client_key,
                               actrust_tls_get_pk_info());
        if (ret != 0) {
            return ret;
        }
        /* Bind client private key to crypto context */
        ret = actrust_tls_bind_actrust_key(&mbedtls_ctx->client_key,
                                           cfg->crypto_ctx, cfg->client_key);
        if (ret != 0) {
            return ret;
        }
        /* Set client certificate and private key */
        ret = mbedtls_ssl_conf_own_cert(&mbedtls_ctx->conf,
                                        &mbedtls_ctx->client_cert,
                                        &mbedtls_ctx->client_key);
        if (ret != 0) {
            return ret;
        }
    }
    /* Setup mbedTLS SSL context */
    ret = mbedtls_ssl_setup(&mbedtls_ctx->ssl, &mbedtls_ctx->conf);
    if (ret != 0) {
        return ret;
    }

    /* Set hostname */
    ret = mbedtls_ssl_set_hostname(&mbedtls_ctx->ssl, cfg->host);
    if (ret != 0) {
        return ret;
    }

    /* Set network I/O callbacks */
    mbedtls_ssl_set_bio(&mbedtls_ctx->ssl, mbedtls_ctx, tls_net_send_cb,
                        tls_net_recv_cb, NULL);
    return 0;
}

static int tls_backend_do_handshake(tls_mbedtls_ctx_t          *mbedtls_ctx,
                                    const actrust_tls_config_t *cfg)
{
    if (mbedtls_ctx == NULL || cfg == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    int ret = 0;
    while ((ret = mbedtls_ssl_handshake(&mbedtls_ctx->ssl)) != 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
            ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            /* Net callback timeout controls how long this retry loop runs. */
            continue;
        }
        return ret;
    }

    if (cfg->ca != NULL) {
        uint32_t verify_flags =
            mbedtls_ssl_get_verify_result(&mbedtls_ctx->ssl);
        if (verify_flags != 0u) {
            return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        }
    }

    return 0;
}

static actrust_err_t tls_backend_connect(actrust_tls_t               tls,
                                         const actrust_tls_config_t *cfg,
                                         uint32_t                    timeout_ms)
{
    if (tls == NULL || cfg == NULL || cfg->host == NULL || cfg->port == 0u ||
        cfg->crypto_ctx == NULL) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if ((cfg->ca == NULL && cfg->ca_len > 0u) ||
        (cfg->ca != NULL && cfg->ca_len == 0u) ||
        (cfg->client_cert == NULL && cfg->client_cert_len > 0u) ||
        (cfg->client_cert != NULL && cfg->client_cert_len == 0u) ||
        ((cfg->client_cert == NULL) != (cfg->client_key == NULL))) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    tls_mbedtls_ctx_t *mbedtls_ctx = ACTRUST_CALLOC(1, sizeof(*mbedtls_ctx));
    if (mbedtls_ctx == NULL) {
        return TLS_ERR(ACTRUST_ERR_NO_MEM);
    }

    int ret = tls_backend_init_ctx(mbedtls_ctx, tls, cfg, timeout_ms);
    if (ret != 0) {
        actrust_err_t err = tls_map_mbedtls_err(ret);
        LOG_ERROR("tls backend init failed: mbedtls=-0x%04X err=0x%08" PRIx32,
                  (unsigned int) -ret, err);
        tls_mbedtls_ctx_free(mbedtls_ctx);
        ACTRUST_FREE(mbedtls_ctx);
        return err;
    }

    ret = tls_backend_do_handshake(mbedtls_ctx, cfg);
    if (ret != 0) {
        actrust_err_t err = tls_map_mbedtls_err(ret);
        LOG_ERROR("tls handshake failed: mbedtls=-0x%04X err=0x%08" PRIx32,
                  (unsigned int) -ret, err);
        tls_mbedtls_ctx_free(mbedtls_ctx);
        ACTRUST_FREE(mbedtls_ctx);
        return err;
    }

    tls->backend_ctx = mbedtls_ctx;
    return ACTRUST_OK;
}

static actrust_err_t tls_backend_close(actrust_tls_t tls, uint32_t timeout_ms)
{
    if (tls == NULL) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    tls_mbedtls_ctx_t *mbedtls_ctx = (tls_mbedtls_ctx_t *) tls->backend_ctx;
    if (mbedtls_ctx == NULL) {
        return ACTRUST_OK;
    }

    mbedtls_ctx->io_timeout_ms = timeout_ms;
    /* close_notify is best-effort: stop on peer-close/EOF/non-retry errors. */
    for (;;) {
        int ret = mbedtls_ssl_close_notify(&mbedtls_ctx->ssl);
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
            ret == MBEDTLS_ERR_SSL_CONN_EOF) {
            break;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
            ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        break;
    }

    tls_mbedtls_ctx_free(mbedtls_ctx);
    ACTRUST_FREE(mbedtls_ctx);
    tls->backend_ctx = NULL;
    return ACTRUST_OK;
}

static actrust_err_t tls_backend_write(actrust_tls_t tls, const uint8_t *buf,
                                       size_t len, uint32_t timeout_ms,
                                       size_t *bytes_written)
{
    if (tls == NULL || buf == NULL || bytes_written == NULL || len == 0u) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    tls_mbedtls_ctx_t *mbedtls_ctx = (tls_mbedtls_ctx_t *) tls->backend_ctx;
    if (mbedtls_ctx == NULL) {
        return TLS_ERR(ACTRUST_ERR_BAD_STATE);
    }

    mbedtls_ctx->io_timeout_ms = timeout_ms;
    size_t write_len = (len > (size_t) INT_MAX) ? (size_t) INT_MAX : len;

    for (;;) {
        int ret = mbedtls_ssl_write(&mbedtls_ctx->ssl, buf, write_len);
        if (ret > 0) {
            *bytes_written = (size_t) ret;
            return ACTRUST_OK;
        }

        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
            ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }

        if (ret == MBEDTLS_ERR_SSL_CONN_EOF ||
            ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) {
            return TLS_ERR(ACTRUST_ERR_BAD_STATE);
        }

        return tls_map_mbedtls_err(ret);
    }
}

static actrust_err_t tls_backend_read(actrust_tls_t tls, uint8_t *buf,
                                      size_t len, uint32_t timeout_ms,
                                      size_t *bytes_read)
{
    if (tls == NULL || buf == NULL || bytes_read == NULL || len == 0u) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    tls_mbedtls_ctx_t *mbedtls_ctx = (tls_mbedtls_ctx_t *) tls->backend_ctx;
    if (mbedtls_ctx == NULL) {
        return TLS_ERR(ACTRUST_ERR_BAD_STATE);
    }

    mbedtls_ctx->io_timeout_ms = timeout_ms;
    size_t read_len = (len > (size_t) INT_MAX) ? (size_t) INT_MAX : len;

    for (;;) {
        int ret = mbedtls_ssl_read(&mbedtls_ctx->ssl, buf, read_len);
        if (ret > 0) {
            *bytes_read = (size_t) ret;
            return ACTRUST_OK;
        }

        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
            ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }

        if (ret == MBEDTLS_ERR_SSL_CONN_EOF ||
            ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) {
            *bytes_read = 0u;
            return ACTRUST_OK;
        }

        return tls_map_mbedtls_err(ret);
    }
}

static actrust_err_t tls_tcp_connect(actrust_tls_t tls, const char *host,
                                     uint16_t port, uint32_t timeout_ms)
{
    if (tls == NULL || host == NULL || port == 0u) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* Open network socket. */
    actrust_err_t err = actrust_net_open(&tls->net, ACTRUST_NET_TCP);
    if (err != ACTRUST_OK) {
        LOG_WARN("tls tcp socket open failed: err=0x%08" PRIx32, err);
        return err;
    }

    /* Resolve DNS. */
    char remote_ip[16] = { 0 };
    err =
        actrust_net_dns_resolve(host, remote_ip, sizeof(remote_ip), timeout_ms);
    if (err != ACTRUST_OK) {
        LOG_WARN("tls dns resolve failed: port=%u err=0x%08" PRIx32,
                 (unsigned int) port, err);
        return err;
    }

    LOG_DEBUG("tls resolved remote: host=%s ip=%s port=%u", host, remote_ip,
              (unsigned int) port);

    /* Connect to remote host. */
    err = actrust_net_connect(tls->net, remote_ip, port, timeout_ms);
    if (err != ACTRUST_OK) {
        LOG_WARN("tls tcp connect failed: ip=%s port=%u err=0x%08" PRIx32,
                 remote_ip, (unsigned int) port, err);
        return err;
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

actrust_err_t actrust_tls_connect(actrust_tls_t              *out_tls,
                                  const actrust_tls_config_t *cfg,
                                  uint32_t                    timeout_ms)
{
    if (out_tls == NULL || cfg == NULL || cfg->host == NULL ||
        cfg->port == 0u) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *out_tls = NULL;

    LOG_INFO("tls connect requested: port=%u timeout_ms=%lu mtls=%d",
             (unsigned int) cfg->port, (unsigned long) timeout_ms,
             cfg->client_cert != NULL ? 1 : 0);
    LOG_DEBUG("tls connect host: %s", cfg->host);

    actrust_tls_t tls = ACTRUST_CALLOC(1, sizeof(*tls));
    if (tls == NULL) {
        return TLS_ERR(ACTRUST_ERR_NO_MEM);
    }

    actrust_err_t err = tls_tcp_connect(tls, cfg->host, cfg->port, timeout_ms);
    if (err != ACTRUST_OK) {
        LOG_ERROR("tls tcp connect stage failed: err=0x%08" PRIx32, err);
        if (tls->net != NULL) {
            (void) actrust_net_close(tls->net);
        }
        ACTRUST_FREE(tls);
        return err;
    }

    /* Initialize mbedTLS backend and perform TLS handshake. */
    err = tls_backend_connect(tls, cfg, timeout_ms);
    if (err != ACTRUST_OK) {
        (void) actrust_net_close(tls->net);
        ACTRUST_FREE(tls);
        return err;
    }

    *out_tls = tls;
    LOG_INFO("tls connected: port=%u", (unsigned int) cfg->port);
    return ACTRUST_OK;
}

actrust_err_t actrust_tls_close(actrust_tls_t *tls, uint32_t timeout_ms)
{
    if (tls == NULL || *tls == NULL) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = ACTRUST_OK;

    /* Close mbedTLS backend. */
    actrust_err_t backend_err = tls_backend_close(*tls, timeout_ms);
    if (backend_err != ACTRUST_OK) {
        LOG_WARN("tls backend close failed: err=0x%08" PRIx32, backend_err);
        err = backend_err;
    }

    /* Close network socket. */
    actrust_err_t net_err = actrust_net_close((*tls)->net);
    if (net_err != ACTRUST_OK) {
        LOG_WARN("tls socket close failed: err=0x%08" PRIx32, net_err);
        if (ACTRUST_IS_OK(err)) {
            err = net_err;
        }
    }

    LOG_INFO("tls closed");

    /* Release TLS handle regardless of close errors to avoid leaking it. */
    memset(*tls, 0, sizeof(**tls));
    ACTRUST_FREE(*tls);
    *tls = NULL;

    return err;
}

actrust_err_t actrust_tls_write(actrust_tls_t tls, const uint8_t *buf,
                                size_t len, uint32_t timeout_ms,
                                size_t *bytes_written)
{
    if (tls == NULL || buf == NULL || bytes_written == NULL || len == 0u) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* Write data to TLS backend. */
    *bytes_written = 0u;
    return tls_backend_write(tls, buf, len, timeout_ms, bytes_written);
}

actrust_err_t actrust_tls_read(actrust_tls_t tls, uint8_t *buf, size_t len,
                               uint32_t timeout_ms, size_t *bytes_read)
{
    if (tls == NULL || buf == NULL || bytes_read == NULL || len == 0u) {
        return TLS_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* Read data from TLS backend. */
    *bytes_read = 0u;
    return tls_backend_read(tls, buf, len, timeout_ms, bytes_read);
}
