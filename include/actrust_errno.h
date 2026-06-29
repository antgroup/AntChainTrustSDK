// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file actrust_errno.h
 * @brief Unified error type and module/reason code layout for AntChainTrustSDK.
 */

#ifndef ACTRUST_ERRNO_H
#define ACTRUST_ERRNO_H

/* C standard */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t actrust_err_t;

#define ACTRUST_OK ((actrust_err_t) 0x00000000u)

/* Layout: [31:16]=module_id (uint16), [15:0]=reason_code (uint16) */
#define ACTRUST_ERR_MAKE(module, code)                                         \
    ((((actrust_err_t) ((module) & 0xFFFFu)) << 16) |                          \
     ((actrust_err_t) ((code) & 0xFFFFu)))

#define ACTRUST_ERR_MODULE(e) ((uint16_t) (((e) >> 16) & 0xFFFFu))
#define ACTRUST_ERR_CODE(e)   ((uint16_t) ((e) & 0xFFFFu))

#define ACTRUST_IS_OK(e)  ((e) == ACTRUST_OK)
#define ACTRUST_IS_ERR(e) ((e) != ACTRUST_OK)

/* Helper: build a full error with module + common reason */
#define ACTRUST_ERR(module, reason) ACTRUST_ERR_MAKE((module), (reason))

/* Module IDs (uint16_t) */
#define ACTRUST_ERR_MODULE_CORE (0x0001u)

#define ACTRUST_ERR_MODULE_COMPONENTS_LOG    (0x2000u)
#define ACTRUST_ERR_MODULE_COMPONENTS_QUEUE  (0x2001u)
#define ACTRUST_ERR_MODULE_COMPONENTS_KV     (0x2002u)
#define ACTRUST_ERR_MODULE_COMPONENTS_JSON   (0x2003u)
#define ACTRUST_ERR_MODULE_COMPONENTS_CRYPTO (0x2004u)
#define ACTRUST_ERR_MODULE_COMPONENTS_NTP    (0x2005u)
#define ACTRUST_ERR_MODULE_COMPONENTS_TLS    (0x2100u)
#define ACTRUST_ERR_MODULE_COMPONENTS_MQTT   (0x2101u)
#define ACTRUST_ERR_MODULE_COMPONENTS_CLOUD  (0x2200u)

#define ACTRUST_ERR_MODULE_ADAPTER_SYSTEM   (0x4000u)
#define ACTRUST_ERR_MODULE_ADAPTER_NETWORK  (0x4001u)
#define ACTRUST_ERR_MODULE_ADAPTER_STORAGE  (0x4002u)
#define ACTRUST_ERR_MODULE_ADAPTER_DEVICE   (0x4003u)
#define ACTRUST_ERR_MODULE_ADAPTER_SECURITY (0x4004u)

/* Common reason codes (0x0001 ~ 0x00FF) */
#define ACTRUST_ERR_INVALID_ARG (0x0001u)
#define ACTRUST_ERR_NOT_READY   (0x0002u)
#define ACTRUST_ERR_BAD_STATE   (0x0003u)
#define ACTRUST_ERR_TIMEOUT     (0x0004u)
#define ACTRUST_ERR_UNSUPPORTED (0x0005u)
#define ACTRUST_ERR_BUSY        (0x0006u)
#define ACTRUST_ERR_ALREADY     (0x0007u)
#define ACTRUST_ERR_WOULD_BLOCK (0x0008u)
#define ACTRUST_ERR_QUEUE_FULL  (0x0009u)

#define ACTRUST_ERR_NO_MEM        (0x0020u)
#define ACTRUST_ERR_NO_RESOURCE   (0x0021u)
#define ACTRUST_ERR_BUF_TOO_SMALL (0x0022u)
#define ACTRUST_ERR_IO            (0x0023u)

#define ACTRUST_ERR_HW_FAILURE     (0x0040u)
#define ACTRUST_ERR_HW_TIMEOUT     (0x0041u)
#define ACTRUST_ERR_HW_COMM        (0x0042u)
#define ACTRUST_ERR_HW_UNSUPPORTED (0x0043u)
#define ACTRUST_ERR_HW_BAD_DATA    (0x0044u)

/* TLS-specific reason codes (0x0100 ~ 0x01FF) */
#define ACTRUST_ERR_TLS_HANDSHAKE_FAILED   (0x0100u)
#define ACTRUST_ERR_TLS_CERT_VERIFY_FAILED (0x0101u)
#define ACTRUST_ERR_TLS_PEER_CLOSED        (0x0102u)
#define ACTRUST_ERR_TLS_CONN_RESET         (0x0103u)
#define ACTRUST_ERR_TLS_INVALID_RECORD     (0x0104u)

/* MQTT-specific reason codes (0x0200 ~ 0x02FF) */
#define ACTRUST_ERR_MQTT_CONNECT_FAILED     (0x0200u)
#define ACTRUST_ERR_MQTT_DISCONNECT_FAILED  (0x0201u)
#define ACTRUST_ERR_MQTT_SUBSCRIBE_FAILED   (0x0202u)
#define ACTRUST_ERR_MQTT_UNSUBSCRIBE_FAILED (0x0203u)
#define ACTRUST_ERR_MQTT_PUBLISH_FAILED     (0x0204u)
#define ACTRUST_ERR_MQTT_RECONNECT_FAILED   (0x0205u)

/* Cloud-specific reason codes (0x0300 ~ 0x03FF) */
#define ACTRUST_ERR_CLOUD_INIT_FAILED      (0x0300u)
#define ACTRUST_ERR_CLOUD_START_FAILED     (0x0301u)
#define ACTRUST_ERR_CLOUD_STOP_FAILED      (0x0302u)
#define ACTRUST_ERR_CLOUD_DEINIT_FAILED    (0x0303u)
#define ACTRUST_ERR_CLOUD_SEND_DATA_FAILED (0x0304u)

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_ERRNO_H */
