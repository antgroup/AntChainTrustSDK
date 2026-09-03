// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Queue */
#include "queue/queue.h"

/* Adapter */
#include "adapter/system.h"

#define TEST_TIMEOUT_MS        50u
#define TEST_WORKER_TIMEOUT_MS 1000u

typedef enum {
    TEST_QUEUE_PUSH = 0,
    TEST_QUEUE_POP,
} test_queue_operation_t;

typedef struct {
    actrust_queue_t        queue;
    actrust_sem_t          ready;
    actrust_sem_t          returned;
    test_queue_operation_t operation;
    int                    value;
    actrust_err_t          result;
} queue_waiter_t;

static void queue_waiter_task(void *arg)
{
    queue_waiter_t *waiter = (queue_waiter_t *) arg;

    waiter->result = actrust_sem_post(waiter->ready);
    if (ACTRUST_IS_ERR(waiter->result)) {
        return;
    }

    if (waiter->operation == TEST_QUEUE_PUSH) {
        waiter->result =
            actrust_queue_push(waiter->queue, &waiter->value, UINT32_MAX);
    } else {
        waiter->result =
            actrust_queue_pop(waiter->queue, &waiter->value, UINT32_MAX);
    }

    (void) actrust_sem_post(waiter->returned);
}

static actrust_queue_t queue;

#ifdef ACTRUST_TEST_WRAP_QUEUE_TEARDOWN
static bool          s_fail_sem_destroy;
static bool          s_fail_mutex_destroy;
extern actrust_err_t __real_actrust_sem_destroy(actrust_sem_t sem);
extern actrust_err_t __real_actrust_mutex_destroy(actrust_mutex_t mutex);

actrust_err_t __wrap_actrust_sem_destroy(actrust_sem_t sem)
{
    if (s_fail_sem_destroy == true) {
        s_fail_sem_destroy = false;
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }
    return __real_actrust_sem_destroy(sem);
}

actrust_err_t __wrap_actrust_mutex_destroy(actrust_mutex_t mutex)
{
    if (s_fail_mutex_destroy == true) {
        s_fail_mutex_destroy = false;
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }
    return __real_actrust_mutex_destroy(mutex);
}
#endif

void setUp(void)
{
    queue = NULL;
#ifdef ACTRUST_TEST_WRAP_QUEUE_TEARDOWN
    s_fail_sem_destroy   = false;
    s_fail_mutex_destroy = false;
#endif
}

void tearDown(void)
{
    if (queue != NULL) {
        actrust_queue_destroy(&queue);
    }
}

void test_create_null_handle(void)
{
    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_CODE(actrust_queue_create(NULL, 4u, sizeof(int))),
        ACTRUST_ERR_INVALID_ARG);
}

void test_create_zero_capacity(void)
{
    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_CODE(actrust_queue_create(&queue, 0u, sizeof(int))),
        ACTRUST_ERR_INVALID_ARG);
}

void test_create_zero_item_size(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_create(&queue, 4u, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_create_success(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));
    TEST_ASSERT_NOT_NULL(queue);
}

void test_destroy_null_handle(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_destroy(NULL)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_destroy_null_contents(void)
{
    actrust_queue_t q = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_destroy(&q)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_destroy_sets_null(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 2u, sizeof(int)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
    TEST_ASSERT_NULL(queue);
}

void test_destroy_retries_after_semaphore_failure(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 2u, sizeof(int)));
#ifdef ACTRUST_TEST_WRAP_QUEUE_TEARDOWN
    s_fail_sem_destroy = true;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_HW_FAILURE,
                      ACTRUST_ERR_CODE(actrust_queue_destroy(&queue)));
    TEST_ASSERT_NOT_NULL(queue);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
    TEST_ASSERT_NULL(queue);
#else
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
#endif
}

void test_destroy_retries_after_mutex_failure(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 2u, sizeof(int)));
#ifdef ACTRUST_TEST_WRAP_QUEUE_TEARDOWN
    s_fail_mutex_destroy = true;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_HW_FAILURE,
                      ACTRUST_ERR_CODE(actrust_queue_destroy(&queue)));
    TEST_ASSERT_NOT_NULL(queue);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
    TEST_ASSERT_NULL(queue);
#else
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
#endif
}

void test_close_is_idempotent(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 2u, sizeof(int)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_close(queue));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_close(queue));
}

void test_close_rejects_operations(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 2u, sizeof(int)));

    int value = 7;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &value, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_close(queue));

    size_t size = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE,
                      ACTRUST_ERR_CODE(actrust_queue_push(queue, &value, 0u)));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE,
                      ACTRUST_ERR_CODE(actrust_queue_pop(queue, &value, 0u)));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE,
                      ACTRUST_ERR_CODE(actrust_queue_size(queue, &size)));
}

static void test_close_wakes_waiter(test_queue_operation_t operation)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 1u, sizeof(int)));

    if (operation == TEST_QUEUE_PUSH) {
        int value = 1;
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &value, 0u));
    }

    actrust_sem_t  ready    = NULL;
    actrust_sem_t  returned = NULL;
    actrust_task_t task     = NULL;
    queue_waiter_t waiter   = {
          .queue     = queue,
          .ready     = NULL,
          .returned  = NULL,
          .operation = operation,
          .value     = 2,
          .result    = ACTRUST_OK,
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&ready, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&returned, 0u));
    waiter.ready    = ready;
    waiter.returned = returned;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&task, "queue_wait",
                                          queue_waiter_task, &waiter, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_wait(ready, TEST_WORKER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_WOULD_BLOCK,
                      ACTRUST_ERR_CODE(actrust_sem_wait(returned, 0u)));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_close(queue));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_wait(returned, TEST_WORKER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE, ACTRUST_ERR_CODE(waiter.result));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_join(task, TEST_WORKER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(returned));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(ready));
}

