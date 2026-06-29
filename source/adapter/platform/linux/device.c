// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file device.c
 * @brief Linux platform device adapter implementation.
 */

/* C standard */
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Adapter */
#include "adapter/device.h"

#define DEV_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_DEVICE, (reason))

#ifndef ACTRUST_LINUX_HW_ID_PATH
#define ACTRUST_LINUX_HW_ID_PATH "/etc/machine-id"
#endif

#ifndef ACTRUST_LINUX_DMI_BOARD_NAME_PATH
#define ACTRUST_LINUX_DMI_BOARD_NAME_PATH "/sys/class/dmi/id/board_name"
#endif

#ifndef ACTRUST_LINUX_DEVICE_TREE_MODEL_PATH
#define ACTRUST_LINUX_DEVICE_TREE_MODEL_PATH "/proc/device-tree/model"
#endif

#ifndef ACTRUST_LINUX_OS_RELEASE_PATH
#define ACTRUST_LINUX_OS_RELEASE_PATH "/etc/os-release"
#endif

/**
 * @brief Read a single-line sysfs/procfs file into buf.
 *
 * The Linux DMI files are newline-terminated, while device-tree strings are
 * commonly NUL-terminated.  Both terminators are stripped from the returned
 * string.
 */
static actrust_err_t read_sysfs(const char *path, char *buf, size_t len)
{
    if (path == NULL || buf == NULL || len == 0u || len > (size_t) INT_MAX) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return DEV_ERR(ACTRUST_ERR_IO);
    }

    size_t  used = 0u;
    int32_t ch   = EOF;
    while ((ch = fgetc(fp)) != EOF) {
        if (ch == '\n' || ch == '\0') {
            break;
        }
        if (used + 1u >= len) {
            fclose(fp);
            return DEV_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
        }
        buf[used++] = (char) ch;
    }

    if (ferror(fp) || used == 0u) {
        fclose(fp);
        return DEV_ERR(ACTRUST_ERR_IO);
    }

    buf[used] = '\0';
    fclose(fp);
    return ACTRUST_OK;
}

static actrust_err_t read_first_sysfs(const char *const *paths, char *buf,
                                      size_t len)
{
    if (paths == NULL || buf == NULL || len == 0u) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    for (size_t i = 0u; paths[i] != NULL; ++i) {
        actrust_err_t err = read_sysfs(paths[i], buf, len);
        if (err == ACTRUST_OK ||
            ACTRUST_ERR_CODE(err) == ACTRUST_ERR_BUF_TOO_SMALL) {
            return err;
        }
    }

    return DEV_ERR(ACTRUST_ERR_IO);
}

actrust_err_t actrust_get_hw_id(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return read_sysfs(ACTRUST_LINUX_HW_ID_PATH, buf, len);
}

actrust_err_t actrust_get_hw_model(char *buf, size_t len)
{
    static const char *const model_paths[] = {
        ACTRUST_LINUX_DMI_BOARD_NAME_PATH,
        ACTRUST_LINUX_DEVICE_TREE_MODEL_PATH,
        NULL,
    };

    if (buf == NULL || len == 0) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return read_first_sysfs(model_paths, buf, len);
}

actrust_err_t actrust_get_fw_version(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    FILE *fp = fopen(ACTRUST_LINUX_OS_RELEASE_PATH, "r");
    if (fp == NULL) {
        return DEV_ERR(ACTRUST_ERR_IO);
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PRETTY_NAME=", strlen("PRETTY_NAME=")) == 0) {
            fclose(fp);
            char *val = line + strlen("PRETTY_NAME=");
            if (*val == '"') {
                val++;
            }
            size_t vlen = strlen(val);
            if (vlen > 0 && val[vlen - 1] == '\n') {
                val[--vlen] = '\0';
            }
            if (vlen > 0 && val[vlen - 1] == '"') {
                val[--vlen] = '\0';
            }
            if (vlen >= len) {
                return DEV_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
            }
            memcpy(buf, val, vlen + 1u);
            return ACTRUST_OK;
        }
    }

    fclose(fp);
    return DEV_ERR(ACTRUST_ERR_IO);
}
