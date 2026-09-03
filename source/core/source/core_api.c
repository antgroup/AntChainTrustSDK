// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file core_api.c
 * @brief Core public API — async entry points and job submission helpers.
 */

/* C standard */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* Common */
#include "common/common.h"

/* Core */
#include "core/core_internal.h"

/* Component */
#include "log/log.h"

#define CORE_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_CORE, (reason))

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

typedef struct {
    actrust_job_type_t type;
    union {
        struct {
            const actrust_config_t *config;
        } init;
        struct {
            const char *data;
            size_t      data_len;
        } send;
    } params;
} core_job_submit_t;

static actrust_err_t core_copy_init_config(actrust_job_t          *job,
                                           const actrust_config_t *config)
{
    if (config == NULL) {
        return ACTRUST_OK;
    }

    if (config->claim_cert == NULL || config->claim_cert_len == 0u ||
        config->claim_key == NULL || config->claim_key_len == 0u) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (config->claim_cert_len > SIZE_MAX - 1u ||
        config->claim_key_len > SIZE_MAX - 1u) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    job->params.init.claim_cert =
        (char *) ACTRUST_MALLOC(config->claim_cert_len + 1u);
    if (job->params.init.claim_cert == NULL) {
        return CORE_ERR(ACTRUST_ERR_NO_MEM);
    }

    (void) memcpy(job->params.init.claim_cert, config->claim_cert,
                  config->claim_cert_len);
    job->params.init.claim_cert[config->claim_cert_len] = '\0';
    job->params.init.claim_cert_len = config->claim_cert_len;

    job->params.init.claim_key =
        (char *) ACTRUST_MALLOC(config->claim_key_len + 1u);
    if (job->params.init.claim_key == NULL) {
        actrust_secure_free(job->params.init.claim_cert,
                            job->params.init.claim_cert_len);
        job->params.init.claim_cert     = NULL;
        job->params.init.claim_cert_len = 0u;
        return CORE_ERR(ACTRUST_ERR_NO_MEM);
    }

    (void) memcpy(job->params.init.claim_key, config->claim_key,
                  config->claim_key_len);
    job->params.init.claim_key[config->claim_key_len] = '\0';
    job->params.init.claim_key_len                    = config->claim_key_len;
    job->params.init.has_config                       = true;

    return ACTRUST_OK;
}

static actrust_err_t core_copy_send_payload(actrust_job_t *job,
                                            const char *data, size_t data_len)
{
    if (data == NULL || data_len == 0u) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (data_len > SIZE_MAX - 1u) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    job->params.send.payload = (char *) ACTRUST_MALLOC(data_len + 1u);
    if (job->params.send.payload == NULL) {
        return CORE_ERR(ACTRUST_ERR_NO_MEM);
    }

    (void) memcpy(job->params.send.payload, data, data_len);
    job->params.send.payload[data_len] = '\0';
    job->params.send.payload_len       = data_len;

    return ACTRUST_OK;
}

static actrust_err_t core_post_job(const core_job_submit_t *submit)
{
    if (submit == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_core_ctx_t *ctx = core_get_ctx();
    actrust_job_t      *job = NULL;
    actrust_err_t       err = actrust_job_alloc(&ctx->job_pool, &job);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    job->type   = submit->type;
    job->result = ACTRUST_OK;

    if (submit->type == ACTRUST_JOB_INIT) {
        err = core_copy_init_config(job, submit->params.init.config);
        if (ACTRUST_IS_ERR(err)) {
            (void) actrust_job_release(&ctx->job_pool, job);
            return err;
        }
    } else if (submit->type == ACTRUST_JOB_SEND) {
        err = core_copy_send_payload(job, submit->params.send.data,
                                     submit->params.send.data_len);
        if (ACTRUST_IS_ERR(err)) {
            (void) actrust_job_release(&ctx->job_pool, job);
            return err;
        }
    }

    err = actrust_job_queue_enqueue(&ctx->job_queue, job);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_job_release(&ctx->job_pool, job);
        return err;
    }

    return ACTRUST_OK;
}

