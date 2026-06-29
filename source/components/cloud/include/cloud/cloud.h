// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cloud.h
 * @brief Cloud client component.
 */

#ifndef ACTRUST_CLOUD_H
#define ACTRUST_CLOUD_H

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_config.h"
#include "actrust_errno.h"

/* Component */
#include "crypto/crypto.h"
#include "queue/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Opaque Handles
 * ======================================================================== */

/** @brief Opaque cloud context handle. */
typedef struct actrust_cloud_ctx *actrust_cloud_t;

/* ========================================================================
 * Enumerations
 * ======================================================================== */

/** @brief Cloud provider identifier. */
typedef enum {
    ACTRUST_CLOUD_PROVIDER_AWS = 1, /**< AWS IoT Core provider. */
} actrust_cloud_provider_t;

/** @brief Registration uplink message type. */
typedef enum {
    ACTRUST_CLOUD_REGISTER_REQUEST = 1, /**< Initial registration request. */
    ACTRUST_CLOUD_REGISTER_RESPONSE,    /**< Challenge response. */
} actrust_cloud_register_msg_type_t;

/* ========================================================================
 * Built-in Cloud Credential IDs
 * ======================================================================== */

#define ACTRUST_CLOUD_CLAIM_KEY_ID    ACTRUST_CRYPTO_KEY_ID_EC_0
#define ACTRUST_CLOUD_CLAIM_CERT_ID   ACTRUST_CRYPTO_CERT_ID_X509_0
#define ACTRUST_CLOUD_RUNTIME_KEY_ID  ACTRUST_CRYPTO_KEY_ID_EC_1
#define ACTRUST_CLOUD_RUNTIME_CERT_ID ACTRUST_CRYPTO_CERT_ID_X509_1

/* ========================================================================
 * Message Types
 * ======================================================================== */

/**
 * @brief Cloud downlink message.
 */
typedef struct {
    uint8_t
           payload[CONFIG_ACTRUST_MQTT_PAYLOAD_MAX_LEN]; /**< Payload buffer. */
    size_t payload_len;                                  /**< Payload length. */
    uint64_t recv_time_ms; /**< Receive timestamp in ms. */
} actrust_cloud_msg_t;

/* ========================================================================
 * Public API
 * ======================================================================== */

/**
 * @brief Initialize cloud context.
 *
 * @param[in] provider Cloud provider.
 * @param[out] out_cloud Receives cloud handle on success.
 * @param[out] out_downlink_queue Receives queue for downlink messages on
 * success.
 *
 * @retval ACTRUST_OK Initialization succeeded.
 * @retval ACTRUST_ERR_INVALID_ARG Invalid arguments or unsupported provider.
 * @retval ACTRUST_ERR_NO_MEM Memory allocation failed.
 */
actrust_err_t actrust_cloud_init(actrust_cloud_provider_t provider,
                                 actrust_cloud_t         *out_cloud,
                                 actrust_queue_t         *out_downlink_queue);

/**
 * @brief Deinitialize cloud context and release all resources.
 *
 * @param[in] cloud Cloud handle.
 *
 * @retval ACTRUST_OK Deinitialization succeeded.
 * @retval ACTRUST_ERR_INVALID_ARG @p cloud is NULL.
 * @retval ACTRUST_ERR_CLOUD_DEINIT_FAILED Provider or queue cleanup failed.
 */
actrust_err_t actrust_cloud_deinit(actrust_cloud_t cloud);

/**
 * @brief Connect to cloud provider.
 *
 * @param[in] cloud Cloud handle.
 *
 * @retval ACTRUST_OK Connection established successfully.
 * @retval ACTRUST_ERR_INVALID_ARG Invalid arguments.
 * @retval ACTRUST_ERR_CLOUD_START_FAILED Provider connection/start failed.
 */
actrust_err_t actrust_cloud_connect(actrust_cloud_t cloud);

/**
 * @brief Disconnect from cloud provider.
 *
 * @param[in] cloud Cloud handle.
 *
 * @retval ACTRUST_OK Disconnection successful.
 * @retval ACTRUST_ERR_INVALID_ARG Invalid arguments.
 * @retval ACTRUST_ERR_CLOUD_STOP_FAILED Provider disconnect/stop failed.
 */
actrust_err_t actrust_cloud_disconnect(actrust_cloud_t cloud);

/**
 * @brief Send business uplink data.
 *
 * @param[in] cloud Cloud handle.
 * @param[in] payload Business payload.
 * @param[in] payload_len Payload length.
 *
 * @retval ACTRUST_OK Data queued for uplink.
 * @retval ACTRUST_ERR_INVALID_ARG Invalid arguments, empty payload, oversized
 * payload, embedded NUL byte, or non-object JSON payload.
 * @retval ACTRUST_ERR_BAD_STATE Provider runtime is not ready.
 * @retval ACTRUST_ERR_QUEUE_FULL MQTT command queue is full.
 * @retval ACTRUST_ERR_BUF_TOO_SMALL Generated JSON, topic, or MQTT payload
 * exceeds configured buffer limits.
 */
actrust_err_t actrust_cloud_send_data(actrust_cloud_t cloud,
                                      const uint8_t  *payload,
                                      size_t          payload_len);

/**
 * @brief Send a registration uplink message.
 *
 * Registration traffic uses provider-specific register topics and does not use
 * the business data path.
 *
 * @param[in] cloud Cloud handle.
 * @param[in] type Registration message type.
 * @param[in] payload Registration JSON payload.
 * @param[in] payload_len Payload length.
 *
 * @retval ACTRUST_OK Message queued for uplink.
 * @retval ACTRUST_ERR_INVALID_ARG Invalid arguments, message type, empty
 * payload, oversized payload, or embedded NUL byte.
 * @retval ACTRUST_ERR_BAD_STATE Provider runtime is not ready.
 * @retval ACTRUST_ERR_QUEUE_FULL MQTT command queue is full.
 */
actrust_err_t actrust_cloud_send_register(
    actrust_cloud_t cloud, actrust_cloud_register_msg_type_t type,
    const uint8_t *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_CLOUD_H */
