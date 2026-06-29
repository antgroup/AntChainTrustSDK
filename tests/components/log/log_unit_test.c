// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <string.h>

/* Third-party */
#include "unity.h"

/* Log */
#include "log/log.h"

void setUp(void)
{
    actrust_log_init();
}

void tearDown(void)
{
    actrust_log_deinit();
}

void test_init_idempotent(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_log_init());
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_log_init());
}

void test_deinit_without_init(void)
{
    actrust_log_deinit();
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_log_deinit());
}

void test_write_without_init(void)
{
    actrust_log_deinit();
    actrust_err_t err = actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__,
                                          __LINE__, __func__, "should fail");
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
    actrust_log_init();
}

void test_write_all_levels(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_log_write(ACTRUST_LOG_LEVEL_ERROR, __FILE__,
                                        __LINE__, __func__, "error msg"));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_log_write(ACTRUST_LOG_LEVEL_WARN, __FILE__,
                                        __LINE__, __func__, "warn msg"));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__,
                                        __LINE__, __func__, "info msg"));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_log_write(ACTRUST_LOG_LEVEL_DEBUG, __FILE__,
                                        __LINE__, __func__, "debug msg"));
}

void test_write_null_fmt(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__,
                                        __LINE__, __func__, NULL));
}

void test_write_empty_fmt(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__,
                                        __LINE__, __func__, "%s", ""));
}

void test_write_null_file_and_func(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_log_write(ACTRUST_LOG_LEVEL_INFO, NULL, 0, NULL,
                                        "null source"));
}

void test_write_long_message_truncated(void)
{
    char big[CONFIG_ACTRUST_LOG_LINE_LEN * 2];
    memset(big, 'X', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__,
                                        __LINE__, __func__, "%s", big));
}

void test_write_format_args(void)
{
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__, __LINE__, __func__,
                          "int=%d str=%s hex=0x%x", 42, "test", 0xBEEF));
}

void test_macros_do_not_crash(void)
{
    LOG_ERROR("macro error %d", 1);
    LOG_WARN("macro warn %d", 2);
    LOG_INFO("macro info %d", 3);
    LOG_DEBUG("macro debug %d", 4);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_idempotent);
    RUN_TEST(test_deinit_without_init);
    RUN_TEST(test_write_without_init);
    RUN_TEST(test_write_all_levels);
    RUN_TEST(test_write_null_fmt);
    RUN_TEST(test_write_empty_fmt);
    RUN_TEST(test_write_null_file_and_func);
    RUN_TEST(test_write_long_message_truncated);
    RUN_TEST(test_write_format_args);
    RUN_TEST(test_macros_do_not_crash);
    return UNITY_END();
}