static actrust_err_t core_submit_job(const core_job_submit_t *submit,
                                     actrust_core_state_t     required_state,
                                     bool allow_registered_state)
{
    actrust_err_t err = actrust_lifecycle_lock();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    actrust_core_ctx_t *ctx = core_get_ctx();
    if (ctx->lock == NULL || ctx->deinit_pending) {
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    err = actrust_mutex_lock(ctx->lock);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_lifecycle_unlock();
        return err;
    }

    bool valid = ctx->state == required_state;
    if (allow_registered_state) {
        valid = valid || ctx->state == ACTRUST_CORE_REGISTERED;
    }

    if (!valid) {
        err = CORE_ERR(ACTRUST_ERR_BAD_STATE);
    } else {
        err = core_post_job(submit);
    }

    (void) actrust_mutex_unlock(ctx->lock);
    (void) actrust_lifecycle_unlock();
    return err;
}

static actrust_err_t core_drain_pending_jobs(actrust_core_ctx_t *ctx)
{
    actrust_err_t first_err = ACTRUST_OK;

    for (;;) {
        actrust_job_t *job = NULL;
        actrust_err_t  err =
            actrust_job_queue_dequeue(&ctx->job_queue, 0u, &job);
        if (ACTRUST_IS_ERR(err)) {
            if (ACTRUST_ERR_MODULE(err) == ACTRUST_ERR_MODULE_CORE &&
                ACTRUST_ERR_CODE(err) == ACTRUST_ERR_BAD_STATE) {
                return first_err;
            }
            return ACTRUST_IS_OK(first_err) ? err : first_err;
        }

        err = actrust_job_release(&ctx->job_pool, job);
        if (ACTRUST_IS_OK(first_err) && ACTRUST_IS_ERR(err)) {
            first_err = err;
        }
    }
}

/* ========================================================================
 * Callback registration
 * ======================================================================== */

actrust_err_t actrust_set_callback(actrust_callback_t cb, void *user_data)
{
    if (cb == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_lifecycle_lock();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    actrust_core_ctx_t *ctx = core_get_ctx();
    if (ctx->lock != NULL || ctx->state != ACTRUST_CORE_UNINIT) {
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    ctx->cb = (actrust_callback_ctx_t){ .fn = cb, .user_data = user_data };
    (void) actrust_lifecycle_unlock();
    return ACTRUST_OK;
}

/* ========================================================================
 * Public async API
 * ======================================================================== */

actrust_err_t actrust_init(const actrust_config_t *config)
{
    actrust_err_t err = actrust_lifecycle_lock();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    actrust_core_ctx_t *ctx = core_get_ctx();
    if (ctx->lock != NULL || ctx->state != ACTRUST_CORE_UNINIT) {
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }
    if (ctx->cb.fn == NULL) {
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_NOT_READY);
    }
    err = actrust_log_init();
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_lifecycle_unlock();
        return err;
    }

    ctx->state = ACTRUST_CORE_INITING;

    bool service_start_failed = false;

    err = actrust_mutex_create(&ctx->lock);
    if (ACTRUST_IS_ERR(err)) {
        goto fail_gate;
    }

    err = actrust_job_pool_init(&ctx->job_pool);
    if (ACTRUST_IS_ERR(err)) {
        goto fail_lock;
    }

    err = actrust_job_queue_init(&ctx->job_queue,
                                 CONFIG_ACTRUST_CORE_JOB_QUEUE_CAPACITY);
    if (ACTRUST_IS_ERR(err)) {
        goto fail_pool;
    }

    ctx->state = ACTRUST_CORE_INITING;

    core_job_submit_t submit = {
        .type               = ACTRUST_JOB_INIT,
        .params.init.config = config,
    };

    err = core_post_job(&submit);
    if (ACTRUST_IS_ERR(err)) {
        goto fail_queue;
    }

    err = core_service_start();
    if (ACTRUST_IS_ERR(err)) {
        service_start_failed = true;
        goto fail_service;
    }

    (void) actrust_lifecycle_unlock();
    LOG_INFO("core service started");
    return ACTRUST_OK;

fail_service:
    (void) actrust_job_queue_close(&ctx->job_queue);
    (void) core_drain_pending_jobs(ctx);
    ctx->state = ACTRUST_CORE_UNINIT;
    goto fail_queue_closed;
fail_queue:
    ctx->state = ACTRUST_CORE_UNINIT;
    (void) actrust_job_queue_close(&ctx->job_queue);
fail_queue_closed:
    (void) actrust_job_queue_deinit(&ctx->job_queue);
fail_pool:
    (void) actrust_job_pool_deinit(&ctx->job_pool);
fail_lock:
    (void) actrust_mutex_destroy(ctx->lock);
    ctx->lock = NULL;
fail_gate:
    ctx->state = ACTRUST_CORE_UNINIT;
    (void) actrust_lifecycle_unlock();
    if (service_start_failed) {
        LOG_ERROR("core service start failed: 0x%08" PRIx32, err);
    }
    return err;
}

