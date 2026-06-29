// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stdint.h>

/* Third-party */
#include "unity.h"

/* Queue */
#include "queue/queue.h"

/* Adapter */
#include "adapter/system.h"

void setUp(void)
{
}
void tearDown(void)
{
}

void test_queue_basic_fifo(void)
{
    actrust_queue_t queue = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));

    const int in_values[3] = { 10, 20, 30 };
    for (size_t i = 0; i < 3u; ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK,
                          actrust_queue_push(queue, &in_values[i], 0u));
    }

    for (size_t i = 0; i < 3u; ++i) {
        int out = 0;
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(queue, &out, 0u));
        TEST_ASSERT_EQUAL_INT(in_values[i], out);
    }

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
}

void test_queue_full_empty(void)
{
    actrust_queue_t queue = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 1u, sizeof(uint32_t)));

    uint32_t v = 42u;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &v, 0u));

    actrust_err_t err = actrust_queue_push(queue, &v, 0u);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_QUEUE_FULL, ACTRUST_ERR_CODE(err));

    uint32_t out = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(queue, &out, 0u));
    TEST_ASSERT_EQUAL_UINT32(v, out);

    err = actrust_queue_pop(queue, &out, 0u);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE, ACTRUST_ERR_CODE(err));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
}

typedef struct {
    actrust_queue_t queue;
    actrust_sem_t   done_sem;
    int             result;
} worker_ctx_t;

static void producer_task(void *arg)
{
    worker_ctx_t *ctx = (worker_ctx_t *) arg;
    ctx->result       = 0;
    for (int i = 0; i < 100; ++i) {
        if (actrust_queue_push(ctx->queue, &i, 0xFFFFFFFFu) != ACTRUST_OK) {
            ctx->result = 1;
            break;
        }
    }
    (void) actrust_sem_post(ctx->done_sem);
}

static void consumer_task(void *arg)
{
    worker_ctx_t *ctx = (worker_ctx_t *) arg;
    ctx->result       = 0;
    for (int i = 0; i < 100; ++i) {
        int out = -1;
        if (actrust_queue_pop(ctx->queue, &out, 0xFFFFFFFFu) != ACTRUST_OK) {
            ctx->result = 1;
            break;
        }
        if (out != i) {
            ctx->result = 1;
            break;
        }
    }
    (void) actrust_sem_post(ctx->done_sem);
}

void test_queue_concurrent(void)
{
    actrust_queue_t queue = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 16u, sizeof(int)));

    actrust_sem_t done_sem = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&done_sem, 0u));

    worker_ctx_t   producer             = { queue, done_sem, 1 };
    worker_ctx_t   consumer             = { queue, done_sem, 1 };
    actrust_task_t producer_task_handle = NULL;
    actrust_task_t consumer_task_handle = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&producer_task_handle, "q_prod",
                                          producer_task, &producer, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&consumer_task_handle, "q_cons",
                                          consumer_task, &consumer, 0u, 0u));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(done_sem, 2000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(done_sem, 2000u));

    TEST_ASSERT_EQUAL(0, producer.result);
    TEST_ASSERT_EQUAL(0, consumer.result);

    size_t size = 1u;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_size(queue, &size));
    TEST_ASSERT_EQUAL(0u, size);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(done_sem));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_queue_basic_fifo);
    RUN_TEST(test_queue_full_empty);
    RUN_TEST(test_queue_concurrent);
    return UNITY_END();
}
