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

static int s_log_info  = 0;
static int s_log_debug = 0;
static int s_log_warn  = 0;
static int s_log_error = 0;

static void log_task_entry(void *arg)
{
    const int task_id = (int) (intptr_t) arg;

    LOG_INFO("[%d] Task %d started", s_log_info++, task_id);
    actrust_sleep_ms(10);

    LOG_DEBUG("[%d] Task %d debug message with value: %d", s_log_debug++,
              task_id, task_id * 100);
    actrust_sleep_ms(10);

    LOG_WARN("[%d] Task %d warning: operation may be slow", s_log_warn++,
             task_id);
    actrust_sleep_ms(10);

    LOG_ERROR("[%d] Task %d encountered simulated error", s_log_error++,
              task_id);
    actrust_sleep_ms(10);

    LOG_INFO("Task %d completed", task_id);
}

void setUp(void)
{
    s_log_info  = 0;
    s_log_debug = 0;
    s_log_warn  = 0;
    s_log_error = 0;
}

void tearDown(void)
{
}

void test_log_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_log_init());
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

    actrust_sleep_ms(300);

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
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_log_deinit());
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
