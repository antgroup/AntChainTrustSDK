// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Third-party */
#include "unity.h"

/* Project */
#include "actrust.h"

/* Core */
#include "core/core_internal.h"

/* Adapter */
#include "adapter/system.h"

#define TEST_JOB_QUEUE_TIMEOUT_MS 50u
#define TEST_WORKER_TIMEOUT_MS    5000u
#define TEST_WORKER_COUNT         2u

typedef struct {
    actrust_sem_t ready;
    actrust_sem_t start;
    actrust_err_t result;
} core_api_worker_t;

static actrust_err_t        s_last_result;
static actrust_core_state_t s_last_state;
static int                  s_callbacks_seen;
static actrust_sem_t        s_callback_sem;
static bool                 s_deinit_from_callback;
static actrust_err_t        s_callback_deinit_result;

static void test_callback(actrust_err_t result, actrust_core_state_t state,
                          void *user_data)
{
    (void) user_data;
    s_last_result = result;
    s_last_state  = state;
    if (s_deinit_from_callback) {
        s_callback_deinit_result = actrust_deinit();
    }
    if (s_callback_sem != NULL) {
        (void) actrust_sem_post(s_callback_sem);
    }
}

static bool wait_for_callbacks(int expected_count, uint32_t timeout_ms)
{
    while (s_callbacks_seen < expected_count) {
        if (ACTRUST_IS_ERR(actrust_sem_wait(s_callback_sem, timeout_ms))) {
            return false;
        }
        s_callbacks_seen++;
    }
    return true;
}

static void init_worker(void *arg)
{
    core_api_worker_t *worker = (core_api_worker_t *) arg;

    worker->result = actrust_sem_post(worker->ready);
    if (ACTRUST_IS_OK(worker->result)) {
        worker->result = actrust_sem_wait(worker->start, UINT32_MAX);
    }
    if (ACTRUST_IS_OK(worker->result)) {
        worker->result = actrust_init(NULL);
    }
}

static void deinit_worker(void *arg)
{
    core_api_worker_t *worker = (core_api_worker_t *) arg;

    worker->result = actrust_sem_post(worker->ready);
    if (ACTRUST_IS_OK(worker->result)) {
        worker->result = actrust_sem_wait(worker->start, UINT32_MAX);
    }
    if (ACTRUST_IS_OK(worker->result)) {
        worker->result = actrust_deinit();
    }
}

static actrust_err_t run_api_workers(core_api_worker_t *workers,
                                     actrust_task_t    *tasks,
                                     void (*entry)(void *))
{
    actrust_sem_t ready = NULL;
    actrust_sem_t start = NULL;
    actrust_err_t err   = actrust_sem_create(&ready, 0u);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = actrust_sem_create(&start, 0u);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_sem_destroy(ready);
        return err;
    }

    for (size_t i = 0u; i < TEST_WORKER_COUNT; ++i) {
        workers[i].ready = ready;
        workers[i].start = start;
        err = actrust_task_create(&tasks[i], "core_api", entry, &workers[i], 0u,
                                  0u);
        if (ACTRUST_IS_ERR(err)) {
            break;
        }
    }

    for (size_t i = 0u; i < TEST_WORKER_COUNT && ACTRUST_IS_OK(err); ++i) {
        err = actrust_sem_wait(ready, TEST_WORKER_TIMEOUT_MS);
    }
    for (size_t i = 0u; i < TEST_WORKER_COUNT; ++i) {
        (void) actrust_sem_post(start);
    }
    for (size_t i = 0u; i < TEST_WORKER_COUNT; ++i) {
        if (tasks[i] != NULL) {
            actrust_err_t join_err = actrust_task_join(tasks[i], UINT32_MAX);
            if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(join_err)) {
                err = join_err;
            }
        }
    }

    (void) actrust_sem_destroy(start);
    (void) actrust_sem_destroy(ready);
    return err;
}

void setUp(void)
{
    s_last_result            = (actrust_err_t) -1;
    s_last_state             = ACTRUST_CORE_UNINIT;
    s_callbacks_seen         = 0;
    s_callback_sem           = NULL;
    s_deinit_from_callback   = false;
    s_callback_deinit_result = ACTRUST_OK;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&s_callback_sem, 0u));
}

void tearDown(void)
{
    if (s_callback_sem != NULL) {
        (void) actrust_sem_destroy(s_callback_sem);
        s_callback_sem = NULL;
    }
}

