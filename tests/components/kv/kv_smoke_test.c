// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* KV */
#include "kv/kv.h"

/* Adapter */
#include "adapter/system.h"

#define ACTRUST_TEST_KV_NAMESPACE     "testns"
#define ACTRUST_TEST_KV_NAMESPACE_LEN 6u
#define ACTRUST_TEST_KV_STORAGE_ID    0x00000001u

void setUp(void)
{
    actrust_kv_init();
}

void tearDown(void)
{
}

void test_kv_basic(void)
{
    static const char *basic_key   = "foo";
    static const char *basic_value = "bar";

    actrust_kv_t kv = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_open(ACTRUST_TEST_KV_NAMESPACE,
                                      ACTRUST_TEST_KV_NAMESPACE_LEN, &kv));

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_set(kv, basic_key, strlen(basic_key),
                                     basic_value, strlen(basic_value)));

    char   buf[16] = { 0 };
    size_t out_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_get(kv, basic_key, strlen(basic_key), buf,
                                     sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL(strlen(basic_value), out_len);
    TEST_ASSERT_EQUAL_MEMORY(basic_value, buf, out_len);

    bool exist = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_exists(kv, basic_key,
                                                    strlen(basic_key), &exist));
    TEST_ASSERT_TRUE(exist);

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_del(kv, basic_key, strlen(basic_key)));
    exist = true;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_exists(kv, basic_key,
                                                    strlen(basic_key), &exist));
    TEST_ASSERT_FALSE(exist);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_close(kv));
}

#define THREAD_COUNT 4
#define ITERATIONS   50
#define VALUE_SIZE   64

typedef struct {
    actrust_kv_t kv;
    int          id;
    int          result;
} thread_ctx_t;

static const char *shared_key   = "shared";
static const char *shared_value = "shared_value";

static void thread_worker(void *arg)
{
    thread_ctx_t *ctx = (thread_ctx_t *) arg;
    char          buf[VALUE_SIZE];
    size_t        out_len = 0;

    for (int i = 0; i < ITERATIONS; ++i) {
        if (actrust_kv_set(ctx->kv, shared_key, strlen(shared_key),
                           shared_value, strlen(shared_value)) != ACTRUST_OK) {
            ctx->result = 1;
            return;
        }
        if (actrust_kv_get(ctx->kv, shared_key, strlen(shared_key), buf,
                           sizeof(buf), &out_len) != ACTRUST_OK) {
            ctx->result = 1;
            return;
        }
    }
    ctx->result = 0;
}

void test_kv_concurrent(void)
{
    actrust_kv_t kv = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_open(ACTRUST_TEST_KV_NAMESPACE,
                                      ACTRUST_TEST_KV_NAMESPACE_LEN, &kv));

    thread_ctx_t   ctxs[THREAD_COUNT];
    actrust_task_t tasks[THREAD_COUNT];

    for (int i = 0; i < THREAD_COUNT; ++i) {
        ctxs[i].kv     = kv;
        ctxs[i].id     = i;
        ctxs[i].result = 1;
        TEST_ASSERT_EQUAL(ACTRUST_OK,
                          actrust_task_create(&tasks[i], "kv_t", thread_worker,
                                              &ctxs[i], 0, 0));
    }

    for (int i = 0; i < THREAD_COUNT; ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(tasks[i], 15000u));
        TEST_ASSERT_EQUAL(0, ctxs[i].result);
    }

    (void) actrust_kv_del(kv, shared_key, strlen(shared_key));
    (void) actrust_kv_close(kv);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_kv_basic);
    RUN_TEST(test_kv_concurrent);
    return UNITY_END();
}
