// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Project */
#include "actrust_config.h"

/* Test */
#include "actrust_test.h"

/* TLS */
#include "tls/tls.h"

/* Component */
#include "crypto/crypto.h"

#define FILE_BUF_MAX 4096
#define RESP_BUF_MAX 10240

#define TEST_TLS_TIMEOUT_MS 10000

#define TEST_HTTP_HOST "www.example.com"
#define TEST_HTTP_PORT 443

#define TEST_MTLS_PORT   8883
#define TEST_MTLS_KEY_ID ACTRUST_CRYPTO_KEY_ID_EC_0

#ifndef TEST_MTLS_PKI_DIR
#define TEST_MTLS_PKI_DIR "pki"
#endif
#define TEST_MTLS_CA_PATH          TEST_MTLS_PKI_DIR "/AmazonRootCA.pem"
#define TEST_MTLS_CLIENT_CERT_PATH TEST_MTLS_PKI_DIR "/client.crt"
#define TEST_MTLS_CLIENT_KEY_PATH  TEST_MTLS_PKI_DIR "/client.key"

void setUp(void)
{
}
void tearDown(void)
{
}

static int import_client_key_pem(actrust_crypto_ctx_t crypto, uint32_t key_id,
                                 const char *key_pem, size_t key_pem_len,
                                 actrust_crypto_key_t *out_key)
{
    if (crypto == NULL || key_pem == NULL || key_pem_len == 0u ||
        out_key == NULL) {
        return 1;
    }

    actrust_err_t err = actrust_crypto_key_import(
        crypto, key_id, ACTRUST_CRYPTO_FORMAT_PRIVATE_PEM,
        (const uint8_t *) key_pem, key_pem_len, out_key);
    return (err == ACTRUST_OK) ? 0 : 1;
}

void test_tls_smoke(void)
{
    actrust_crypto_ctx_t crypto = NULL;
    actrust_tls_t        tls    = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_init(&crypto));

    actrust_tls_config_t cfg = {
        .host               = TEST_HTTP_HOST,
        .port               = TEST_HTTP_PORT,
        .crypto_ctx         = crypto,
        .ca                 = NULL,
        .ca_len             = 0u,
        .ca_format          = ACTRUST_TLS_CERT_FORMAT_PEM,
        .insecure           = true,
        .client_cert        = NULL,
        .client_cert_len    = 0u,
        .client_cert_format = ACTRUST_TLS_CERT_FORMAT_PEM,
        .client_key         = NULL,
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_tls_connect(&tls, &cfg, TEST_TLS_TIMEOUT_MS));

    const char req[] = "GET / HTTP/1.1\r\nHost: " TEST_HTTP_HOST
                       "\r\nConnection: close\r\n\r\n";
    size_t written = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_tls_write(tls, (const uint8_t *) req,
                                        sizeof(req) - 1u, TEST_TLS_TIMEOUT_MS,
                                        &written));
    TEST_ASSERT_GREATER_THAN(0u, written);

    uint8_t resp[RESP_BUF_MAX] = { 0 };
    size_t  read_len           = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_tls_read(tls, resp, sizeof(resp),
                                       TEST_TLS_TIMEOUT_MS, &read_len));
    TEST_ASSERT_GREATER_THAN(0u, read_len);

    (void) actrust_tls_close(&tls, 1000u);
    (void) actrust_crypto_deinit(&crypto);
}

void test_tls_mutual_tls(void)
{
    actrust_crypto_ctx_t crypto     = NULL;
    actrust_crypto_key_t client_key = NULL;
    actrust_tls_t        tls        = NULL;

    uint8_t ca_pem[FILE_BUF_MAX];
    size_t  ca_len = sizeof(ca_pem);
    uint8_t client_cert_pem[FILE_BUF_MAX];
    size_t  client_cert_len = sizeof(client_cert_pem);
    char    client_key_pem[FILE_BUF_MAX];
    size_t  client_key_pem_len = sizeof(client_key_pem);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_init(&crypto));

    TEST_ASSERT_EQUAL(0,
                      actrust_test_load_file(TEST_MTLS_CA_PATH, ca_pem,
                                             sizeof(ca_pem), &ca_len, false));
    TEST_ASSERT_EQUAL(0, actrust_test_load_file(
                             TEST_MTLS_CLIENT_CERT_PATH, client_cert_pem,
                             sizeof(client_cert_pem), &client_cert_len, false));
    TEST_ASSERT_EQUAL(0, actrust_test_load_file(TEST_MTLS_CLIENT_KEY_PATH,
                                                client_key_pem,
                                                sizeof(client_key_pem),
                                                &client_key_pem_len, true));
    TEST_ASSERT_EQUAL(
        0, import_client_key_pem(crypto, TEST_MTLS_KEY_ID, client_key_pem,
                                 client_key_pem_len, &client_key));
    TEST_ASSERT_NOT_NULL(client_key);

    actrust_tls_config_t cfg = {
        .host               = CONFIG_ACTRUST_CLOUD_AWS_ENDPOINT,
        .port               = TEST_MTLS_PORT,
        .crypto_ctx         = crypto,
        .ca                 = ca_pem,
        .ca_len             = ca_len,
        .ca_format          = ACTRUST_TLS_CERT_FORMAT_PEM,
        .client_cert        = client_cert_pem,
        .client_cert_len    = client_cert_len,
        .client_cert_format = ACTRUST_TLS_CERT_FORMAT_PEM,
        .client_key         = client_key,
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_tls_connect(&tls, &cfg, TEST_TLS_TIMEOUT_MS));

    const uint8_t req[]   = { 0x10, 0x1A, 0x00, 0x04, 'M',  'Q',  'T',
                              'T',  0x04, 0x02, 0x00, 0x3C, 0x00, 0x0E,
                              't',  'l',  's',  '-',  's',  'm',  'o',
                              'k',  'e',  '-',  't',  'e',  's',  't' };
    size_t        written = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_tls_write(tls, req, sizeof(req),
                                        TEST_TLS_TIMEOUT_MS, &written));
    TEST_ASSERT_GREATER_THAN(0u, written);

    uint8_t resp[RESP_BUF_MAX] = { 0 };
    size_t  read_len           = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_tls_read(tls, resp, sizeof(resp),
                                       TEST_TLS_TIMEOUT_MS, &read_len));
    TEST_ASSERT_GREATER_THAN(0u, read_len);

    (void) actrust_tls_close(&tls, 1000u);
    (void) actrust_crypto_key_close(crypto, &client_key);
    (void) actrust_crypto_key_destroy(crypto, TEST_MTLS_KEY_ID);
    (void) actrust_crypto_deinit(&crypto);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tls_smoke);
    RUN_TEST(test_tls_mutual_tls);
    return UNITY_END();
}