void test_concurrent_init_admits_one_caller(void)
{
    core_api_worker_t workers[TEST_WORKER_COUNT] = { 0 };
    actrust_task_t    tasks[TEST_WORKER_COUNT]   = { NULL };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, run_api_workers(workers, tasks, init_worker));

    unsigned int success_count   = 0u;
    unsigned int bad_state_count = 0u;
    for (size_t i = 0u; i < TEST_WORKER_COUNT; ++i) {
        if (workers[i].result == ACTRUST_OK) {
            success_count++;
        } else if (ACTRUST_ERR_CODE(workers[i].result) ==
                   ACTRUST_ERR_BAD_STATE) {
            bad_state_count++;
        }
    }

    TEST_ASSERT_EQUAL_UINT(1u, success_count);
    TEST_ASSERT_EQUAL_UINT(1u, bad_state_count);
    TEST_ASSERT_TRUE(wait_for_callbacks(1, TEST_WORKER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, TEST_WORKER_TIMEOUT_MS));
}

void test_concurrent_deinit_admits_one_caller(void)
{
    core_api_worker_t workers[TEST_WORKER_COUNT] = { 0 };
    actrust_task_t    tasks[TEST_WORKER_COUNT]   = { NULL };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(1, TEST_WORKER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      run_api_workers(workers, tasks, deinit_worker));

    unsigned int success_count   = 0u;
    unsigned int bad_state_count = 0u;
    for (size_t i = 0u; i < TEST_WORKER_COUNT; ++i) {
        if (workers[i].result == ACTRUST_OK) {
            success_count++;
        } else if (ACTRUST_ERR_CODE(workers[i].result) ==
                   ACTRUST_ERR_BAD_STATE) {
            bad_state_count++;
        }
    }

    TEST_ASSERT_EQUAL_UINT(1u, success_count);
    TEST_ASSERT_EQUAL_UINT(1u, bad_state_count);
    TEST_ASSERT_TRUE(wait_for_callbacks(2, TEST_WORKER_TIMEOUT_MS));
}

void test_set_callback_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_set_callback(NULL, NULL));
}

void test_set_callback_success(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
}

void test_init_deinit_lifecycle(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(1, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_READY, s_last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_DEINIT, s_last_state);
}

void test_repeated_init_deinit_lifecycle(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(1, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(3, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(4, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
}

void test_deinit_without_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    actrust_err_t err = actrust_deinit();
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
}

void test_connect_without_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    actrust_err_t err = actrust_connect();
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
}

void test_publish_without_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    actrust_err_t err = actrust_data_publish("{}", 2);
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
}

void test_publish_rejects_embedded_nul_payload(void)
{
    const char    payload[] = { '{', '}', '\0', 'x', '\0' };
    actrust_err_t err;

    /* Force the lifecycle precondition so this test reaches payload validation.
     */
    core_set_state(ACTRUST_CORE_REGISTERED);
    err = actrust_data_publish(payload, 4u);
    core_set_state(ACTRUST_CORE_UNINIT);

    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

typedef struct {
    actrust_job_queue_t *queue;
    actrust_job_t       *job;
    actrust_err_t        result;
} job_queue_waiter_t;

static void job_queue_waiter(void *arg)
{
    job_queue_waiter_t *waiter = (job_queue_waiter_t *) arg;
    waiter->result =
        actrust_job_queue_dequeue(waiter->queue, UINT32_MAX, &waiter->job);
}

static bool wait_for_job_queue_waiter(actrust_job_queue_t *q,
                                      uint32_t             timeout_ms)
{
    uint32_t elapsed_ms = 0u;

    while (elapsed_ms < timeout_ms) {
        if (actrust_mutex_lock(q->lock) != ACTRUST_OK) {
            return false;
        }
        bool waiting = q->waiter_count > 0u;
        (void) actrust_mutex_unlock(q->lock);
        if (waiting) {
            return true;
        }
        actrust_sleep_ms(1u);
        elapsed_ms++;
    }

    return false;
}

void test_job_queue_close_rejects_enqueue(void)
{
    actrust_job_queue_t q   = { 0 };
    actrust_job_t       job = { .type = ACTRUST_JOB_CONNECT };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_init(&q, 1u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_close(&q));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE,
                      ACTRUST_ERR_CODE(actrust_job_queue_enqueue(&q, &job)));
    TEST_ASSERT_EQUAL_UINT16(0u, q.count);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
}

