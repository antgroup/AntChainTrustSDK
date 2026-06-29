// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
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
