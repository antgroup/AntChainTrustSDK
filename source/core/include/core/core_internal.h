// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file core_internal.h
 * @brief Core internal shared declarations — context, lock helpers, service
 * lifecycle.
 */

#ifndef ACTRUST_CORE_INTERNAL_H
#define ACTRUST_CORE_INTERNAL_H

/* C standard */
#include <stdbool.h>
#include <stddef.h>

/* Project */
#include "actrust.h"
#include "actrust_config.h"

/* Core */
#include "core/core_job.h"

/* Component */
#include "cloud/cloud.h"
#include "crypto/crypto.h"
#include "ntp/ntp.h"

/* Adapter */
#include "adapter/system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Context
 * ======================================================================== */

/** @brief Callback with associated user context. */
typedef struct {
    actrust_callback_t fn;
    void              *user_data;
} actrust_callback_ctx_t;

#define CORE_NONCE_MAX_LEN               64
#define CORE_REGISTER_SESSION_RANDOM_LEN 16u
#define CORE_REGISTER_SESSION_ID_LEN     (CORE_REGISTER_SESSION_RANDOM_LEN * 2u)

typedef enum {
    ACTRUST_CORE_REG_PHASE_IDLE = 0,
    ACTRUST_CORE_REG_PHASE_WAIT_CHALLENGE,
    ACTRUST_CORE_REG_PHASE_WAIT_RESULT,
} actrust_core_register_phase_t;

/** @brief Register synchronization context. */
typedef struct {
    actrust_sem_t                 challenge_sem;
    actrust_sem_t                 result_sem;
    uint8_t                       nonce[CORE_NONCE_MAX_LEN];
    size_t                        nonce_len;
    char                          session_id[CORE_REGISTER_SESSION_ID_LEN + 1u];
    size_t                        session_id_len;
    bool                          ok;
    bool                          in_progress;
    actrust_core_register_phase_t phase;
} actrust_core_register_ctx_t;

typedef enum {
    ACTRUST_CORE_DEINIT_PHASE_JOB = 0,
    ACTRUST_CORE_DEINIT_PHASE_QUEUE,
    ACTRUST_CORE_DEINIT_PHASE_POOL,
    ACTRUST_CORE_DEINIT_PHASE_LOCK,
} actrust_core_deinit_phase_t;

/** @brief Global core context (single instance). */
typedef struct {
    /* Core control */
    actrust_mutex_t lock; /**< Mutex protecting shared core state, callback
                             data, and registration handshake state. */
    actrust_core_state_t state; /**< Core lifecycle state. */
    bool deinit_pending; /**< Set by the first actrust_deinit() to win the race;
                            gates concurrent callers. */
    bool
        callback_active; /**< User callback is executing on the service task. */
    bool deinit_finalizing; /**< A caller owns the current teardown attempt. */
    bool deinit_job_done; /**< DEINIT job has completed on the service task. */
    actrust_core_deinit_phase_t
                  deinit_phase;  /**< Next Core teardown phase to execute. */
    actrust_err_t deinit_result; /**< Result of the most recent DEINIT job. */
    actrust_callback_ctx_t
        cb; /**< Registered completion callback and its user context. */

    /* Async job execution */
    actrust_job_pool_t job_pool; /**< Pre-allocated pool for async core jobs. */
    actrust_job_queue_t
        job_queue; /**< FIFO queue used to hand jobs to the service task. */
    actrust_task_t
        service_task; /**< Background task that dequeues and executes jobs. */
    actrust_err_t service_result; /**< Service loop result returned after a
                                     successful join. */

    /* Cloud runtime */
    actrust_cloud_t
        cloud; /**< Cloud client handle for connect, uplink, and downlink. */
    actrust_queue_t downlink_queue;  /**< Queue receiving downlink messages from
                                     the  cloud layer. */
    actrust_task_t downlink_task;    /**< Task that consumes registration-phase
                                     downlink messages. */
    actrust_sem_t downlink_stop_sem; /**< Posted on disconnect to wake and
                                     terminate the downlink task. */

    /* Security */
    actrust_crypto_ctx_t
        crypto; /**< Crypto subsystem context for key and signing operations. */
    actrust_crypto_key_t sign_key; /**< Device signing key used for registration
                                   and data publish. */

    /* Time synchronization */
    actrust_ntp_t
        ntp; /**< NTP handle used to obtain corrected UTC timestamps. */
    actrust_task_t ntp_task; /**< Periodic NTP sync task; lives between connect
                             and disconnect. */
    actrust_sem_t ntp_stop_sem; /**< Posted on disconnect to wake and terminate
                                the NTP task. */

    /* Registration */
    actrust_core_register_ctx_t
        reg; /**< Temporary state for challenge-response registration. */
} actrust_core_ctx_t;

/* ========================================================================
 * Context accessors
 * ======================================================================== */

/**
 * @brief Obtain a pointer to the single global core context.
 */
actrust_core_ctx_t *core_get_ctx(void);

/**
 * @brief Lock the global core context mutex.
 */
void core_lock(void);

/**
 * @brief Unlock the global core context mutex.
 */
void core_unlock(void);

/* ========================================================================
 * State accessors
 * ======================================================================== */

/**
 * @brief Read the current core state (thread-safe).
 */
actrust_core_state_t core_get_state(void);

/**
 * @brief Write the core state (thread-safe).
 */
void core_set_state(actrust_core_state_t state);

/**
 * @brief Atomically test-and-set the deinit-pending flag.
 *
 * Returns @c true on the first call that finds a deinitialisable state and
 * @c deinit_pending == false, marking the flag set. All subsequent concurrent
 * callers receive @c false and must short-circuit. Used to make
 * @ref actrust_deinit idempotent under racing threads.
 */
bool core_try_acquire_deinit(void);

/* ========================================================================
 * Callback accessors
 * ======================================================================== */

/**
 * @brief Read the registered callback (thread-safe).
 */
actrust_callback_ctx_t core_get_callback(void);

/**
 * @brief Write the registered callback (thread-safe).
 */
void core_set_callback(actrust_callback_ctx_t cb);

/* ========================================================================
 * Service lifecycle
 * ======================================================================== */

/**
 * @brief Start the service task.
 */
actrust_err_t core_service_start(void);

/**
 * @brief Request the service to stop.
 */
actrust_err_t core_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_CORE_INTERNAL_H */
