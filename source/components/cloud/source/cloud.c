// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cloud.c
 * @brief Cloud client dispatch layer.
 */

/* C standard */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Common */
#include "common/common.h"

/* Project */
#include "actrust_errno.h"

/* Cloud */
#include "cloud/cloud.h"
#include "cloud/cloud_internal.h"

/* Component */
#include "log/log.h"

/* Adapter */
#include "adapter/system.h"

#define CLOUD_ERR(reason)                                                      \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_CLOUD, (reason))

/* ========================================================================
 * Private Functions
 * ======================================================================== */

static const actrust_cloud_provider_ops_t *cloud_get_provider_ops(
    actrust_cloud_provider_t provider)
{
    switch (provider) {
        case ACTRUST_CLOUD_PROVIDER_AWS:
            return &actrust_cloud_provider_aws_ops;
        default:
            return NULL;
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

actrust_err_t actrust_cloud_init(actrust_cloud_provider_t provider,
                                 actrust_cloud_t         *out_cloud,
                                 actrust_queue_t         *out_downlink_queue)
{
    LOG_INFO("cloud init requested: provider=%d", (int) provider);
    if (out_cloud == NULL || out_downlink_queue == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const actrust_cloud_provider_ops_t *provider_ops =
        cloud_get_provider_ops(provider);
    if (provider_ops == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_t cloud = (actrust_cloud_t) ACTRUST_CALLOC(1, sizeof(*cloud));
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_NO_MEM);
    }

    cloud->provider_type = provider;
    cloud->provider_ops  = provider_ops;

    actrust_err_t ret = actrust_queue_create(
        &cloud->downlink_queue, CONFIG_ACTRUST_CLOUD_DOWNLINK_QUEUE_SIZE,
        sizeof(actrust_cloud_msg_t));
    if (ret != ACTRUST_OK) {
        goto fail;
    }

    ret = cloud->provider_ops->init(cloud);
    if (ret != ACTRUST_OK) {
        goto fail;
    }

    *out_downlink_queue = cloud->downlink_queue;
    *out_cloud          = (actrust_cloud_t) cloud;
    LOG_INFO("cloud initialized: provider=%d", (int) provider);
    return ACTRUST_OK;

fail:
    LOG_ERROR("cloud init failed: provider=%d err=0x%08" PRIx32, (int) provider,
              ret);
    if (cloud->downlink_queue != NULL) {
        actrust_queue_destroy(&cloud->downlink_queue);
    }
    ACTRUST_FREE(cloud);
    return ret;
}

actrust_err_t actrust_cloud_deinit(actrust_cloud_t cloud)
{
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t ret = cloud->provider_ops->deinit(cloud);
    if (ret != ACTRUST_OK) {
        return CLOUD_ERR(ACTRUST_ERR_CLOUD_DEINIT_FAILED);
    }

    ret = actrust_queue_destroy(&cloud->downlink_queue);
    if (ret != ACTRUST_OK) {
        return CLOUD_ERR(ACTRUST_ERR_CLOUD_DEINIT_FAILED);
    }

    memset(cloud, 0, sizeof(*cloud));
    ACTRUST_FREE(cloud);

    LOG_INFO("cloud deinitialized");
    return ACTRUST_OK;
}

actrust_err_t actrust_cloud_connect(actrust_cloud_t cloud)
{
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    LOG_INFO("cloud connect requested");
    actrust_err_t ret = cloud->provider_ops->connect(cloud);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("cloud connect failed: 0x%08" PRIx32, ret);
        return CLOUD_ERR(ACTRUST_ERR_CLOUD_START_FAILED);
    }

    LOG_INFO("cloud connected");
    return ACTRUST_OK;
}

actrust_err_t actrust_cloud_disconnect(actrust_cloud_t cloud)
{
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    LOG_INFO("cloud disconnect requested");
    actrust_err_t ret = cloud->provider_ops->disconnect(cloud);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("cloud disconnect failed: 0x%08" PRIx32, ret);
        return CLOUD_ERR(ACTRUST_ERR_CLOUD_STOP_FAILED);
    }

    LOG_INFO("cloud disconnected");
    return ACTRUST_OK;
}

actrust_err_t actrust_cloud_send_data(actrust_cloud_t cloud,
                                      const uint8_t  *payload,
                                      size_t          payload_len)
{
    if (cloud == NULL || payload == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return cloud->provider_ops->send_data(cloud, payload, payload_len);
}

actrust_err_t actrust_cloud_send_register(
    actrust_cloud_t cloud, actrust_cloud_register_msg_type_t type,
    const uint8_t *payload, size_t payload_len)
{
    if (cloud == NULL || payload == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (cloud->provider_ops->send_register == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    return cloud->provider_ops->send_register(cloud, type, payload,
                                              payload_len);
}
