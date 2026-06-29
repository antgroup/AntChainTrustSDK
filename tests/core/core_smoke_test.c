// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stddef.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Project */
#include "actrust.h"

/* Test */
#include "actrust_test.h"

/* Adapter */
#include "adapter/system.h"

#define CB_TIMEOUT_MS       10000u
#define CONNECT_TIMEOUT_MS  120000u
#define REGISTER_TIMEOUT_MS 120000u
#define FILE_BUF_MAX        4096u

#ifndef TEST_CORE_PKI_DIR
#define TEST_CORE_PKI_DIR "pki"
#endif
#define TEST_CORE_CLAIM_CERT_PATH TEST_CORE_PKI_DIR "/client.crt"
#define TEST_CORE_CLAIM_KEY_PATH  TEST_CORE_PKI_DIR "/client.key"

typedef struct {
    actrust_sem_t        sem;
    actrust_err_t        last_result;
    actrust_core_state_t last_state;
    uint32_t             cb_count;
} test_ctx_t;

static test_ctx_t g_ctx;

static void test_callback(actrust_err_t result, actrust_core_state_t state,
                          void *user_data)
{
    test_ctx_t *ctx  = (test_ctx_t *) user_data;
    ctx->last_result = result;
    ctx->last_state  = state;
    ctx->cb_count++;
    (void) actrust_sem_post(ctx->sem);
}

void setUp(void)
{
}
void tearDown(void)
{
}

void test_full_lifecycle(void)
{
    char   claim_cert_pem[FILE_BUF_MAX];
    size_t claim_cert_len = 0u;
    char   claim_key_pem[FILE_BUF_MAX];
    size_t claim_key_len = 0u;

    TEST_ASSERT_EQUAL(0, actrust_test_load_file(
                             TEST_CORE_CLAIM_CERT_PATH, claim_cert_pem,
                             sizeof(claim_cert_pem), &claim_cert_len, true));
    TEST_ASSERT_EQUAL(
        0, actrust_test_load_file(TEST_CORE_CLAIM_KEY_PATH, claim_key_pem,
                                  sizeof(claim_key_pem), &claim_key_len, true));

    actrust_config_t config = {
        .claim_cert     = claim_cert_pem,
        .claim_cert_len = claim_cert_len,
        .claim_key      = claim_key_pem,
        .claim_key_len  = claim_key_len,
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(&config));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(g_ctx.sem, CB_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, g_ctx.last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_READY, g_ctx.last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_connect());
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_wait(g_ctx.sem, CONNECT_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, g_ctx.last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_UNREGISTERED, g_ctx.last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_register());
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_wait(g_ctx.sem, REGISTER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, g_ctx.last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_REGISTERED, g_ctx.last_state);

    const char *payload = "{\"temperature\":25.3}";
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_data_publish(payload, strlen(payload)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(g_ctx.sem, CB_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, g_ctx.last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_REGISTERED, g_ctx.last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_disconnect());
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(g_ctx.sem, CB_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, g_ctx.last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_READY, g_ctx.last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(g_ctx.sem, CB_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, g_ctx.last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_DEINIT, g_ctx.last_state);

    TEST_ASSERT_EQUAL_UINT32(6u, g_ctx.cb_count);
}

int main(void)
{
    if (!ACTRUST_IS_OK(actrust_sem_create(&g_ctx.sem, 0))) {
        return 1;
    }
    if (!ACTRUST_IS_OK(actrust_set_callback(test_callback, &g_ctx))) {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_full_lifecycle);
    int rc = UNITY_END();

    (void) actrust_sem_destroy(g_ctx.sem);
    return rc;
}