void test_job_queue_close_drains_queued_jobs(void)
{
    actrust_job_queue_t q      = { 0 };
    actrust_job_t       jobs[] = {
        { .type = ACTRUST_JOB_CONNECT },
        { .type = ACTRUST_JOB_REGISTER },
    };
    actrust_job_t *out_job = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_init(&q, 2u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_enqueue(&q, &jobs[0]));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_enqueue(&q, &jobs[1]));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_close(&q));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_dequeue(&q, 0u, &out_job));
    TEST_ASSERT_EQUAL_PTR(&jobs[0], out_job);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_dequeue(&q, 0u, &out_job));
    TEST_ASSERT_EQUAL_PTR(&jobs[1], out_job);
    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_BAD_STATE,
        ACTRUST_ERR_CODE(actrust_job_queue_dequeue(&q, 0u, &out_job)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
}

void test_job_queue_close_wakes_blocked_dequeue(void)
{
    actrust_job_queue_t q      = { 0 };
    actrust_task_t      task   = NULL;
    job_queue_waiter_t  waiter = { .queue  = &q,
                                   .job    = NULL,
                                   .result = ACTRUST_OK };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_init(&q, 1u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&task, "job_wait", job_queue_waiter,
                                          &waiter, 0u, 0u));
    TEST_ASSERT_TRUE(wait_for_job_queue_waiter(&q, TEST_WORKER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_close(&q));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_join(task, TEST_WORKER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE, ACTRUST_ERR_CODE(waiter.result));
    TEST_ASSERT_NULL(waiter.job);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
}

void test_job_queue_close_wakes_multiple_dequeues(void)
{
    actrust_job_queue_t q          = { 0 };
    actrust_task_t      tasks[2]   = { NULL };
    job_queue_waiter_t  waiters[2] = {
        { .queue = &q, .job = NULL, .result = ACTRUST_OK },
        { .queue = &q, .job = NULL, .result = ACTRUST_OK },
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_init(&q, 1u));
    for (size_t i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_create(&tasks[i], "job_wait",
                                                          job_queue_waiter,
                                                          &waiters[i], 0u, 0u));
    }

    uint32_t elapsed_ms = 0u;
    for (;;) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_lock(q.lock));
        bool all_waiting = q.waiter_count == 2u;
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mutex_unlock(q.lock));
        if (all_waiting) {
            break;
        }
        TEST_ASSERT_LESS_THAN(TEST_WORKER_TIMEOUT_MS, elapsed_ms);
        actrust_sleep_ms(1u);
        elapsed_ms++;
    }

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_close(&q));
    for (size_t i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK,
                          actrust_task_join(tasks[i], TEST_WORKER_TIMEOUT_MS));
        TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE,
                          ACTRUST_ERR_CODE(waiters[i].result));
        TEST_ASSERT_NULL(waiters[i].job);
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
}

void test_job_pool_exhaustion_recovers(void)
{
    actrust_job_pool_t pool                                    = { 0 };
    actrust_job_t     *jobs[CONFIG_ACTRUST_CORE_JOB_POOL_SIZE] = { NULL };
    actrust_job_t     *extra                                   = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_pool_init(&pool));
    for (size_t i = 0u; i < CONFIG_ACTRUST_CORE_JOB_POOL_SIZE; ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_alloc(&pool, &jobs[i]));
        TEST_ASSERT_NOT_NULL(jobs[i]);
    }

    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE,
                      ACTRUST_ERR_CODE(actrust_job_alloc(&pool, &extra)));
    TEST_ASSERT_NULL(extra);

    for (size_t i = 0u; i < CONFIG_ACTRUST_CORE_JOB_POOL_SIZE; ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_release(&pool, jobs[i]));
    }
    TEST_ASSERT_EQUAL_INT(CONFIG_ACTRUST_CORE_JOB_POOL_SIZE - 1, pool.free_top);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_alloc(&pool, &extra));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_release(&pool, extra));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_pool_deinit(&pool));
}

void test_job_queue_dequeue_nonblocking_preserves_would_block(void)
{
    actrust_job_queue_t q       = { 0 };
    actrust_job_t      *out_job = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_init(&q, 1u));

    actrust_err_t err = actrust_job_queue_dequeue(&q, 0u, &out_job);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                      ACTRUST_ERR_MODULE(err));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_WOULD_BLOCK, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_close(&q));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
}

