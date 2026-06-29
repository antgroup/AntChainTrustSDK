// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file actrust.h
 * @brief AntChainTrustSDK Core public API.
 */

#ifndef ACTRUST_H
#define ACTRUST_H

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Type Definitions
 * ======================================================================== */

/** @brief Core life-cycle states visible to the caller. */
typedef enum {
    ACTRUST_CORE_UNINIT       = 0, /**< Not yet initialised. */
    ACTRUST_CORE_READY        = 1, /**< Initialised, not connected. */
    ACTRUST_CORE_UNREGISTERED = 2, /**< Connected, device not registered. */
    ACTRUST_CORE_REGISTERED   = 3, /**< Connected and registered. */
    ACTRUST_CORE_DEINIT       = 4, /**< Shutdown in progress. */
    ACTRUST_CORE_INITING      = 5, /**< Initialisation job is running. */
    ACTRUST_CORE_INIT_FAILED  = 6, /**< Init failed; cleanup needed. */
} actrust_core_state_t;

/**
 * @brief Async completion callback signature.
 *
 * @param[in] result     Operation result — @c ACTRUST_OK on success.
 * @param[in] state      Core lifecycle state observed at callback time, after
 *                       the job has been dispatched and any state transitions
 *                       made by that job have been applied.
 * @param[in] user_data  Opaque pointer set via @ref actrust_set_callback.
 */
typedef void (*actrust_callback_t)(actrust_err_t        result,
                                   actrust_core_state_t state, void *user_data);

/**
 * @brief Bootstrap configuration consumed by @ref actrust_init.
 */
typedef struct {
    const char
          *claim_cert;     /**< Pre-provisioned claim certificate (PEM text). */
    size_t claim_cert_len; /**< Length of @c claim_cert in bytes (excluding
                              NUL). */
    const char *claim_key; /**< Pre-provisioned claim private key (PEM text). */
    size_t
        claim_key_len; /**< Length of @c claim_key in bytes (excluding NUL). */
} actrust_config_t;

/* ========================================================================
 * AntChainTrustSDK API
 * ======================================================================== */

/**
 * @brief Set the global async completion callback.
 *
 * All subsequent async API calls will invoke @p cb on completion.
 * Must be called before @ref actrust_init.
 *
 * @param[in] cb         Completion callback (must not be NULL).
 * @param[in] user_data  Opaque pointer forwarded to every @p cb invocation.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG if @p cb is NULL.
 */
actrust_err_t actrust_set_callback(actrust_callback_t cb, void *user_data);

/**
 * @brief Initialise the Core framework.
 *
 * Initialises the single global context resources (JobPool and JobQueue),
 * submits an asynchronous INIT job, then starts the Service task.  The callback
 * fires once the init logic (credential loading, etc.) completes on the Service
 * thread.
 *
 * @param[in] config  Optional first-boot provisioning config; may be @c NULL
 *                    when claim credentials are already stored.
 *
 * @return @c ACTRUST_OK — job submitted; await callback for result.
 * @return @c ACTRUST_ERR_BAD_STATE if core is already initialised,
 *         initialising, or waiting for failed-init cleanup.
 * @return @c ACTRUST_ERR_NOT_READY if internal resources are unavailable.
 */
actrust_err_t actrust_init(const actrust_config_t *config);

/**
 * @brief Tear down the Core framework.
 *
 * Submits a DEINIT job and waits for service shutdown. When this call returns,
 * internal Core resources have been released.
 *
 * @pre The caller must call @ref actrust_disconnect and wait for the disconnect
 *      callback before invoking this function.  Calling @c actrust_deinit while
 *      a session is still active results in undefined behavior.
 *
 * @return @c ACTRUST_OK on successful shutdown.
 * @return @c ACTRUST_ERR_BAD_STATE if core is not in a deinitialisable state.
 *         Failed initialisation is deinitialisable after the callback reports
 *         @ref ACTRUST_CORE_INIT_FAILED.
 */
actrust_err_t actrust_deinit(void);

/**
 * @brief Establish a session.
 *
 * @return @c ACTRUST_OK on successful submission.
 * @return @c ACTRUST_ERR_BAD_STATE if core is not in the READY state.
 */
actrust_err_t actrust_connect(void);

/**
 * @brief Disconnect the current session.
 *
 * @return @c ACTRUST_OK on successful submission.
 * @return @c ACTRUST_ERR_BAD_STATE if core is not in a connected state.
 */
actrust_err_t actrust_disconnect(void);

/**
 * @brief Perform device registration / credential issuance.
 *
 * @return @c ACTRUST_OK on successful submission.
 * @return @c ACTRUST_ERR_BAD_STATE if core is not in the UNREGISTERED state.
 */
actrust_err_t actrust_register(void);

/**
 * @brief Publish business data over the Runtime session.
 *
 * @param[in] data      Payload text bytes (copied internally before return).
 *                      The payload must not contain NUL bytes.
 * @param[in] data_len  Payload length in bytes.
 *
 * @return @c ACTRUST_OK on successful submission.
 * @return @c ACTRUST_ERR_INVALID_ARG if @p data is NULL or @p data_len is zero.
 * @return @c ACTRUST_ERR_BAD_STATE if core is not in the REGISTERED state.
 * @return @c ACTRUST_ERR_NO_MEM if insufficient memory for the payload copy.
 */
actrust_err_t actrust_data_publish(const char *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_H */
