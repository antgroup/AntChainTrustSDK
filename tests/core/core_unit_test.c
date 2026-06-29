// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Third-party */
#include "unity.h"

/* Project */
#include "actrust.h"

/* Core */
#include "core/core_internal.h"

/* Adapter */
#include "adapter/system.h"

static actrust_err_t        s_last_result;
static actrust_core_state_t s_last_state;
static int                  s_cb_count;

static void test_callback(actrust_err_t result, actrust_core_state_t state,
                          void *user_data)
{
    (void) user_data;
    s_last_result = result;
    s_last_state  = state;
    s_cb_count++;
}

static bool wait_for_callbacks(int expected_count, uint32_t timeout_ms)
{
    uint32_t waited_ms = 0u;

    while (s_cb_count < expected_count && waited_ms < timeout_ms) {
        actrust_sleep_ms(10u);
        waited_ms += 10u;
    }

    return s_cb_count >= expected_count;
}

void setUp(void)
{
    s_last_result = (actrust_err_t) -1;
    s_last_state  = ACTRUST_CORE_UNINIT;
    s_cb_count    = 0;
}

void tearDown(void)
{
}

void test_set_callback_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_set_callback(NULL, NULL));
}

void test_set_callback_success(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
}

void test_init_deinit_lifecycle(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(1, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_READY, s_last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_DEINIT, s_last_state);
}

void test_repeated_init_deinit_lifecycle(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(1, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(3, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(4, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
}

void test_deinit_without_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    actrust_err_t err = actrust_deinit();
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
}

void test_connect_without_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    actrust_err_t err = actrust_connect();
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
}

void test_publish_without_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    actrust_err_t err = actrust_data_publish("{}", 2);
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
}

void test_publish_rejects_embedded_nul_payload(void)
{
    const char    payload[] = { '{', '}', '\0', 'x', '\0' };
    actrust_err_t err;

    /* Force the lifecycle precondition so this test reaches payload validation.
     */
    core_set_state(ACTRUST_CORE_REGISTERED);
    err = actrust_data_publish(payload, 4u);
    core_set_state(ACTRUST_CORE_UNINIT);

    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

void test_job_queue_dequeue_restores_item_token_on_lock_failure(void)
{
    actrust_job_queue_t q       = { 0 };
    actrust_job_t       job     = { .type = ACTRUST_JOB_INIT };
    actrust_job_t      *out_job = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_init(&q, 1u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_enqueue(&q, &job));

    actrust_mutex_t saved_lock = q.lock;
    q.lock                     = NULL;
    actrust_err_t err          = actrust_job_queue_dequeue(&q, 0u, &out_job);
    q.lock                     = saved_lock;

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_dequeue(&q, 0u, &out_job));
    TEST_ASSERT_EQUAL_PTR(&job, out_job);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
}

void test_init_rejects_oversized_claim_lengths(void)
{
    static const char claim_cert[] = "cert";
    static const char claim_key[]  = "key";

    actrust_config_t config = {
        .claim_cert     = claim_cert,
        .claim_cert_len = SIZE_MAX,
        .claim_key      = claim_key,
        .claim_key_len  = sizeof(claim_key) - 1u,
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    actrust_err_t err = actrust_init(&config);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

void test_deinit_recovers_after_async_init_failure(void)
{
    static const char claim_cert[] = "invalid cert";
    static const char claim_key[]  = "invalid key";

    actrust_config_t config = {
        .claim_cert     = claim_cert,
        .claim_cert_len = sizeof(claim_cert) - 1u,
        .claim_key      = claim_key,
        .claim_key_len  = sizeof(claim_key) - 1u,
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(&config));
    TEST_ASSERT_TRUE(wait_for_callbacks(1, 5000u));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_INIT_FAILED, s_last_state);

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_connect());
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_init(NULL));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_DEINIT, s_last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(3, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_READY, s_last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(4, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_DEINIT, s_last_state);
}

void test_callback_fires_on_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));

    TEST_ASSERT_TRUE(wait_for_callbacks(1, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, 5000u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_set_callback_null);
    RUN_TEST(test_set_callback_success);
    RUN_TEST(test_deinit_without_init);
    RUN_TEST(test_connect_without_init);
    RUN_TEST(test_publish_without_init);
    RUN_TEST(test_publish_rejects_embedded_nul_payload);
    RUN_TEST(test_job_queue_dequeue_restores_item_token_on_lock_failure);
    RUN_TEST(test_init_rejects_oversized_claim_lengths);
    RUN_TEST(test_deinit_recovers_after_async_init_failure);
    RUN_TEST(test_init_deinit_lifecycle);
    RUN_TEST(test_repeated_init_deinit_lifecycle);
    RUN_TEST(test_callback_fires_on_init);
    return UNITY_END();
}
