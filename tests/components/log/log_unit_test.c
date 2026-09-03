// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Log */
#include "log/log.h"

/* Adapter */
#include "adapter/system.h"

#ifdef ACTRUST_TEST_WRAP_LOG_OUT
extern actrust_err_t __real_actrust_log_out(const char *buf, size_t len);

static bool          s_block_output;
static actrust_sem_t s_output_entered;
static actrust_sem_t s_output_release;

actrust_err_t __wrap_actrust_log_out(const char *buf, size_t len)
{
    if (s_block_output) {
        (void) actrust_sem_post(s_output_entered);
        (void) actrust_sem_wait(s_output_release, UINT32_MAX);
    }
    return __real_actrust_log_out(buf, len);
}
#else
static bool          s_block_output;
static actrust_sem_t s_output_entered;
static actrust_sem_t s_output_release;
#endif

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

void setUp(void)
{
    s_block_output   = false;
    s_output_entered = NULL;
    s_output_release = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_init());
}

void tearDown(void)
{
    s_block_output = false;
    (void) lifecycle_log_deinit();
    if (s_output_release != NULL) {
        (void) actrust_sem_destroy(s_output_release);
        s_output_release = NULL;
    }
    if (s_output_entered != NULL) {
        (void) actrust_sem_destroy(s_output_entered);
        s_output_entered = NULL;
    }
}

void test_init_idempotent(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_init());
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_init());
}

void test_deinit_without_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_deinit());
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, lifecycle_log_deinit());
}

void test_write_without_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_deinit());
    actrust_err_t err = actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__,
                                          __LINE__, __func__, "should fail");
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_init());
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

typedef struct {
    actrust_err_t result;
    actrust_sem_t returned;
} log_worker_t;

static void log_write_worker(void *arg)
{
    log_worker_t *worker = (log_worker_t *) arg;
    worker->result       = actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__,
                                             __LINE__, __func__, "blocked writer");
    (void) actrust_sem_post(worker->returned);
}

static void log_deinit_worker(void *arg)
{
    log_worker_t *worker = (log_worker_t *) arg;
    worker->result       = lifecycle_log_deinit();
    (void) actrust_sem_post(worker->returned);
}

void test_deinit_waits_for_active_writer(void)
{
    actrust_task_t writer_task = NULL;
    actrust_task_t deinit_task = NULL;
    actrust_sem_t  returned    = NULL;
    log_worker_t   writer      = { .result = ACTRUST_OK };
    log_worker_t   deinit      = { .result = ACTRUST_OK };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&s_output_entered, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&s_output_release, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&returned, 0u));
    writer.returned = returned;
    deinit.returned = returned;
    s_block_output  = true;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&writer_task, "log_write",
                                          log_write_worker, &writer, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(s_output_entered, 1000u));

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&deinit_task, "log_deinit",
                                          log_deinit_worker, &deinit, 0u, 0u));
    actrust_sleep_ms(10u);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_WOULD_BLOCK,
                      ACTRUST_ERR_CODE(actrust_sem_wait(returned, 0u)));

    s_block_output = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_post(s_output_release));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(returned, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(returned, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(writer_task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(deinit_task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, writer.result);
    TEST_ASSERT_EQUAL(ACTRUST_OK, deinit.result);
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_log_write(ACTRUST_LOG_LEVEL_INFO, __FILE__,
                                            __LINE__, __func__, "down"));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(returned));
}

void test_repeated_lifecycle(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_deinit());
    for (unsigned int i = 0u; i < 3u; i++) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_init());
        TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_deinit());
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, lifecycle_log_init());
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
    RUN_TEST(test_deinit_waits_for_active_writer);
    RUN_TEST(test_repeated_lifecycle);
    RUN_TEST(test_macros_do_not_crash);
    return UNITY_END();
}
