// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <inttypes.h>
#include <stdint.h>

/* Third-party */
#include "unity.h"

/* Log */
#include "log/log.h"

/* Adapter */
#include "adapter/system.h"

#define TEST_TASK_COUNT 4
#define TEST_STACK_SIZE 8192
#define TEST_PRIORITY   1

static int             s_log_info      = 0;
static int             s_log_debug     = 0;
static int             s_log_warn      = 0;
static int             s_log_error     = 0;
static actrust_mutex_t s_counter_mutex = NULL;

static actrust_err_t lifecycle_log_init(void)
{
    actrust_err_t err = actrust_lifecycle_lock();
    if (err != ACTRUST_OK) {
        return err;
    }

    err                  = actrust_log_init();
    actrust_err_t unlock = actrust_lifecycle_unlock();
    return err != ACTRUST_OK ? err : unlock;
}

static actrust_err_t lifecycle_log_deinit(void)
{
    actrust_err_t err = actrust_lifecycle_lock();
    if (err != ACTRUST_OK) {
        return err;
    }

    err                  = actrust_log_deinit();
    actrust_err_t unlock = actrust_lifecycle_unlock();
    return err != ACTRUST_OK ? err : unlock;
}

static int next_counter(int *counter)
{
    int value = 0;
    (void) actrust_mutex_lock(s_counter_mutex);
    value = (*counter)++;
    (void) actrust_mutex_unlock(s_counter_mutex);
    return value;
}

static void log_task_entry(void *arg)
{
    const int task_id = (int) (intptr_t) arg;

    LOG_INFO("[%d] Task %d started", next_counter(&s_log_info), task_id);
    actrust_sleep_ms(10);

    LOG_DEBUG("[%d] Task %d debug message with value: %d",
              next_counter(&s_log_debug), task_id, task_id * 100);
    actrust_sleep_ms(10);

    LOG_WARN("[%d] Task %d warning: operation may be slow",
             next_counter(&s_log_warn), task_id);
    actrust_sleep_ms(10);

    LOG_ERROR("[%d] Task %d encountered simulated error",
              next_counter(&s_log_error), task_id);
    actrust_sleep_ms(10);

    LOG_INFO("Task %d completed", task_id);
}

void setUp(void)
{
    s_log_info  = 0;
    s_log_debug = 0;
    s_log_warn  = 0;
    s_log_error = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_create(&s_counter_mutex));
}

void tearDown(void)
{
    if (s_counter_mutex != NULL) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_destroy(s_counter_mutex));
        s_counter_mutex = NULL;
    }
}

void test_log_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_init());
}

void test_basic_logging(void)
{
    LOG_ERROR("This is an error message");
    LOG_WARN("This is a warning message");
    LOG_INFO("This is an info message");
    LOG_DEBUG("This is a debug message");

    LOG_INFO("Integer: %" PRId32 ", String: %s, Hex: 0x%08" PRIx32,
             (int32_t) 42, "test", (uint32_t) 0xDEADBEEFu);

    LOG_INFO(NULL);
    LOG_INFO("%s", "");

    LOG_INFO("Long message: %s",
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
             "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
}

void test_concurrent_logging(void)
{
    actrust_task_t tasks[TEST_TASK_COUNT];

    for (int i = 0; i < TEST_TASK_COUNT; i++) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_create(
                                          &tasks[i], "log_test", log_task_entry,
                                          (void *) (intptr_t) i,
                                          TEST_STACK_SIZE, TEST_PRIORITY));
    }

    for (int i = 0; i < TEST_TASK_COUNT; i++) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(tasks[i], 1000u));
    }

#if ACTRUST_LOG_COMPILED_LEVEL >= ACTRUST_LOG_LEVEL_INFO_VALUE
    TEST_ASSERT_EQUAL(TEST_TASK_COUNT, s_log_info);
#else
    TEST_ASSERT_EQUAL(0, s_log_info);
#endif

#if ACTRUST_LOG_COMPILED_LEVEL >= ACTRUST_LOG_LEVEL_DEBUG_VALUE
    TEST_ASSERT_EQUAL(TEST_TASK_COUNT, s_log_debug);
#else
    TEST_ASSERT_EQUAL(0, s_log_debug);
#endif

#if ACTRUST_LOG_COMPILED_LEVEL >= ACTRUST_LOG_LEVEL_WARN_VALUE
    TEST_ASSERT_EQUAL(TEST_TASK_COUNT, s_log_warn);
#else
    TEST_ASSERT_EQUAL(0, s_log_warn);
#endif

#if ACTRUST_LOG_COMPILED_LEVEL >= ACTRUST_LOG_LEVEL_ERROR_VALUE
    TEST_ASSERT_EQUAL(TEST_TASK_COUNT, s_log_error);
#else
    TEST_ASSERT_EQUAL(0, s_log_error);
#endif
}

void test_log_deinit(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_deinit());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_log_init);
    RUN_TEST(test_basic_logging);
    RUN_TEST(test_concurrent_logging);
    RUN_TEST(test_log_deinit);
    return UNITY_END();
}
