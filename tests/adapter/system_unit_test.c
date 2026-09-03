// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/system.h"

void setUp(void)
{
}
void tearDown(void)
{
}

/* --- Time --- */

void test_monotonic_increases(void)
{
    uint64_t t1 = actrust_monotonic_ms();
    actrust_sleep_ms(10);
    uint64_t t2 = actrust_monotonic_ms();
    TEST_ASSERT_GREATER_THAN(t1, t2);
}

void test_wall_time_nonzero(void)
{
    uint64_t wt = actrust_wall_time_ms();
    TEST_ASSERT_GREATER_THAN(0u, wt);
}

/* --- Mutex --- */

void test_mutex_create_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mutex_create(NULL));
}

void test_mutex_lock_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mutex_lock(NULL));
}

void test_mutex_unlock_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mutex_unlock(NULL));
}

void test_mutex_destroy_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mutex_destroy(NULL));
}

void test_mutex_recursive_lock(void)
{
    actrust_mutex_t m = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_create(&m));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_lock(m));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_lock(m));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_unlock(m));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_unlock(m));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_destroy(m));
}

/* --- Lifecycle gate --- */

#define LIFECYCLE_GATE_WORKER_COUNT 2u

typedef struct {
    actrust_sem_t ready;
    actrust_sem_t start;
    actrust_sem_t attempting;
    actrust_sem_t lock_start;
    actrust_sem_t acquired;
    actrust_sem_t release;
    actrust_err_t result;
} lifecycle_gate_worker_ctx_t;

static void lifecycle_gate_worker(void *arg)
{
    lifecycle_gate_worker_ctx_t *ctx = (lifecycle_gate_worker_ctx_t *) arg;

    ctx->result = actrust_sem_post(ctx->ready);
    if (ctx->result != ACTRUST_OK) {
        return;
    }

    ctx->result = actrust_sem_wait(ctx->start, UINT32_MAX);
    if (ctx->result != ACTRUST_OK) {
        return;
    }

    ctx->result = actrust_sem_post(ctx->attempting);
    if (ctx->result != ACTRUST_OK) {
        return;
    }

    ctx->result = actrust_sem_wait(ctx->lock_start, UINT32_MAX);
    if (ctx->result != ACTRUST_OK) {
        return;
    }

    ctx->result = actrust_lifecycle_lock();
    if (ctx->result != ACTRUST_OK) {
        return;
    }

    actrust_err_t err = actrust_sem_post(ctx->acquired);
    if (err == ACTRUST_OK) {
        err = actrust_sem_wait(ctx->release, UINT32_MAX);
    }

    actrust_err_t unlock_err = actrust_lifecycle_unlock();
    ctx->result              = (err != ACTRUST_OK) ? err : unlock_err;
}

void test_lifecycle_gate_serializes_first_acquire(void)
{
    actrust_sem_t               ready                                = NULL;
    actrust_sem_t               start                                = NULL;
    actrust_sem_t               attempting                           = NULL;
    actrust_sem_t               lock_start                           = NULL;
    actrust_sem_t               acquired                             = NULL;
    actrust_sem_t               release                              = NULL;
    actrust_task_t              tasks[LIFECYCLE_GATE_WORKER_COUNT]   = { NULL };
    lifecycle_gate_worker_ctx_t workers[LIFECYCLE_GATE_WORKER_COUNT] = { 0 };
    actrust_err_t               result     = ACTRUST_OK;
    size_t                      started    = 0u;
    bool                        serialized = false;
    bool                        all_joined = true;

    result = actrust_sem_create(&ready, 0u);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }
    result = actrust_sem_create(&start, 0u);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }
    result = actrust_sem_create(&attempting, 0u);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }
    result = actrust_sem_create(&lock_start, 0u);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }
    result = actrust_sem_create(&acquired, 0u);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }
    result = actrust_sem_create(&release, 0u);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }

    for (size_t i = 0u; i < LIFECYCLE_GATE_WORKER_COUNT; ++i) {
        workers[i].ready      = ready;
        workers[i].start      = start;
        workers[i].attempting = attempting;
        workers[i].lock_start = lock_start;
        workers[i].acquired   = acquired;
        workers[i].release    = release;
        result =
            actrust_task_create(&tasks[i], "lifecycle_gate",
                                lifecycle_gate_worker, &workers[i], 0u, 0u);
        if (result != ACTRUST_OK) {
            goto cleanup;
        }
        started++;
    }

    for (size_t i = 0u; i < LIFECYCLE_GATE_WORKER_COUNT; ++i) {
        result = actrust_sem_wait(ready, 1000u);
        if (result != ACTRUST_OK) {
            goto cleanup;
        }
    }
    for (size_t i = 0u; i < LIFECYCLE_GATE_WORKER_COUNT; ++i) {
        result = actrust_sem_post(start);
        if (result != ACTRUST_OK) {
            goto cleanup;
        }
    }
    for (size_t i = 0u; i < LIFECYCLE_GATE_WORKER_COUNT; ++i) {
        result = actrust_sem_wait(attempting, 1000u);
        if (result != ACTRUST_OK) {
            goto cleanup;
        }
    }
    for (size_t i = 0u; i < LIFECYCLE_GATE_WORKER_COUNT; ++i) {
        result = actrust_sem_post(lock_start);
        if (result != ACTRUST_OK) {
            goto cleanup;
        }
    }

    result = actrust_sem_wait(acquired, 1000u);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }

    result = actrust_sem_wait(acquired, 100u);
    if (result ==
        ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM, ACTRUST_ERR_TIMEOUT)) {
        serialized = true;
        result     = ACTRUST_OK;
    } else if (result == ACTRUST_OK) {
        result = ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                             ACTRUST_ERR_BAD_STATE);
        goto cleanup;
    } else {
        goto cleanup;
    }

    result = actrust_sem_post(release);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }
    result = actrust_sem_wait(acquired, 1000u);
    if (result != ACTRUST_OK) {
        goto cleanup;
    }
    result = actrust_sem_post(release);

