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

    (void) actrust_log_init();
    LOG_INFO("core init requested");

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
        LOG_ERROR("core service start failed: 0x%08" PRIx32, err);
        goto fail_service;
    }

    (void) actrust_lifecycle_unlock();
    LOG_INFO("core service started");
    return ACTRUST_OK;

fail_service: {
    actrust_job_t *job = NULL;
    while (
        ACTRUST_IS_OK(actrust_job_queue_dequeue(&ctx->job_queue, 0u, &job))) {
        (void) actrust_job_release(&ctx->job_pool, job);
    }
}
fail_queue:
    ctx->state = ACTRUST_CORE_UNINIT;
    (void) actrust_job_queue_deinit(&ctx->job_queue);
fail_pool:
    (void) actrust_job_pool_deinit(&ctx->job_pool);
fail_lock:
    (void) actrust_mutex_destroy(ctx->lock);
    ctx->lock = NULL;
fail_gate:
    (void) actrust_lifecycle_unlock();
    return err;
}

actrust_err_t actrust_deinit(void)
{
    LOG_INFO("core deinit requested");

    actrust_err_t err = actrust_lifecycle_lock();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    actrust_core_ctx_t *ctx = core_get_ctx();
    if (ctx->lock == NULL || ctx->deinit_pending ||
        (ctx->state != ACTRUST_CORE_READY &&
         ctx->state != ACTRUST_CORE_INIT_FAILED)) {
        (void) actrust_lifecycle_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    err = actrust_mutex_lock(ctx->lock);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_lifecycle_unlock();
        return err;
    }

    ctx->deinit_pending = true;

    core_job_submit_t submit = {
        .type = ACTRUST_JOB_DEINIT,
    };

    err = core_post_job(&submit);
    if (ACTRUST_IS_ERR(err)) {
        ctx->deinit_pending = false;
        (void) actrust_mutex_unlock(ctx->lock);
        (void) actrust_lifecycle_unlock();
        return err;
    }

    (void) actrust_mutex_unlock(ctx->lock);
    (void) actrust_lifecycle_unlock();

    (void) core_service_stop();

    err = actrust_lifecycle_lock();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    actrust_job_t *job = NULL;
    while (
        ACTRUST_IS_OK(actrust_job_queue_dequeue(&ctx->job_queue, 0u, &job))) {
        (void) actrust_job_release(&ctx->job_pool, job);
    }

    (void) actrust_job_queue_deinit(&ctx->job_queue);
    (void) actrust_job_pool_deinit(&ctx->job_pool);
    (void) actrust_mutex_destroy(ctx->lock);
    ctx->lock           = NULL;
    ctx->state          = ACTRUST_CORE_UNINIT;
    ctx->deinit_pending = false;
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
