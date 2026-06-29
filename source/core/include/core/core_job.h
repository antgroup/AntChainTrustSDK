// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file core_job.h
 * @brief Job entity, JobPool and JobQueue — internal declarations.
 */

#ifndef ACTRUST_CORE_JOB_H
#define ACTRUST_CORE_JOB_H

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_config.h"
#include "actrust_errno.h"

/* Adapter */
#include "adapter/system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Job
 * ======================================================================== */

/** @brief Job types — one per public API verb. */
typedef enum {
    ACTRUST_JOB_NONE       = 0,
    ACTRUST_JOB_INIT       = 1,
    ACTRUST_JOB_CONNECT    = 2,
    ACTRUST_JOB_DISCONNECT = 3,
    ACTRUST_JOB_REGISTER   = 4,
    ACTRUST_JOB_SEND       = 5,
    ACTRUST_JOB_DEINIT     = 6,
} actrust_job_type_t;

/** @brief A single schedulable work item consumed by the Core-Service. */
typedef struct actrust_job {
    actrust_job_type_t type;
    uint8_t            refcnt;
    actrust_err_t      result;
    union {
        struct {
            bool has_config;
            char *
                claim_cert; /**< Owned PEM text copied from actrust_config_t. */
            size_t claim_cert_len;
            char
                *claim_key; /**< Owned PEM text copied from actrust_config_t. */
            size_t claim_key_len;
        } init;
        struct {
            char  *payload; /**< Owned copy of user-provided data. */
            size_t payload_len;
        } send;
    } params;
} actrust_job_t;

/* ========================================================================
 * Job Pool
 * ======================================================================== */

/** @brief Pre-allocated pool of reusable Job objects. */
typedef struct {
    actrust_job_t   pool[CONFIG_ACTRUST_CORE_JOB_POOL_SIZE];
    uint16_t        free_indices[CONFIG_ACTRUST_CORE_JOB_POOL_SIZE];
    int16_t         free_top;
    actrust_mutex_t mutex;
} actrust_job_pool_t;

/**
 * @brief Initialise the job pool.
 */
actrust_err_t actrust_job_pool_init(actrust_job_pool_t *pool);

/**
 * @brief Deinitialise the job pool and release its mutex.
 */
actrust_err_t actrust_job_pool_deinit(actrust_job_pool_t *pool);

/**
 * @brief Allocate a job from the pool.
 *
 * @param[in]  pool  Pool instance.
 * @param[out] job   Receives a pointer to the allocated job.
 *
 * @return @c ACTRUST_OK or @c ACTRUST_ERR_NO_RESOURCE when exhausted.
 */
actrust_err_t actrust_job_alloc(actrust_job_pool_t *pool, actrust_job_t **job);

/**
 * @brief Increment the reference count of a job.
 *
 * @return @c ACTRUST_OK on success, or an error if the job is not in use.
 */
actrust_err_t actrust_job_retain(actrust_job_pool_t *pool, actrust_job_t *job);

/**
 * @brief Decrement the reference count; return to pool when it hits zero.
 *
 * @return @c ACTRUST_OK on success, or an error if the job is not in use.
 */
actrust_err_t actrust_job_release(actrust_job_pool_t *pool, actrust_job_t *job);

/* ========================================================================
 * Job Queue
 * ======================================================================== */

/** @brief FIFO queue carrying Job pointers from API to Service. */
typedef struct {
    actrust_job_t **buf;
    uint16_t        depth;
    uint16_t        head;
    uint16_t        tail;
    uint16_t        count;
    actrust_mutex_t lock;
    actrust_sem_t   sem_items;
} actrust_job_queue_t;

/**
 * @brief Initialise a job queue.
 *
 * @param[in] q      Queue instance.
 * @param[in] depth  Maximum number of pending jobs.
 *
 * @return @c ACTRUST_OK on success.
 */
actrust_err_t actrust_job_queue_init(actrust_job_queue_t *q, uint16_t depth);

/**
 * @brief Deinitialise a job queue and release resources.
 */
actrust_err_t actrust_job_queue_deinit(actrust_job_queue_t *q);

/**
 * @brief Enqueue a job (non-blocking for async callers).
 *
 * On success the queue takes ownership of @p job until it is dequeued
 * and completed by the service.
 *
 * @return @c ACTRUST_OK or @c ACTRUST_ERR_QUEUE_FULL.
 */
actrust_err_t actrust_job_queue_enqueue(actrust_job_queue_t *q,
                                        actrust_job_t       *job);

/**
 * @brief Blocking dequeue — waits until a job is available.
 *
 * @param[in]  q          Queue instance.
 * @param[in]  timeout_ms Timeout for the blocking wait.
 * @param[out] out_job    Receives the dequeued job.
 *
 * @return @c ACTRUST_OK or @c ACTRUST_ERR_TIMEOUT.
 */
actrust_err_t actrust_job_queue_dequeue(actrust_job_queue_t *q,
                                        uint32_t             timeout_ms,
                                        actrust_job_t      **out_job);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_CORE_JOB_H */
