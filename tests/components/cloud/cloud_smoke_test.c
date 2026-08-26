// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Project */
#include "actrust_config.h"

/* Test */
#include "actrust_test.h"

/* Cloud */
#include "cloud/cloud.h"

/* Component */
#include "crypto/crypto.h"
#include "kv/kv.h"
#include "log/log.h"
#include "queue/queue.h"

/* Adapter */
#include "adapter/system.h"

#define FILE_BUF_MAX 4096u

#ifndef TEST_CLOUD_PKI_DIR
#define TEST_CLOUD_PKI_DIR "pki"
#endif
#define TEST_CLOUD_CLAIM_CERT_PATH TEST_CLOUD_PKI_DIR "/client.crt"
#define TEST_CLOUD_CLAIM_KEY_PATH  TEST_CLOUD_PKI_DIR "/client.key"

void setUp(void)
{
}
void tearDown(void)
{
}

void test_prepare_environment(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_init());

    char   claim_cert_pem[FILE_BUF_MAX];
    size_t claim_cert_len = 0u;

    char   claim_key_pem[FILE_BUF_MAX];
    size_t claim_key_pem_len = 0u;

    TEST_ASSERT_EQUAL(0, actrust_test_load_file(
                             TEST_CLOUD_CLAIM_CERT_PATH, claim_cert_pem,
                             sizeof(claim_cert_pem), &claim_cert_len, false));

    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_crypto_cert_write(ACTRUST_CLOUD_CLAIM_CERT_ID,
                                              (const uint8_t *) claim_cert_pem,
                                              claim_cert_len));

    TEST_ASSERT_EQUAL(0, actrust_test_load_file(
                             TEST_CLOUD_CLAIM_KEY_PATH, claim_key_pem,
                             sizeof(claim_key_pem), &claim_key_pem_len, true));

    actrust_crypto_ctx_t crypto    = NULL;
    actrust_crypto_key_t claim_key = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_init(&crypto));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_import(
                                      crypto, ACTRUST_CLOUD_CLAIM_KEY_ID,
                                      ACTRUST_CRYPTO_FORMAT_PRIVATE_PEM,
                                      (const uint8_t *) claim_key_pem,
                                      claim_key_pem_len, &claim_key));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(crypto, &claim_key));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_deinit(&crypto));
}

static bool g_cloud_test_downlink_running = false;

static void cloud_test_handle_downlink_message(void *arg)
{
    actrust_queue_t downlink_queue = (actrust_queue_t) arg;
    if (downlink_queue == NULL) {
        return;
    }

    while (g_cloud_test_downlink_running == true) {
        actrust_cloud_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        (void) actrust_queue_pop(downlink_queue, &msg, 500u);
    }
}

static const char *reports[] = {
    "{\"temperature\":25,\"humidity\":50}",
    "{\"temperature\":26,\"humidity\":49}",
    "{\"temperature\":27,\"humidity\":48}",
};

void test_cloud_main_flow(void)
{
    actrust_cloud_t cloud = NULL;
    actrust_queue_t downlink_queue;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS,
                                                     &cloud, &downlink_queue));

    g_cloud_test_downlink_running = true;
    actrust_task_t downlink_task;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&downlink_task,
                                          "cloud_handle_downlink_message",
                                          cloud_test_handle_downlink_message,
                                          downlink_queue, 0u, 0u));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_connect(cloud));

    for (size_t i = 0u; i < sizeof(reports) / sizeof(reports[0]); ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_send_data(
                                          cloud, (const uint8_t *) reports[i],
                                          strlen(reports[i])));
        actrust_sleep_ms(3000u);
    }

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_disconnect(cloud));
    g_cloud_test_downlink_running = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(downlink_task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(cloud));
    cloud = NULL;
}

int main(void)
{
    actrust_err_t err = actrust_lifecycle_lock();
    if (err != ACTRUST_OK) {
        return 1;
    }

    err                  = actrust_log_init();
    actrust_err_t unlock = actrust_lifecycle_unlock();
    if (err != ACTRUST_OK || unlock != ACTRUST_OK) {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_prepare_environment);
    RUN_TEST(test_cloud_main_flow);
    return UNITY_END();
}
