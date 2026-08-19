// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file system.c
 * @brief Android platform system adapter implementation
 *
 * Provides cross-platform primitives for time, mutexes, semaphores, threads,
 * and log output.  Android is implemented with Bionic pthreads, POSIX clocks,
 * unnamed semaphores, and logcat output through liblog.
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

/* C standard */
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Android */
#include <android/log.h>

/* Adapter */
#include "adapter/system.h"

static pthread_mutex_t g_lifecycle_gate = PTHREAD_MUTEX_INITIALIZER;

/* ========================================================================
 * Lifecycle Gate Implementation
 * ======================================================================== */

actrust_err_t actrust_lifecycle_lock(void)
{
    return (pthread_mutex_lock(&g_lifecycle_gate) == 0)
               ? ACTRUST_OK
               : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                             ACTRUST_ERR_HW_FAILURE);
}

actrust_err_t actrust_lifecycle_unlock(void)
{
    return (pthread_mutex_unlock(&g_lifecycle_gate) == 0)
               ? ACTRUST_OK
               : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                             ACTRUST_ERR_HW_FAILURE);
}

/* ========================================================================
 * Memory Allocation Implementation
 * ======================================================================== */

void *actrust_malloc(size_t size)
{
    return (size == 0u) ? NULL : malloc(size);
}

void *actrust_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void actrust_free(void *ptr)
{
    free(ptr);
}

/* ========================================================================
 * Time Services Implementation
 * ======================================================================== */

uint64_t actrust_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) (ts.tv_nsec / 1000000u);
}

uint64_t actrust_wall_time_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) (ts.tv_nsec / 1000000u);
}

void actrust_sleep_ms(uint32_t ms)
{
    struct timespec req;
    req.tv_sec  = (time_t) (ms / 1000u);
    req.tv_nsec = (long) ((ms % 1000u) * 1000000u);

    while (nanosleep(&req, &req) != 0) {
        if (errno != EINTR) {
            break;
        }
    }
}

/* ========================================================================
 * Mutex Implementation
 * ======================================================================== */

actrust_err_t actrust_mutex_create(actrust_mutex_t *out)
{
    if (out == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }

    pthread_mutex_t *mtx = (pthread_mutex_t *) calloc(1, sizeof(*mtx));
    if (mtx == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_NO_MEM);
    }

    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) {
        free(mtx);
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }
    (void) pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    if (pthread_mutex_init(mtx, &attr) != 0) {
        (void) pthread_mutexattr_destroy(&attr);
        free(mtx);
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }

    (void) pthread_mutexattr_destroy(&attr);
    *out = (actrust_mutex_t) mtx;
    return ACTRUST_OK;
}

actrust_err_t actrust_mutex_destroy(actrust_mutex_t m)
{
    if (m == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }

    pthread_mutex_t *mtx = (pthread_mutex_t *) m;
    int              rc  = pthread_mutex_destroy(mtx);
    if (rc != 0) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }
    free(mtx);
    return ACTRUST_OK;
}

actrust_err_t actrust_mutex_lock(actrust_mutex_t m)
{
    if (m == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }
    pthread_mutex_t *mtx = (pthread_mutex_t *) m;
    return (pthread_mutex_lock(mtx) == 0)
               ? ACTRUST_OK
               : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                             ACTRUST_ERR_HW_FAILURE);
}

actrust_err_t actrust_mutex_unlock(actrust_mutex_t m)
{
    if (m == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }
    pthread_mutex_t *mtx = (pthread_mutex_t *) m;
    return (pthread_mutex_unlock(mtx) == 0)
               ? ACTRUST_OK
               : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                             ACTRUST_ERR_HW_FAILURE);
}

/* ========================================================================
 * Semaphore Implementation
 * ======================================================================== */

actrust_err_t actrust_sem_create(actrust_sem_t *out, uint32_t initial_count)
{
    if (out == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }

    sem_t *sem = (sem_t *) calloc(1, sizeof(*sem));
    if (sem == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_NO_MEM);
    }

    if (sem_init(sem, 0, (unsigned int) initial_count) != 0) {
        free(sem);
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }

    *out = (actrust_sem_t) sem;
    return ACTRUST_OK;
}

actrust_err_t actrust_sem_destroy(actrust_sem_t sem)
{
    if (sem == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }

    sem_t *s  = (sem_t *) sem;
    int    rc = sem_destroy(s);
    if (rc != 0) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }
    free(s);
    return ACTRUST_OK;
}

actrust_err_t actrust_sem_wait(actrust_sem_t sem, uint32_t timeout_ms)
{
    if (sem == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }

    sem_t *s = (sem_t *) sem;

    if (timeout_ms == 0) {
        if (sem_trywait(s) == 0) {
            return ACTRUST_OK;
        }
        return (errno == EAGAIN)
                   ? ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                                 ACTRUST_ERR_WOULD_BLOCK)
                   : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                                 ACTRUST_ERR_HW_FAILURE);
    }

    if (timeout_ms == 0xFFFFFFFFu) {
        int rc;
        do {
            rc = sem_wait(s);
        } while (rc != 0 && errno == EINTR);
        return (rc == 0) ? ACTRUST_OK
                         : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                                       ACTRUST_ERR_HW_FAILURE);
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }

    uint64_t nsec = (uint64_t) ts.tv_nsec + (uint64_t) timeout_ms * 1000000u;
    ts.tv_sec += (time_t) (nsec / 1000000000u);
    ts.tv_nsec = (long) (nsec % 1000000000u);

    int rc;
    do {
        rc = sem_timedwait(s, &ts);
    } while (rc != 0 && errno == EINTR);

    if (rc == 0) {
        return ACTRUST_OK;
    }
    if (errno == ETIMEDOUT) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_TIMEOUT);
    }
    return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                       ACTRUST_ERR_HW_FAILURE);
}

