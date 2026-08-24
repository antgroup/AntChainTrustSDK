// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file mqtt.h
 * @brief MQTT client component.
 */

#ifndef ACTRUST_MQTT_H
#define ACTRUST_MQTT_H

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_config.h"
#include "actrust_errno.h"

/* Component */
#include "tls/tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Opaque Handles
 * ======================================================================== */

/** @brief Opaque MQTT client handle. */
typedef struct actrust_mqtt_ctx *actrust_mqtt_t;

/* ========================================================================
 * Enumerations
 * ======================================================================== */

/** @brief MQTT QoS levels supported by this component. */
typedef enum {
    ACTRUST_MQTT_QOS0 = 0, /**< At most once delivery. */
    ACTRUST_MQTT_QOS1 = 1, /**< At least once delivery. */
} actrust_mqtt_qos_t;

/** @brief Underlying transport mode. */
typedef enum {
    ACTRUST_MQTT_TRANSPORT_TLS = 0, /**< TCP with TLS. */
    ACTRUST_MQTT_TRANSPORT_TCP = 1, /**< Plain TCP without TLS. */
} actrust_mqtt_transport_t;

/* ========================================================================
 * Configuration and Data Types
 * ======================================================================== */

/**
 * @brief Plain TCP transport configuration.
 */
typedef struct {
    const char *host; /**< Host name or IP string. */
    uint16_t    port; /**< TCP port. */
} actrust_mqtt_tcp_config_t;

/**
 * @brief Transport selection and transport-specific configuration.
 */
typedef struct {
    actrust_mqtt_transport_t type; /**< Transport mode. */
    union {
        actrust_mqtt_tcp_config_t tcp; /**< TCP configuration. */
        actrust_tls_config_t      tls; /**< TLS configuration. */
    } config;
} actrust_mqtt_transport_config_t;

/**
 * @brief Static client configuration supplied to @ref actrust_mqtt_init.
 *
 * The string and byte buffers referenced by this structure are copied
 * synchronously by @ref actrust_mqtt_connect. The caller may modify or release
 * @p client_id, the transport host, the CA buffer, and the client certificate
 * after that function returns. The copied configuration is retained while the
 * MQTT process may reconnect.
 *
 * The TLS @p crypto_ctx and @p client_key handles are borrowed. MQTT does not
 * retain, close, or deinitialize them; they must remain valid through the TLS
 * session, process, reconnect, disconnect, and deinitialization lifecycle.
 */
typedef struct {
    const char                     *client_id; /**< MQTT client identifier. */
    actrust_mqtt_transport_config_t transport; /**< Transport configuration. */
} actrust_mqtt_config_t;

/**
 * @brief MQTT message.
 */
typedef struct {
    char              *topic;       /**< Topic. */
    uint16_t           topic_len;   /**< Topic length. */
    uint8_t           *payload;     /**< Payload. */
    size_t             payload_len; /**< Payload length. */
    actrust_mqtt_qos_t qos;         /**< QoS. */
} actrust_mqtt_message_t;

/* ========================================================================
 * Callbacks
 * ======================================================================== */

/**
 * @brief Callback invoked for inbound messages.
 *
 * @param[in] user_ctx User context configured in @ref actrust_mqtt_callbacks_t.
 * @param[in] message  Inbound MQTT message.
 */
typedef void (*actrust_mqtt_on_message_fn)(
    void *user_ctx, const actrust_mqtt_message_t *message);

/**
 * @brief Application callback bundle.
 */
typedef struct {
    actrust_mqtt_on_message_fn on_message; /**< Message callback, or NULL. */
    void *user_ctx; /**< Opaque user context passed to callbacks. */
} actrust_mqtt_callbacks_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Threading model.
 *
 * The APIs below enqueue MQTT commands. Protocol progress and socket I/O are
 * driven by @ref actrust_mqtt_process, which must run in a dedicated task
 * created by the upper layer.
 *
 * @note @ref actrust_mqtt_connect and @ref actrust_mqtt_disconnect poll the
 *       connection state until completion or timeout (checking every
 *       @c ACTRUST_MQTT_WAIT_FOR_RESULT_MS).  The upper-layer process task
 *       (driven by @ref actrust_mqtt_process) must be running so that the
 *       MQTT stack can make progress; otherwise these calls will time out.
 */

/**
 * @brief Initialize an MQTT client.
 *
 * @param[out] out_mqtt Receives the client handle.
 *
 * @retval ACTRUST_OK Initialization succeeded.
 * @retval ACTRUST_ERR_INVALID_ARG One or more arguments are invalid.
 * @retval ACTRUST_ERR_NO_MEM Memory allocation failed.
 */
actrust_err_t actrust_mqtt_init(actrust_mqtt_t *out_mqtt);

/**
 * @brief Deinitialize an MQTT client.
 *
 * If the process loop is still running after the client has left
 * CONNECTED/CONNECTING state, this function requests loop termination and
 * waits for @ref actrust_mqtt_process to return before releasing resources.
 *
 * @param[in] mqtt Client handle.
 *
 * @retval ACTRUST_OK Deinitialization succeeded.
 * @retval ACTRUST_ERR_INVALID_ARG @c mqtt is NULL.
 * @retval ACTRUST_ERR_BAD_STATE Client is connected or connecting.
 * @retval ACTRUST_ERR_TIMEOUT Process loop did not terminate in time.
 */
