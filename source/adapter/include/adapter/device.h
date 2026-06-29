// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file adapter/device.h
 * @brief Platform device information abstraction layer interface
 *
 * Provides read-only accessors for hardware identity, model, revision,
 * and firmware version.  Platform adapters implement these to return
 * device-specific strings.
 */

#ifndef ACTRUST_DEVICE_H
#define ACTRUST_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* C standard */
#include <stddef.h>

/* Project */
#include "actrust_errno.h"

/**
 * @defgroup adapter_device Device Information
 * @{
 */

/**
 * @brief Get the hardware unique identifier
 *
 * Returns a platform-specific unique ID string (e.g. chip UID, IMEI)
 * used as the device logical identity primary key.
 *
 * @param[out] buf   Buffer to receive the NUL-terminated ID string
 * @param[in]  len   Capacity of @p buf in bytes (including NUL)
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p buf is NULL or @p len is 0
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p len is insufficient
 * @return @c ACTRUST_ERR_UNSUPPORTED if not implemented on this platform
 */
actrust_err_t actrust_get_hw_id(char *buf, size_t len);

/**
 * @brief Get the hardware model name
 *
 * Returns the hardware model string (e.g. "A7606E-H", "ESP32-S3").
 *
 * @param[out] buf   Buffer to receive the NUL-terminated model string
 * @param[in]  len   Capacity of @p buf in bytes (including NUL)
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p buf is NULL or @p len is 0
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p len is insufficient
 * @return @c ACTRUST_ERR_UNSUPPORTED if not implemented on this platform
 */
actrust_err_t actrust_get_hw_model(char *buf, size_t len);

/**
 * @brief Get the firmware version
 *
 * Returns the firmware version string (e.g. "1.0.0", "2.3.1-rc1").
 *
 * @param[out] buf   Buffer to receive the NUL-terminated version string
 * @param[in]  len   Capacity of @p buf in bytes (including NUL)
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p buf is NULL or @p len is 0
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p len is insufficient
 * @return @c ACTRUST_ERR_UNSUPPORTED if not implemented on this platform
 */
actrust_err_t actrust_get_fw_version(char *buf, size_t len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_DEVICE_H */
