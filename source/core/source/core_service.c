// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file core_service.c
 * @brief Core Service — single-threaded job executor and dispatcher.
 */

/* C standard */
#include <stdbool.h>

/* Core */
#include "core/core_internal.h"
#include "core/core_ops.h"

#define CORE_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_CORE, (reason))

static actrust_core_ctx_t g_core;

actrust_core_ctx_t *core_get_ctx(void)
{
    return &g_core;
}

void core_lock(void)
{
    if (g_core.lock != NULL) {
        (void) actrust_mutex_lock(g_core.lock);
    }
}

void core_unlock(void)
{
    if (g_core.lock != NULL) {
        (void) actrust_mutex_unlock(g_core.lock);
    }
}

actrust_core_state_t core_get_state(void)
{
    actrust_core_state_t state;

    core_lock();
    state = g_core.state;
    core_unlock();

    return state;
}

void core_set_state(actrust_core_state_t state)
{
    core_lock();
    g_core.state = state;
    core_unlock();
}

bool core_try_acquire_deinit(void)
{
    core_lock();
    if ((g_core.state != ACTRUST_CORE_READY &&
         g_core.state != ACTRUST_CORE_INIT_FAILED) ||
        g_core.deinit_pending) {
        core_unlock();
        return false;
    }
    g_core.deinit_pending = true;
    core_unlock();
    return true;
}

actrust_callback_ctx_t core_get_callback(void)
{
    actrust_callback_ctx_t cb;

    core_lock();
    cb = g_core.cb;
    core_unlock();

    return cb;
}

void core_set_callback(actrust_callback_ctx_t cb)
{
    core_lock();
    g_core.cb = cb;
    core_unlock();
}

/* ========================================================================
 * Dispatcher
 * ======================================================================== */

static actrust_err_t core_service_dispatch_job(actrust_job_t *job)
{
    switch (job->type) {
        case ACTRUST_JOB_INIT:
            return core_ops_init(job);
        case ACTRUST_JOB_CONNECT:
            return core_ops_connect(job);
        case ACTRUST_JOB_DISCONNECT:
            return core_ops_disconnect(job);
        case ACTRUST_JOB_REGISTER:
            return core_ops_register(job);
        case ACTRUST_JOB_SEND:
            return core_ops_send(job);
        case ACTRUST_JOB_DEINIT:
            return core_ops_deinit(job);
        default:
            return CORE_ERR(ACTRUST_ERR_UNSUPPORTED);
    }
}

static void core_service_complete_job(actrust_job_t *job, actrust_err_t result)
{
    job->result = result;

    actrust_callback_ctx_t cb;
    actrust_core_state_t   state;
    bool                   is_deinit = job->type == ACTRUST_JOB_DEINIT;

    core_lock();
    cb    = g_core.cb;
    state = g_core.state;
    if (is_deinit) {
        g_core.deinit_result   = result;
        g_core.deinit_job_done = true;
    }
    if (cb.fn != NULL) {
        g_core.callback_active = true;
    }
    core_unlock();

    (void) actrust_job_release(&g_core.job_pool, job);

    if (cb.fn != NULL) {
        cb.fn(result, state, cb.user_data);
        core_lock();
        g_core.callback_active = false;
        core_unlock();
    }
}

/* ========================================================================
 * Main loop
 * ======================================================================== */

static void core_service_main(void *arg)
{
    (void) arg;
    g_core.service_result = ACTRUST_OK;

    for (;;) {
        actrust_job_t *job = NULL;
        actrust_err_t  err =
            actrust_job_queue_dequeue(&g_core.job_queue, UINT32_MAX, &job);
        if (ACTRUST_IS_ERR(err)) {
            if (ACTRUST_ERR_MODULE(err) == ACTRUST_ERR_MODULE_CORE &&
                ACTRUST_ERR_CODE(err) == ACTRUST_ERR_BAD_STATE) {
                break;
            }
            g_core.service_result = err;
            break;
        }
        if (job == NULL) {
            continue;
        }

        err            = core_service_dispatch_job(job);
        bool is_deinit = job->type == ACTRUST_JOB_DEINIT;
        core_service_complete_job(job, err);
        if (is_deinit) {
            break;
        }
    }
}

/* ========================================================================
 * Lifecycle
 * ======================================================================== */

actrust_err_t core_service_start(void)
{
    g_core.service_result = ACTRUST_OK;
    actrust_err_t err     = actrust_task_create(
        &g_core.service_task, "core_service", core_service_main, NULL,
        CONFIG_ACTRUST_CORE_SERVICE_STACK_SIZE,
        CONFIG_ACTRUST_CORE_SERVICE_PRIORITY);

    return err;
}

actrust_err_t core_service_stop(void)
{
    if (g_core.service_task == NULL) {
        return ACTRUST_OK;
    }

    actrust_err_t err = actrust_task_join(g_core.service_task, 0xFFFFFFFFu);
    if (ACTRUST_IS_OK(err)) {
        g_core.service_task = NULL;
        err                 = g_core.service_result;
    }

    return err;
}
