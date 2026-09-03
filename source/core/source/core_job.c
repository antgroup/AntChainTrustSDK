// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file core_job.c
 * @brief Job entity, JobPool and JobQueue implementation.
 */

/* C standard */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Common */
#include "common/common.h"

/* Core */
#include "core/core_job.h"

#define CORE_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_CORE, (reason))

/* ========================================================================
 * Job Pool
 * ======================================================================== */

actrust_err_t actrust_job_pool_init(actrust_job_pool_t *pool)
{
    if (pool == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    memset(pool->pool, 0, sizeof(pool->pool));

    for (uint16_t i = 0; i < CONFIG_ACTRUST_CORE_JOB_POOL_SIZE; i++) {
        pool->free_indices[i] = i;
    }
    pool->free_top = (int16_t) (CONFIG_ACTRUST_CORE_JOB_POOL_SIZE - 1);

    actrust_err_t err = actrust_mutex_create(&pool->mutex);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    return ACTRUST_OK;
}

actrust_err_t actrust_job_pool_deinit(actrust_job_pool_t *pool)
{
    if (pool == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (pool->mutex == NULL) {
        return ACTRUST_OK;
    }

    actrust_err_t err = actrust_mutex_destroy(pool->mutex);
    if (ACTRUST_IS_OK(err)) {
        pool->mutex = NULL;
    }
    return err;
}

actrust_err_t actrust_job_alloc(actrust_job_pool_t *pool, actrust_job_t **job)
{
    if (pool == NULL || job == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_mutex_lock(pool->mutex);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    if (pool->free_top < 0) {
        (void) actrust_mutex_unlock(pool->mutex);
        return CORE_ERR(ACTRUST_ERR_NO_RESOURCE);
    }

    uint16_t idx = pool->free_indices[pool->free_top];
    pool->free_top--;

    *job = &pool->pool[idx];
    memset(*job, 0, sizeof(**job));
    (*job)->refcnt = 1;

    (void) actrust_mutex_unlock(pool->mutex);
    return ACTRUST_OK;
}

actrust_err_t actrust_job_retain(actrust_job_pool_t *pool, actrust_job_t *job)
{
    if (pool == NULL || job == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_mutex_lock(pool->mutex);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    if (job->refcnt == 0) {
        (void) actrust_mutex_unlock(pool->mutex);
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (job->refcnt == UINT8_MAX) {
        (void) actrust_mutex_unlock(pool->mutex);
        return CORE_ERR(ACTRUST_ERR_NO_RESOURCE);
    }

    job->refcnt++;
    (void) actrust_mutex_unlock(pool->mutex);
    return ACTRUST_OK;
}

actrust_err_t actrust_job_release(actrust_job_pool_t *pool, actrust_job_t *job)
{
    if (pool == NULL || job == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_mutex_lock(pool->mutex);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    if (job->refcnt == 0) {
        (void) actrust_mutex_unlock(pool->mutex);
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    job->refcnt--;

    if (job->refcnt == 0) {
        if (job->type == ACTRUST_JOB_INIT) {
            actrust_secure_free(job->params.init.claim_cert,
                                job->params.init.claim_cert_len);
            actrust_secure_free(job->params.init.claim_key,
                                job->params.init.claim_key_len);
            job->params.init.claim_cert     = NULL;
            job->params.init.claim_cert_len = 0u;
            job->params.init.claim_key      = NULL;
            job->params.init.claim_key_len  = 0u;
            job->params.init.has_config     = false;
        }

        if (job->type == ACTRUST_JOB_SEND && job->params.send.payload != NULL) {
            ACTRUST_FREE(job->params.send.payload);
            job->params.send.payload     = NULL;
            job->params.send.payload_len = 0u;
        }

        job->type   = ACTRUST_JOB_NONE;
        job->result = ACTRUST_OK;

        uint16_t idx = (uint16_t) (job - pool->pool);
        if (idx >= CONFIG_ACTRUST_CORE_JOB_POOL_SIZE) {
            (void) actrust_mutex_unlock(pool->mutex);
            return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
        }
        pool->free_top++;
        pool->free_indices[pool->free_top] = idx;
    }

    (void) actrust_mutex_unlock(pool->mutex);
    return ACTRUST_OK;
}

/* ========================================================================
 * Job Queue
 * ======================================================================== */

actrust_err_t actrust_job_queue_init(actrust_job_queue_t *q, uint16_t depth)
{
    if (q == NULL || depth == 0) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    q->buf = (actrust_job_t **) ACTRUST_MALLOC(depth * sizeof(actrust_job_t *));
    if (q->buf == NULL) {
        return CORE_ERR(ACTRUST_ERR_NO_MEM);
    }

    q->depth              = depth;
    q->head               = 0;
    q->tail               = 0;
    q->count              = 0;
    q->waiter_count       = 0;
    q->close_wake_pending = 0;
    q->closed             = false;

    actrust_err_t err = actrust_mutex_create(&q->lock);
    if (ACTRUST_IS_ERR(err)) {
        ACTRUST_FREE(q->buf);
        q->buf = NULL;
        return err;
    }

    err = actrust_sem_create(&q->sem_items, 0);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_mutex_destroy(q->lock);
        ACTRUST_FREE(q->buf);
        q->buf = NULL;
        return err;
    }

    return ACTRUST_OK;
}

actrust_err_t actrust_job_queue_close(actrust_job_queue_t *q)
{
    if (q == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_mutex_lock(q->lock);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    if (!q->closed) {
        q->closed             = true;
        q->close_wake_pending = q->waiter_count;
    }

    while (q->close_wake_pending > 0u) {
        err = actrust_sem_post(q->sem_items);
        if (ACTRUST_IS_ERR(err)) {
            break;
        }
        q->close_wake_pending--;
    }

    actrust_err_t unlock_err = actrust_mutex_unlock(q->lock);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    return unlock_err;
}

actrust_err_t actrust_job_queue_deinit(actrust_job_queue_t *q)
{
    if (q == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (q->lock == NULL) {
        return (q->buf == NULL && q->sem_items == NULL)
                   ? ACTRUST_OK
                   : CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_err_t err = actrust_mutex_lock(q->lock);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    bool          ready = q->closed && q->count == 0u && q->waiter_count == 0u;
    actrust_err_t unlock_err = actrust_mutex_unlock(q->lock);
    if (ACTRUST_IS_ERR(unlock_err)) {
        return unlock_err;
    }
    if (!ready) {
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (q->sem_items != NULL) {
        err = actrust_sem_destroy(q->sem_items);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        q->sem_items = NULL;
    }

    if (q->lock != NULL) {
        err = actrust_mutex_destroy(q->lock);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        q->lock = NULL;
    }

    ACTRUST_FREE(q->buf);
    q->buf                = NULL;
    q->count              = 0;
    q->waiter_count       = 0;
    q->close_wake_pending = 0;

    return ACTRUST_OK;
}

actrust_err_t actrust_job_queue_enqueue(actrust_job_queue_t *q,
                                        actrust_job_t       *job)
{
    if (q == NULL || job == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_mutex_lock(q->lock);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    if (q->closed) {
        (void) actrust_mutex_unlock(q->lock);
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (q->count >= q->depth) {
        actrust_err_t unlock_err = actrust_mutex_unlock(q->lock);
        if (ACTRUST_IS_ERR(unlock_err)) {
            return unlock_err;
        }
        return CORE_ERR(ACTRUST_ERR_QUEUE_FULL);
    }

    /* Publish before commit so a post failure cannot enqueue a dangling job. */
    err = actrust_sem_post(q->sem_items);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_mutex_unlock(q->lock);
        return err;
    }

    q->buf[q->tail] = job;
    q->tail         = (q->tail + 1) % q->depth;
    q->count++;

    (void) actrust_mutex_unlock(q->lock);
    return ACTRUST_OK;
}

actrust_err_t actrust_job_queue_dequeue(actrust_job_queue_t *q,
                                        uint32_t             timeout_ms,
                                        actrust_job_t      **out_job)
{
    if (q == NULL || out_job == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_mutex_lock(q->lock);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    if (q->closed && q->count == 0u) {
        (void) actrust_mutex_unlock(q->lock);
        *out_job = NULL;
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    q->waiter_count++;
    (void) actrust_mutex_unlock(q->lock);

    err = actrust_sem_wait(q->sem_items, timeout_ms);
    if (ACTRUST_IS_ERR(err)) {
        actrust_err_t lock_err = actrust_mutex_lock(q->lock);
        if (ACTRUST_IS_ERR(lock_err)) {
            return lock_err;
        }
        if (q->waiter_count > 0u) {
            q->waiter_count--;
        }
        bool closed_empty = q->closed && q->count == 0u;
        (void) actrust_mutex_unlock(q->lock);
        if (closed_empty) {
            *out_job = NULL;
            return CORE_ERR(ACTRUST_ERR_BAD_STATE);
        }
        return err;
    }

    err = actrust_mutex_lock(q->lock);
    if (ACTRUST_IS_ERR(err)) {
        if (q->waiter_count > 0u) {
            q->waiter_count--;
        }
        (void) actrust_sem_post(q->sem_items);
        return err;
    }

    if (q->waiter_count > 0u) {
        q->waiter_count--;
    }

    if (q->count == 0u) {
        bool closed              = q->closed;
        *out_job                 = NULL;
        actrust_err_t unlock_err = actrust_mutex_unlock(q->lock);
        if (ACTRUST_IS_ERR(unlock_err)) {
            return unlock_err;
        }
        return closed ? CORE_ERR(ACTRUST_ERR_BAD_STATE)
                      : CORE_ERR(ACTRUST_ERR_NO_RESOURCE);
    }

    *out_job = q->buf[q->head];
    q->head  = (q->head + 1) % q->depth;
    q->count--;

    return actrust_mutex_unlock(q->lock);
}