actrust_err_t actrust_sem_post(actrust_sem_t sem)
{
    if (sem == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }
    sem_t *s = (sem_t *) sem;
    return (sem_post(s) == 0) ? ACTRUST_OK
                              : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                                            ACTRUST_ERR_HW_FAILURE);
}

/* ========================================================================
 * Log Output Implementation
 * ======================================================================== */

actrust_err_t actrust_log_out(const char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }

    size_t total = 0u;
    while (total < len) {
        size_t chunk = len - total;
        if (chunk > (size_t) INT_MAX) {
            chunk = (size_t) INT_MAX;
        }

        int rc = __android_log_print(ANDROID_LOG_INFO, "actrust", "%.*s",
                                     (int) chunk, buf + total);
        if (rc < 0) {
            return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                               ACTRUST_ERR_IO);
        }
        total += chunk;
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Thread Task Implementation
 * ======================================================================== */

/* ========================================================================
 * Private Types and Helper Functions
 * ======================================================================== */

typedef struct {
    pthread_t thread;
    sem_t     done;
    void (*entry)(void *);
    void *arg;
} actrust_task_ctx_t;

/**
 * @brief Thread entry adapter (converts void(*)(void*) to pthread entry)
 */
static void *actrust_task_entry_wrapper(void *param)
{
    actrust_task_ctx_t *ctx = (actrust_task_ctx_t *) param;
    if (ctx != NULL && ctx->entry != NULL) {
        ctx->entry(ctx->arg);
        (void) sem_post(&ctx->done);
    }
    return NULL;
}

static void actrust_task_ctx_destroy(actrust_task_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    (void) sem_destroy(&ctx->done);
    free(ctx);
}

static actrust_err_t actrust_task_wait_done(actrust_task_ctx_t *ctx,
                                            uint32_t            timeout_ms)
{
    if (timeout_ms == 0u) {
        if (sem_trywait(&ctx->done) == 0) {
            return ACTRUST_OK;
        }
        return (errno == EAGAIN)
                   ? ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                                 ACTRUST_ERR_WOULD_BLOCK)
                   : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                                 ACTRUST_ERR_HW_FAILURE);
    }

    if (timeout_ms == 0xFFFFFFFFu) {
        int rc;
        do {
            rc = sem_wait(&ctx->done);
        } while (rc != 0 && errno == EINTR);
        return (rc == 0) ? ACTRUST_OK
                         : ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                                       ACTRUST_ERR_HW_FAILURE);
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }

    uint64_t nsec = (uint64_t) ts.tv_nsec + (uint64_t) timeout_ms * 1000000u;
    ts.tv_sec += (time_t) (nsec / 1000000000u);
    ts.tv_nsec = (long) (nsec % 1000000000u);

    int rc;
    do {
        rc = sem_timedwait(&ctx->done, &ts);
    } while (rc != 0 && errno == EINTR);

    if (rc == 0) {
        return ACTRUST_OK;
    }
    if (errno == ETIMEDOUT) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_TIMEOUT);
    }
    return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                       ACTRUST_ERR_HW_FAILURE);
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

actrust_err_t actrust_task_create(actrust_task_t *out, const char *name,
                                  void (*entry)(void *), void     *arg,
                                  uint32_t stack_size, uint32_t priority)
{
    (void) priority;

    if (out == NULL || entry == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }

    actrust_task_ctx_t *ctx = (actrust_task_ctx_t *) calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_NO_MEM);
    }

    if (sem_init(&ctx->done, 0, 0) != 0) {
        free(ctx);
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }
    ctx->entry = entry;
    ctx->arg   = arg;

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) {
        actrust_task_ctx_destroy(ctx);
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }

    if (stack_size > 0) {
        (void) pthread_attr_setstacksize(&attr, (size_t) stack_size);
    }

    int rc =
        pthread_create(&ctx->thread, &attr, actrust_task_entry_wrapper, ctx);
    (void) pthread_attr_destroy(&attr);

    if (rc != 0) {
        actrust_task_ctx_destroy(ctx);
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }

    if (name != NULL && name[0] != '\0') {
        char truncated[16];
        (void) snprintf(truncated, sizeof(truncated), "%s", name);
        (void) pthread_setname_np(ctx->thread, truncated);
    }

    *out = (actrust_task_t) ctx;
    return ACTRUST_OK;
}

actrust_err_t actrust_task_join(actrust_task_t task, uint32_t timeout_ms)
{
    if (task == NULL) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_INVALID_ARG);
    }

    actrust_task_ctx_t *ctx = (actrust_task_ctx_t *) task;

    actrust_err_t err = actrust_task_wait_done(ctx, timeout_ms);
    if (err != ACTRUST_OK) {
        return err;
    }

    if (pthread_join(ctx->thread, NULL) != 0) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_HW_FAILURE);
    }

    actrust_task_ctx_destroy(ctx);
    return ACTRUST_OK;
}