void test_close_wakes_blocked_push(void)
{
    test_close_wakes_waiter(TEST_QUEUE_PUSH);
}

void test_close_wakes_blocked_pop(void)
{
    test_close_wakes_waiter(TEST_QUEUE_POP);
}

void test_destroy_implicitly_closes(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 2u, sizeof(int)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
    TEST_ASSERT_NULL(queue);
}

void test_push_null_queue(void)
{
    int v = 1;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_push(NULL, &v, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_push_null_item(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_push(queue, NULL, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_pop_null_queue(void)
{
    int out;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_pop(NULL, &out, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_pop_null_item(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_pop(queue, NULL, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_pop_empty_nonblocking(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));
    int out = 0;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_pop(queue, &out, 0u)),
                      ACTRUST_ERR_NO_RESOURCE);
}

void test_pop_empty_timeout(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));

    int           out    = 0;
    actrust_err_t err    = actrust_queue_pop(queue, &out, TEST_TIMEOUT_MS);
    uint16_t      module = ACTRUST_ERR_MODULE(err);

    TEST_ASSERT_EQUAL(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM, module);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_TIMEOUT, ACTRUST_ERR_CODE(err));
}

void test_push_full_nonblocking(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 1u, sizeof(int)));
    int v = 42;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &v, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_push(queue, &v, 0u)),
                      ACTRUST_ERR_QUEUE_FULL);
}

void test_push_full_timeout(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 1u, sizeof(int)));

    int v = 42;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &v, 0u));

    actrust_err_t err    = actrust_queue_push(queue, &v, TEST_TIMEOUT_MS);
    uint16_t      module = ACTRUST_ERR_MODULE(err);

    TEST_ASSERT_EQUAL(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM, module);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_TIMEOUT, ACTRUST_ERR_CODE(err));
}

void test_fifo_order(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 8u, sizeof(int)));

    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &i, 0u));
    }

    for (int i = 0; i < 5; ++i) {
        int out = -1;
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(queue, &out, 0u));
        TEST_ASSERT_EQUAL_INT(i, out);
    }
}

void test_wraparound(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 3u, sizeof(int)));

    for (int round = 0; round < 4; ++round) {
        for (int i = 0; i < 3; ++i) {
            int v = round * 10 + i;
            TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &v, 0u));
        }
        for (int i = 0; i < 3; ++i) {
            int out = -1;
            TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(queue, &out, 0u));
            TEST_ASSERT_EQUAL_INT(round * 10 + i, out);
        }
    }
}

void test_size_tracks_push_pop(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));

    size_t sz = 99u;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_size(queue, &sz));
    TEST_ASSERT_EQUAL(0u, sz);

    int v = 1;
    actrust_queue_push(queue, &v, 0u);
    actrust_queue_push(queue, &v, 0u);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_size(queue, &sz));
    TEST_ASSERT_EQUAL(2u, sz);

    actrust_queue_pop(queue, &v, 0u);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_size(queue, &sz));
    TEST_ASSERT_EQUAL(1u, sz);
}

void test_size_null_args(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_size(NULL, NULL)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_large_item_struct(void)
{
    typedef struct {
        uint32_t id;
        char     name[32];
        float    value;
    } big_item_t;

    actrust_queue_t q = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&q, 4u, sizeof(big_item_t)));

    big_item_t in = { .id = 0xDEADBEEF, .name = "hello", .value = 3.14f };
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(q, &in, 0u));

    big_item_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(q, &out, 0u));
    TEST_ASSERT_EQUAL_UINT32(in.id, out.id);
    TEST_ASSERT_EQUAL_STRING(in.name, out.name);
    TEST_ASSERT_EQUAL_FLOAT(in.value, out.value);

    actrust_queue_destroy(&q);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_null_handle);
    RUN_TEST(test_create_zero_capacity);
    RUN_TEST(test_create_zero_item_size);
    RUN_TEST(test_create_success);
    RUN_TEST(test_destroy_null_handle);
    RUN_TEST(test_destroy_null_contents);
    RUN_TEST(test_destroy_sets_null);
    RUN_TEST(test_destroy_retries_after_semaphore_failure);
    RUN_TEST(test_destroy_retries_after_mutex_failure);
    RUN_TEST(test_close_is_idempotent);
    RUN_TEST(test_close_rejects_operations);
    RUN_TEST(test_close_wakes_blocked_push);
    RUN_TEST(test_close_wakes_blocked_pop);
    RUN_TEST(test_destroy_implicitly_closes);
    RUN_TEST(test_push_null_queue);
    RUN_TEST(test_push_null_item);
    RUN_TEST(test_pop_null_queue);
    RUN_TEST(test_pop_null_item);
    RUN_TEST(test_pop_empty_nonblocking);
    RUN_TEST(test_pop_empty_timeout);
    RUN_TEST(test_push_full_nonblocking);
    RUN_TEST(test_push_full_timeout);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_size_tracks_push_pop);
    RUN_TEST(test_size_null_args);
    RUN_TEST(test_large_item_struct);
    return UNITY_END();
}