void test_job_queue_dequeue_preserves_timeout(void)
{
    actrust_job_queue_t q       = { 0 };
    actrust_job_t      *out_job = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_init(&q, 1u));

    actrust_err_t err =
        actrust_job_queue_dequeue(&q, TEST_JOB_QUEUE_TIMEOUT_MS, &out_job);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                      ACTRUST_ERR_MODULE(err));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_TIMEOUT, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_close(&q));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
}

void test_job_queue_dequeue_restores_item_token_on_lock_failure(void)
{
    actrust_job_queue_t q       = { 0 };
    actrust_job_t       job     = { .type = ACTRUST_JOB_INIT };
    actrust_job_t      *out_job = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_init(&q, 1u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_enqueue(&q, &job));

    actrust_mutex_t saved_lock = q.lock;
    q.lock                     = NULL;
    actrust_err_t err          = actrust_job_queue_dequeue(&q, 0u, &out_job);
    q.lock                     = saved_lock;

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_dequeue(&q, 0u, &out_job));
    TEST_ASSERT_EQUAL_PTR(&job, out_job);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_close(&q));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_job_queue_deinit(&q));
}

void test_init_rejects_oversized_claim_lengths(void)
{
    static const char claim_cert[] = "cert";
    static const char claim_key[]  = "key";

    actrust_config_t config = {
        .claim_cert     = claim_cert,
        .claim_cert_len = SIZE_MAX,
        .claim_key      = claim_key,
        .claim_key_len  = sizeof(claim_key) - 1u,
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    actrust_err_t err = actrust_init(&config);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

void test_deinit_recovers_after_async_init_failure(void)
{
    static const char claim_cert[] = "invalid cert";
    static const char claim_key[]  = "invalid key";

    actrust_config_t config = {
        .claim_cert     = claim_cert,
        .claim_cert_len = sizeof(claim_cert) - 1u,
        .claim_key      = claim_key,
        .claim_key_len  = sizeof(claim_key) - 1u,
    };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(&config));
    TEST_ASSERT_TRUE(wait_for_callbacks(1, 5000u));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_INIT_FAILED, s_last_state);

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_connect());
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_init(NULL));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_DEINIT, s_last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(3, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_READY, s_last_state);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(4, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);
    TEST_ASSERT_EQUAL(ACTRUST_CORE_DEINIT, s_last_state);
}

void test_callback_fires_on_init(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));

    TEST_ASSERT_TRUE(wait_for_callbacks(1, 5000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, s_last_result);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, 5000u));
}

void test_callback_deinit_is_rejected(void)
{
    s_deinit_from_callback = true;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_set_callback(test_callback, NULL));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_init(NULL));
    TEST_ASSERT_TRUE(wait_for_callbacks(1, TEST_WORKER_TIMEOUT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE,
                      ACTRUST_ERR_CODE(s_callback_deinit_result));
    s_deinit_from_callback = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_deinit());
    TEST_ASSERT_TRUE(wait_for_callbacks(2, TEST_WORKER_TIMEOUT_MS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_set_callback_null);
    RUN_TEST(test_set_callback_success);
    RUN_TEST(test_concurrent_init_admits_one_caller);
    RUN_TEST(test_concurrent_deinit_admits_one_caller);
    RUN_TEST(test_deinit_without_init);
    RUN_TEST(test_connect_without_init);
    RUN_TEST(test_publish_without_init);
    RUN_TEST(test_publish_rejects_embedded_nul_payload);
    RUN_TEST(test_job_queue_close_rejects_enqueue);
    RUN_TEST(test_job_queue_close_drains_queued_jobs);
    RUN_TEST(test_job_queue_close_wakes_blocked_dequeue);
    RUN_TEST(test_job_queue_close_wakes_multiple_dequeues);
    RUN_TEST(test_job_pool_exhaustion_recovers);
    RUN_TEST(test_job_queue_dequeue_nonblocking_preserves_would_block);
    RUN_TEST(test_job_queue_dequeue_preserves_timeout);
    RUN_TEST(test_job_queue_dequeue_restores_item_token_on_lock_failure);
    RUN_TEST(test_init_rejects_oversized_claim_lengths);
    RUN_TEST(test_deinit_recovers_after_async_init_failure);
    RUN_TEST(test_init_deinit_lifecycle);
    RUN_TEST(test_repeated_init_deinit_lifecycle);
    RUN_TEST(test_callback_fires_on_init);
    RUN_TEST(test_callback_deinit_is_rejected);
    return UNITY_END();
}
