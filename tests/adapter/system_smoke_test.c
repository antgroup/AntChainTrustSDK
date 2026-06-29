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

void test_time(void)
{
    uint64_t t1 = actrust_monotonic_ms();
    actrust_sleep_ms(50);
    uint64_t t2 = actrust_monotonic_ms();
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(t1, t2);
    (void) actrust_wall_time_ms();
}

void test_mutex(void)
{
    actrust_mutex_t mtx = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_create(&mtx));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_lock(mtx));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_unlock(mtx));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_destroy(mtx));
}

void test_semaphore(void)
{
    actrust_sem_t sem = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&sem, 1));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(sem, 0));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_post(sem));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(sem));
}

static volatile int s_task_ran;

static void task_entry(void *arg)
{
    int *flag = (int *) arg;
    *flag     = 1;
}

void test_task(void)
{
    s_task_ran          = 0;
    actrust_task_t task = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&task, "smoke", task_entry,
                                          (void *) &s_task_ran, 0, 0));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(task, 1000u));
    TEST_ASSERT_EQUAL(1, s_task_ran);
}

void test_log_out(void)
{
    const char msg[] = "adapter log test\n";
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_log_out(msg, sizeof(msg) - 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_time);
    RUN_TEST(test_mutex);
    RUN_TEST(test_semaphore);
    RUN_TEST(test_task);
    RUN_TEST(test_log_out);
    return UNITY_END();
}
