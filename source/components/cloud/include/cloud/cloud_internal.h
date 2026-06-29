// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cloud_internal.h
 * @brief Internal declarations for cloud providers.
 */

#ifndef ACTRUST_CLOUD_INTERNAL_H
#define ACTRUST_CLOUD_INTERNAL_H

/* Project */
#include "actrust_errno.h"

/* Cloud */
#include "cloud/cloud.h"

/* Component */
#include "queue/queue.h"

/* Adapter */
#include "adapter/system.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cloud provider operation table.
 */
typedef struct {
    actrust_err_t (*init)(actrust_cloud_t cloud);
    actrust_err_t (*deinit)(actrust_cloud_t cloud);
    actrust_err_t (*connect)(actrust_cloud_t cloud);
    actrust_err_t (*disconnect)(actrust_cloud_t cloud);
    actrust_err_t (*send_data)(actrust_cloud_t cloud, const uint8_t *payload,
                               size_t payload_len);
    actrust_err_t (*send_register)(actrust_cloud_t                   cloud,
                                   actrust_cloud_register_msg_type_t type,
                                   const uint8_t *payload, size_t payload_len);
} actrust_cloud_provider_ops_t;

/**
 * @brief Cloud context.
 */
struct actrust_cloud_ctx {
    actrust_cloud_provider_t provider_type; /**< Cloud provider type. */
    const actrust_cloud_provider_ops_t
                   *provider_ops;   /**< Provider operations. */
    void           *provider_ctx;   /**< Provider private runtime context. */
    actrust_queue_t downlink_queue; /**< Downlink message queue. */
};

#if !defined(CONFIG_ACTRUST_CLOUD_PLATFORM_AWS) ||                             \
    CONFIG_ACTRUST_CLOUD_PLATFORM_AWS
extern const actrust_cloud_provider_ops_t actrust_cloud_provider_aws_ops;
#endif

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_CLOUD_INTERNAL_H */
