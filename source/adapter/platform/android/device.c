// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file device.c
 * @brief Android platform device adapter implementation.
 */

/* C standard */
#include <stdio.h>
#include <string.h>

/* Android */
#include <sys/system_properties.h>

/* Adapter */
#include "adapter/device.h"

#define DEV_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_DEVICE, (reason))

/**
 * @brief Read a system property into a local buffer.
 */
static int read_property_raw(const char *name, char *value)
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    int rc = __system_property_get(name, value);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return rc;
}

static actrust_err_t copy_string(char *buf, size_t len, const char *value)
{
    if (buf == NULL || len == 0u || value == NULL) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t value_len = strlen(value);
    if (value_len + 1u > len) {
        return DEV_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    memcpy(buf, value, value_len + 1u);
    return ACTRUST_OK;
}

static actrust_err_t copy_property(const char *name, char *buf, size_t len)
{
    if (name == NULL || buf == NULL || len == 0u) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char value[PROP_VALUE_MAX];
    int  value_len = read_property_raw(name, value);
    if (value_len <= 0 || value[0] == '\0') {
        return DEV_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    return copy_string(buf, len, value);
}

static actrust_err_t copy_first_property(const char *const *names, char *buf,
                                         size_t len)
{
    if (names == NULL || buf == NULL || len == 0u) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    for (size_t i = 0u; names[i] != NULL; ++i) {
        actrust_err_t err = copy_property(names[i], buf, len);
        if (err == ACTRUST_OK ||
            ACTRUST_ERR_CODE(err) == ACTRUST_ERR_BUF_TOO_SMALL) {
            return err;
        }
    }

    return DEV_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_get_hw_id(char *buf, size_t len)
{
    static const char *const id_props[] = {
        "ro.serialno",
        "ro.boot.serialno",
        "ro.boot.hardware.serialno",
        NULL,
    };

    if (buf == NULL || len == 0u) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return copy_first_property(id_props, buf, len);
}

actrust_err_t actrust_get_hw_model(char *buf, size_t len)
{
    static const char *const model_props[] = {
        "ro.product.model",
        "ro.product.device",
        "ro.hardware",
        NULL,
    };

    if (buf == NULL || len == 0u) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return copy_first_property(model_props, buf, len);
}

actrust_err_t actrust_get_fw_version(char *buf, size_t len)
{
    if (buf == NULL || len == 0u) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char release[PROP_VALUE_MAX];
    char sdk[PROP_VALUE_MAX];
    int  release_len = read_property_raw("ro.build.version.release", release);
    int  sdk_len     = read_property_raw("ro.build.version.sdk", sdk);

    if (release_len > 0 && release[0] != '\0' && sdk_len > 0 &&
        sdk[0] != '\0') {
        char value[(PROP_VALUE_MAX * 2) + 16];
        int  n =
            snprintf(value, sizeof(value), "Android %s (SDK %s)", release, sdk);
        if (n < 0 || (size_t) n >= sizeof(value)) {
            return DEV_ERR(ACTRUST_ERR_IO);
        }
        return copy_string(buf, len, value);
    }

    if (release_len > 0 && release[0] != '\0') {
        return copy_string(buf, len, release);
    }

    return copy_property("ro.build.version.incremental", buf, len);
}
