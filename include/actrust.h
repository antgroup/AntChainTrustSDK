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
 * The callback is invoked synchronously after an accepted asynchronous
 * operation completes. Only an operation accepted by a public API produces a
 * callback; invalid arguments, invalid state, resource exhaustion, and
 * allocation failures are reported synchronously by that API instead. The
 * callback must not call
 * @ref actrust_deinit; defer teardown until the callback returns. The
 * @p user_data pointer is borrowed and must remain valid until all callbacks
 * that use it have completed.
 *
 * @param[in] result     Operation result — @c ACTRUST_OK on success.
 * @param[in] state      SDK lifecycle state observed when the callback is
 *                       invoked, after the operation's state change has been
 *                       applied.
 * @param[in] user_data  Opaque pointer set via @ref actrust_set_callback.
 */
typedef void (*actrust_callback_t)(actrust_err_t        result,
                                   actrust_core_state_t state, void *user_data);

/**
 * @brief Bootstrap configuration consumed by @ref actrust_init.
 *
 * When non-NULL, the certificate and key bytes are copied while initialization
 * is submitted. The caller may release or modify the source buffers after
 * @ref actrust_init returns. Both certificate and key buffers must be provided
 * together; a partial or malformed configuration is rejected synchronously or
 * reported by the asynchronous init callback, depending on the failure point.
 * When @p config is NULL, the configured credential store is used.
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
 * All subsequent asynchronous API calls will invoke @p cb on completion. The
 * callback runs synchronously in the SDK's service context, so application code
 * should normally record the result and perform the next SDK call from its own
 * task or main loop. Only calls accepted after this function returns can
 * produce callbacks. Must be called before @ref actrust_init. The @p user_data
 * pointer is borrowed and must remain valid until callback delivery is
 * complete.
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
 * Initialises the single global SDK context, submits an asynchronous
 * initialisation request, then starts the service context. The callback fires
 * once initialisation, including credential loading, completes.
 *
 * @param[in] config  Optional first-boot provisioning config; may be @c NULL
 *                    when claim credentials are already stored.
 *
 * @return @c ACTRUST_OK — job submitted; await callback for result.
 * @return @c ACTRUST_ERR_BAD_STATE if the SDK is already initialised,
 *         initialising, or waiting for failed-initialisation cleanup.
 * @return @c ACTRUST_ERR_NOT_READY if the SDK callback or internal resources
 *         are unavailable.
 */
actrust_err_t actrust_init(const actrust_config_t *config);

/**
 * @brief Tear down the Core framework.
 *
 * Requests asynchronous shutdown and waits for it to complete. When this call
 * returns, the SDK is no longer usable until a new initialization. Failures
 * that prevent the request from being accepted are returned synchronously;
 * failures during shutdown are reported through the completion callback.
 * Calling
 * @c actrust_deinit from a callback is invalid and must be deferred until the
 * callback returns.
 *
 * @pre The caller must call @ref actrust_disconnect and wait for the disconnect
 *      callback before invoking this function. The call is valid only when the
 *      SDK is in a deinitialisable state; an active session must not be torn
 *      down. Failed initialisation is deinitialisable after the callback
 *      reports @ref ACTRUST_CORE_INIT_FAILED.
 *
 * @return @c ACTRUST_OK on successful shutdown.
 * @return @c ACTRUST_ERR_BAD_STATE if the SDK is not in a deinitialisable
 *         state. Failed initialisation is deinitialisable after the callback
 *         reports @ref ACTRUST_CORE_INIT_FAILED.
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
 * The input must be non-empty bytes without embedded NUL characters. The SDK
 * copies the bytes before returning, obtains a timestamp, signs the generated
 * representation, and submits the resulting business update through the
 * configured cloud provider. The operation requires time synchronisation and
 * is subject to the configured transport and generated-message size limits. A
 * successful return means only that the asynchronous operation was accepted; it
 * does not confirm remote service or blockchain acknowledgement. Processing
 * errors are reported by the callback.
 *
 * @param[in] data      Business payload bytes (copied internally before
 *                      return).
 * @param[in] data_len  Payload length in bytes.
 *
 * @return @c ACTRUST_OK on successful submission.
 * @return @c ACTRUST_ERR_INVALID_ARG if @p data is NULL, @p data_len is zero,
 *         or the payload contains NUL bytes.
 * @return @c ACTRUST_ERR_BAD_STATE if core is not in the REGISTERED state.
 * @return @c ACTRUST_ERR_NO_MEM if insufficient memory for the payload copy.
 */
actrust_err_t actrust_data_publish(const char *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_H */