actrust_err_t actrust_mqtt_deinit(actrust_mqtt_t mqtt);

/**
 * @brief Set the callback bundle.
 *
 * @param[in] mqtt Client handle.
 * @param[in] callbacks Callback bundle.
 *
 * @retval ACTRUST_OK Callbacks were updated.
 * @retval ACTRUST_ERR_INVALID_ARG @c mqtt is NULL.
 */
actrust_err_t actrust_mqtt_set_callbacks(
    actrust_mqtt_t mqtt, const actrust_mqtt_callbacks_t *callbacks);

/**
 * @brief Connect to the broker.
 *
 * @param[in] mqtt Client handle.
 * @param[in] config Client configuration. String and certificate buffers are
 *                   copied before this function returns; TLS crypto/key handles
 *                   remain borrowed.
 *
 * @retval ACTRUST_OK Connected successfully.
 * @retval ACTRUST_ERR_INVALID_ARG The configuration contains an invalid
 * transport, empty client/host string, zero port, inconsistent TLS buffer
 * length, certificate/key pairing, certificate format, or crypto context.
 * @retval ACTRUST_ERR_BAD_STATE @c mqtt is in a non-connectable state.
 * @retval ACTRUST_ERR_TIMEOUT Connect attempt exceeded its timeout.
 * @retval ACTRUST_ERR_QUEUE_FULL Internal command queue is full.
 * @retval ACTRUST_ERR_NO_MEM Memory allocation failed.
 * @return Transport/TLS/MQTT mapping errors from lower layers if opening or
 * connecting fails.
 */
actrust_err_t actrust_mqtt_connect(actrust_mqtt_t               mqtt,
                                   const actrust_mqtt_config_t *config);

/**
 * @brief Disconnect from the broker.
 *
 * @param[in] mqtt Client handle.
 *
 * @retval ACTRUST_OK Disconnected successfully.
 * @retval ACTRUST_ERR_INVALID_ARG @c mqtt is NULL.
 * @retval ACTRUST_ERR_BAD_STATE Client is not connected.
 * @retval ACTRUST_ERR_TIMEOUT Disconnect attempt exceeded its timeout.
 * @retval ACTRUST_ERR_QUEUE_FULL Internal command queue is full.
 * @retval ACTRUST_ERR_NO_MEM Memory allocation failed.
 */
actrust_err_t actrust_mqtt_disconnect(actrust_mqtt_t mqtt);

/**
 * @brief Publish a message.
 *
 * @param[in] mqtt Client handle.
 * @param[in] message Publish message.
 *
 * @retval ACTRUST_OK Message published successfully (in the transmit queue).
 * @retval ACTRUST_ERR_BAD_STATE Client is not connected.
 * @retval ACTRUST_ERR_INVALID_ARG Invalid arguments.
 * @retval ACTRUST_ERR_QUEUE_FULL Internal transmit queue is full.
 * @retval ACTRUST_ERR_BUF_TOO_SMALL Topic or payload exceeds the configured
 * limits.
 * @retval ACTRUST_ERR_NO_MEM Memory allocation failed.
 */
actrust_err_t actrust_mqtt_publish(actrust_mqtt_t                mqtt,
                                   const actrust_mqtt_message_t *message);

/**
 * @brief Subscribe to a topic.
 *
 * @param[in] mqtt Client handle.
 * @param[in] topic Topic name.
 *
 * @retval ACTRUST_OK Subscription request accepted.
 * @retval ACTRUST_ERR_BAD_STATE Client is not connected.
 * @retval ACTRUST_ERR_INVALID_ARG Invalid arguments.
 * @retval ACTRUST_ERR_NO_MEM Memory allocation failed.
 */
actrust_err_t actrust_mqtt_subscribe(actrust_mqtt_t mqtt, const char *topic);

/**
 * @brief Unsubscribe from a topic.
 *
 * @param[in] mqtt Client handle.
 * @param[in] topic Topic name.
 *
 * @retval ACTRUST_OK Unsubscription request accepted.
 * @retval ACTRUST_ERR_BAD_STATE Client is not connected.
 * @retval ACTRUST_ERR_INVALID_ARG Invalid arguments.
 * @retval ACTRUST_ERR_NO_MEM Memory allocation failed.
 */
actrust_err_t actrust_mqtt_unsubscribe(actrust_mqtt_t mqtt, const char *topic);

/**
 * @brief Process MQTT traffic and protocol loop.
 *
 * @param[in] mqtt Client handle.
 *
 * @retval ACTRUST_OK Processing loop exited normally.
 * @retval ACTRUST_ERR_INVALID_ARG @c mqtt is NULL.
 */
actrust_err_t actrust_mqtt_process(actrust_mqtt_t mqtt);

/**
 * @brief Request a running @ref actrust_mqtt_process loop to terminate.
 *
 * @param[in] mqtt Client handle.
 *
 * @retval ACTRUST_OK Termination command was queued.
 * @retval ACTRUST_ERR_INVALID_ARG @c mqtt is NULL.
 * @retval ACTRUST_ERR_QUEUE_FULL Internal command queue is full.
 */
actrust_err_t actrust_mqtt_stop_process(actrust_mqtt_t mqtt);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_MQTT_H */