cleanup:
    if (start != NULL) {
        for (size_t i = 0u; i < started; ++i) {
            (void) actrust_sem_post(start);
        }
    }
    if (lock_start != NULL) {
        for (size_t i = 0u; i < started; ++i) {
            (void) actrust_sem_post(lock_start);
        }
    }
    if (release != NULL) {
        for (size_t i = 0u; i < started; ++i) {
            (void) actrust_sem_post(release);
        }
    }

    for (size_t i = 0u; i < started; ++i) {
        actrust_err_t join_err = actrust_task_join(tasks[i], 1000u);
        if (join_err != ACTRUST_OK) {
            if (result == ACTRUST_OK) {
                result = join_err;
            }
            join_err = actrust_task_join(tasks[i], UINT32_MAX);
        }

        if (join_err != ACTRUST_OK) {
            all_joined = false;
        } else if (workers[i].result != ACTRUST_OK && result == ACTRUST_OK) {
            result = workers[i].result;
        }
    }

    if (all_joined) {
        if (release != NULL) {
            (void) actrust_sem_destroy(release);
        }
        if (acquired != NULL) {
            (void) actrust_sem_destroy(acquired);
        }
        if (lock_start != NULL) {
            (void) actrust_sem_destroy(lock_start);
        }
        if (attempting != NULL) {
            (void) actrust_sem_destroy(attempting);
        }
        if (start != NULL) {
            (void) actrust_sem_destroy(start);
        }
        if (ready != NULL) {
            (void) actrust_sem_destroy(ready);
        }
    }

    TEST_ASSERT_EQUAL(ACTRUST_OK, result);
    TEST_ASSERT_TRUE(serialized);
}

void test_lifecycle_gate_reusable(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_lifecycle_lock());
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_lifecycle_unlock());
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_lifecycle_lock());
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_lifecycle_unlock());
}

/* --- Semaphore --- */

void test_sem_create_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_sem_create(NULL, 0));
}

void test_sem_wait_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_sem_wait(NULL, 0));
}

void test_sem_post_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_sem_post(NULL));
}

void test_sem_destroy_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_sem_destroy(NULL));
}

void test_sem_wait_timeout(void)
{
    actrust_sem_t sem = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&sem, 0u));

    uint64_t      before  = actrust_monotonic_ms();
    actrust_err_t err     = actrust_sem_wait(sem, 50u);
    uint64_t      elapsed = actrust_monotonic_ms() - before;

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
    TEST_ASSERT_GREATER_OR_EQUAL(40u, elapsed);

    actrust_sem_destroy(sem);
}

void test_sem_nonblocking_empty(void)
{
    actrust_sem_t sem = NULL;
    actrust_sem_create(&sem, 0);
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_sem_wait(sem, 0));
    actrust_sem_destroy(sem);
}

/* --- Task --- */

void test_task_create_null_out(void)
{
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK,
        actrust_task_create(NULL, "t", (void (*)(void *)) 0x1, NULL, 0, 0));
}

void test_task_create_null_entry(void)
{
    actrust_task_t t = NULL;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_task_create(&t, "t", NULL, NULL, 0, 0));
}

void test_task_join_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_task_join(NULL, 0));
}

/* --- Log output --- */

void test_log_out_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_log_out(NULL, 0));
}

void test_log_out_zero_len(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_log_out("x", 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_monotonic_increases);
    RUN_TEST(test_wall_time_nonzero);
    RUN_TEST(test_mutex_create_null);
    RUN_TEST(test_mutex_lock_null);
    RUN_TEST(test_mutex_unlock_null);
    RUN_TEST(test_mutex_destroy_null);
    RUN_TEST(test_mutex_recursive_lock);
    RUN_TEST(test_lifecycle_gate_serializes_first_acquire);
    RUN_TEST(test_lifecycle_gate_reusable);
    RUN_TEST(test_sem_create_null);
    RUN_TEST(test_sem_wait_null);
    RUN_TEST(test_sem_post_null);
    RUN_TEST(test_sem_destroy_null);
    RUN_TEST(test_sem_wait_timeout);
    RUN_TEST(test_sem_nonblocking_empty);
    RUN_TEST(test_task_create_null_out);
    RUN_TEST(test_task_create_null_entry);
    RUN_TEST(test_task_join_null);
    RUN_TEST(test_log_out_null);
    RUN_TEST(test_log_out_zero_len);
    return UNITY_END();
}