static void core_deinit_clear_finalizing(actrust_core_ctx_t *ctx)
{
    if (ctx->lock != NULL && actrust_mutex_lock(ctx->lock) == ACTRUST_OK) {
        ctx->deinit_finalizing = false;
        (void) actrust_mutex_unlock(ctx->lock);
    }
}

static void core_deinit_restore_state(actrust_core_ctx_t  *ctx,
                                      actrust_core_state_t previous_state)
{
    if (ctx->lock != NULL && actrust_mutex_lock(ctx->lock) == ACTRUST_OK) {
        ctx->state             = previous_state;
        ctx->deinit_pending    = false;
        ctx->deinit_finalizing = false;
        ctx->deinit_job_done   = false;
        ctx->deinit_result     = ACTRUST_OK;
        ctx->deinit_phase      = ACTRUST_CORE_DEINIT_PHASE_JOB;
        (void) actrust_mutex_unlock(ctx->lock);
    }
}

actrust_err_t actrust_deinit(void)
{
    LOG_INFO("core deinit requested");

    actrust_err_t err = actrust_lifecycle_lock();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    actrust_core_ctx_t *ctx = core_get_ctx();
    if (ctx->lock == NULL) {
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    err = actrust_mutex_lock(ctx->lock);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_lifecycle_unlock();
        return err;
    }

    if (ctx->callback_active || ctx->deinit_finalizing) {
        (void) actrust_mutex_unlock(ctx->lock);
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    bool                 new_deinit     = false;
    actrust_core_state_t previous_state = ACTRUST_CORE_READY;
    if (ctx->state == ACTRUST_CORE_READY ||
        ctx->state == ACTRUST_CORE_INIT_FAILED) {
        previous_state       = ctx->state;
        ctx->deinit_pending  = true;
        ctx->deinit_job_done = false;
        ctx->deinit_result   = ACTRUST_OK;
        ctx->deinit_phase    = ACTRUST_CORE_DEINIT_PHASE_JOB;
        ctx->state           = ACTRUST_CORE_DEINIT;
        new_deinit           = true;
    } else if (ctx->state != ACTRUST_CORE_DEINIT || !ctx->deinit_pending) {
        (void) actrust_mutex_unlock(ctx->lock);
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    ctx->deinit_finalizing = true;
    bool submit_job =
        ctx->deinit_phase == ACTRUST_CORE_DEINIT_PHASE_JOB &&
        (new_deinit ||
         (ctx->service_task == NULL &&
          (!ctx->deinit_job_done || ACTRUST_IS_ERR(ctx->deinit_result))));
    bool start_service = submit_job && ctx->service_task == NULL;
    (void) actrust_mutex_unlock(ctx->lock);
    (void) actrust_lifecycle_unlock();

    if (submit_job) {
        core_job_submit_t submit = {
            .type = ACTRUST_JOB_DEINIT,
        };
        err = core_post_job(&submit);
        if (ACTRUST_IS_ERR(err)) {
            core_deinit_restore_state(ctx, previous_state);
            return err;
        }
        if (start_service) {
            err = core_service_start();
            if (ACTRUST_IS_ERR(err)) {
                (void) core_drain_pending_jobs(ctx);
                core_deinit_restore_state(ctx, previous_state);
                return err;
            }
        }
    }

    if (ctx->deinit_phase == ACTRUST_CORE_DEINIT_PHASE_JOB) {
        err = core_service_stop();
        if (ACTRUST_IS_ERR(err)) {
            core_deinit_clear_finalizing(ctx);
            return err;
        }

        err = actrust_lifecycle_lock();
        if (ACTRUST_IS_ERR(err)) {
            core_deinit_clear_finalizing(ctx);
            return err;
        }
        if (ctx->lock == NULL) {
            (void) actrust_lifecycle_unlock();
            return CORE_ERR(ACTRUST_ERR_BAD_STATE);
        }
        err = actrust_mutex_lock(ctx->lock);
        if (ACTRUST_IS_ERR(err)) {
            core_deinit_clear_finalizing(ctx);
            (void) actrust_lifecycle_unlock();
            return err;
        }

        if (!ctx->deinit_job_done) {
            ctx->deinit_finalizing = false;
            (void) actrust_mutex_unlock(ctx->lock);
            (void) actrust_lifecycle_unlock();
            return CORE_ERR(ACTRUST_ERR_BAD_STATE);
        }
        if (ACTRUST_IS_ERR(ctx->deinit_result)) {
            err                    = ctx->deinit_result;
            ctx->deinit_finalizing = false;
            (void) actrust_mutex_unlock(ctx->lock);
            (void) actrust_lifecycle_unlock();
            return err;
        }

        ctx->deinit_phase = ACTRUST_CORE_DEINIT_PHASE_QUEUE;
        (void) actrust_mutex_unlock(ctx->lock);
        (void) actrust_lifecycle_unlock();
    }

    err = actrust_lifecycle_lock();
    if (ACTRUST_IS_ERR(err)) {
        core_deinit_clear_finalizing(ctx);
        return err;
    }
    if (ctx->lock == NULL) {
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }
    err = actrust_mutex_lock(ctx->lock);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_lifecycle_unlock();
        return err;
    }

    if (ctx->deinit_phase == ACTRUST_CORE_DEINIT_PHASE_QUEUE) {
        err = actrust_job_queue_close(&ctx->job_queue);
        if (ACTRUST_IS_OK(err)) {
            err = core_drain_pending_jobs(ctx);
        }
        if (ACTRUST_IS_OK(err)) {
            err = actrust_job_queue_deinit(&ctx->job_queue);
        }
        if (ACTRUST_IS_ERR(err)) {
            ctx->deinit_finalizing = false;
            (void) actrust_mutex_unlock(ctx->lock);
            (void) actrust_lifecycle_unlock();
            return err;
        }
        ctx->deinit_phase = ACTRUST_CORE_DEINIT_PHASE_POOL;
    }

    if (ctx->deinit_phase == ACTRUST_CORE_DEINIT_PHASE_POOL) {
        err = actrust_job_pool_deinit(&ctx->job_pool);
        if (ACTRUST_IS_ERR(err)) {
            ctx->deinit_finalizing = false;
            (void) actrust_mutex_unlock(ctx->lock);
            (void) actrust_lifecycle_unlock();
            return err;
        }
        ctx->deinit_phase = ACTRUST_CORE_DEINIT_PHASE_LOCK;
    }

    if (ctx->deinit_phase == ACTRUST_CORE_DEINIT_PHASE_LOCK) {
        actrust_mutex_t core_lock = ctx->lock;
        (void) actrust_mutex_unlock(ctx->lock);
        err = actrust_mutex_destroy(core_lock);
        if (ACTRUST_IS_ERR(err)) {
            core_deinit_clear_finalizing(ctx);
            (void) actrust_lifecycle_unlock();
            return err;
        }

        ctx->lock              = NULL;
        ctx->state             = ACTRUST_CORE_UNINIT;
        ctx->deinit_pending    = false;
        ctx->deinit_finalizing = false;
        ctx->deinit_job_done   = false;
        ctx->deinit_result     = ACTRUST_OK;
        ctx->deinit_phase      = ACTRUST_CORE_DEINIT_PHASE_JOB;
    }

    (void) actrust_lifecycle_unlock();
    LOG_INFO("core deinitialized");
    return ACTRUST_OK;
}

actrust_err_t actrust_connect(void)
{
    core_job_submit_t submit = {
        .type = ACTRUST_JOB_CONNECT,
    };

    return core_submit_job(&submit, ACTRUST_CORE_READY, false);
}

actrust_err_t actrust_disconnect(void)
{
    core_job_submit_t submit = {
        .type = ACTRUST_JOB_DISCONNECT,
    };

    return core_submit_job(&submit, ACTRUST_CORE_UNREGISTERED, true);
}

actrust_err_t actrust_register(void)
{
    core_job_submit_t submit = {
        .type = ACTRUST_JOB_REGISTER,
    };

    return core_submit_job(&submit, ACTRUST_CORE_UNREGISTERED, false);
}

actrust_err_t actrust_data_publish(const char *data, size_t data_len)
{
    if (data == NULL || data_len == 0) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (memchr(data, '\0', data_len) != NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    core_job_submit_t submit = {
        .type                 = ACTRUST_JOB_SEND,
        .params.send.data     = data,
        .params.send.data_len = data_len,
    };

    return core_submit_job(&submit, ACTRUST_CORE_REGISTERED, false);
}
