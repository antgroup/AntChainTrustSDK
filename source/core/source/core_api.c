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

/* ========================================================================
 * Callback registration
 * ======================================================================== */

actrust_err_t actrust_set_callback(actrust_callback_t cb, void *user_data)
{
    if (cb == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    core_set_callback(
        (actrust_callback_ctx_t){ .fn = cb, .user_data = user_data });

    return ACTRUST_OK;
}

/* ========================================================================
 * Public async API
 * ======================================================================== */

actrust_err_t actrust_init(const actrust_config_t *config)
{
    (void) actrust_log_init();
    LOG_INFO("core init requested");

    actrust_core_ctx_t *ctx = core_get_ctx();

    if (core_get_state() != ACTRUST_CORE_UNINIT) {
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (ctx->cb.fn == NULL) {
        return CORE_ERR(ACTRUST_ERR_NOT_READY);
    }

    actrust_err_t err = actrust_mutex_create(&ctx->lock);
    if (ACTRUST_IS_ERR(err)) {
        return err;
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

    core_set_state(ACTRUST_CORE_INITING);

    /* core_post_job allocates a job and enqueues it; on failure it releases
     * the job back to the pool itself, so no extra cleanup is required here. */
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
        LOG_ERROR("core service start failed: 0x%08" PRIx32, err);
        goto fail_service;
    }

    LOG_INFO("core service started");
    return ACTRUST_OK;

fail_service: {
    actrust_job_t *job = NULL;
    while (ACTRUST_IS_OK(actrust_job_queue_dequeue(&ctx->job_queue, 0, &job))) {
        (void) actrust_job_release(&ctx->job_pool, job);
    }
}
fail_queue:
    core_set_state(ACTRUST_CORE_UNINIT);
    (void) actrust_job_queue_deinit(&ctx->job_queue);
fail_pool:
    (void) actrust_job_pool_deinit(&ctx->job_pool);
fail_lock:
    (void) actrust_mutex_destroy(ctx->lock);
    ctx->lock = NULL;
    return err;
}

actrust_err_t actrust_deinit(void)
{
    LOG_INFO("core deinit requested");

    actrust_core_ctx_t *ctx = core_get_ctx();

    /* Atomically claim the deinit slot. Concurrent callers fail here, so we
     * cannot end up running cleanup twice on the same ctx. */
    if (!core_try_acquire_deinit()) {
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    core_job_submit_t submit = {
        .type = ACTRUST_JOB_DEINIT,
    };

    actrust_err_t err = core_post_job(&submit);
    if (ACTRUST_IS_ERR(err)) {
        core_lock();
        ctx->deinit_pending = false;
        core_unlock();
        return err;
    }

    (void) core_service_stop();

    actrust_job_t *job = NULL;
    /* Drain any remaining jobs from the queue and release them back to the
     * pool. */
    while (ACTRUST_IS_OK(actrust_job_queue_dequeue(&ctx->job_queue, 0, &job))) {
        (void) actrust_job_release(&ctx->job_pool, job);
    }

    (void) actrust_job_queue_deinit(&ctx->job_queue);
    (void) actrust_job_pool_deinit(&ctx->job_pool);
    core_lock();
    ctx->deinit_pending = false;
    core_unlock();
    core_set_state(ACTRUST_CORE_UNINIT);
    (void) actrust_mutex_destroy(ctx->lock);
    ctx->lock = NULL;

    LOG_INFO("core deinitialized");
    return ACTRUST_OK;
}

actrust_err_t actrust_connect(void)
{
    actrust_core_state_t state = core_get_state();
    if (state != ACTRUST_CORE_READY) {
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    core_job_submit_t submit = {
        .type = ACTRUST_JOB_CONNECT,
    };

    return core_post_job(&submit);
}

actrust_err_t actrust_disconnect(void)
{
    actrust_core_state_t state = core_get_state();
    if (state != ACTRUST_CORE_UNREGISTERED &&
        state != ACTRUST_CORE_REGISTERED) {
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    core_job_submit_t submit = {
        .type = ACTRUST_JOB_DISCONNECT,
    };

    return core_post_job(&submit);
}

actrust_err_t actrust_register(void)
{
    actrust_core_state_t state = core_get_state();
    if (state != ACTRUST_CORE_UNREGISTERED) {
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    core_job_submit_t submit = {
        .type = ACTRUST_JOB_REGISTER,
    };

    return core_post_job(&submit);
}

actrust_err_t actrust_data_publish(const char *data, size_t data_len)
{
    if (data == NULL || data_len == 0) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_core_state_t state = core_get_state();
    if (state != ACTRUST_CORE_REGISTERED) {
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (memchr(data, '\0', data_len) != NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    core_job_submit_t submit = {
        .type                 = ACTRUST_JOB_SEND,
        .params.send.data     = data,
        .params.send.data_len = data_len,
    };

    return core_post_job(&submit);
}
