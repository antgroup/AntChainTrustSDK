// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file device.c
 * @brief SIMCom A7606E-H platform device adapter implementation
 *
 * Provides device identity accessors for the A7606E-H Cat-1 module running
 * OpenWrt Linux on an ASR1806 SoC.  Hardware ID is the IMEI retrieved via
 * the SIMCom SDK (libsdk.a).  Model is read from /proc/device-tree/model;
 * firmware version is read from /etc/mversion.
 */

/* C standard */
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Adapter */
#include "adapter/device.h"

#define DEV_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_DEVICE, (reason))

/** @brief IMEI string length (15 digits) */
#define IMEI_LENGTH 15

/*
 * Declare the SIMCom SDK functions directly to avoid pulling in ATControl.h
 * and its heavy dependency chain (uci.h, pthread.h, etc.).
 */
extern int  atctrl_init(void);
extern void atctrl_deinit(void);
extern int  getIMEI(char *pBuff, int size);

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

/**
 * @brief Read a single-line sysfs file into buf, stripping trailing newline.
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

    if (!fgets(buf, (int) len, fp)) {
        fclose(fp);
        return DEV_ERR(ACTRUST_ERR_IO);
    }

    actrust_err_t err  = ACTRUST_OK;
    size_t        slen = strlen(buf);
    if (slen > 0 && buf[slen - 1] == '\n') {
        buf[slen - 1] = '\0';
    } else if (!feof(fp)) {
        err = DEV_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }
    fclose(fp);
    return err;
}

static actrust_err_t validate_imei(const char *buf, size_t len)
{
    const char *end = (const char *) memchr(buf, '\0', len);
    if (end == NULL) {
        return DEV_ERR(ACTRUST_ERR_HW_BAD_DATA);
    }

    size_t slen = (size_t) (end - buf);
    if (slen != IMEI_LENGTH) {
        return DEV_ERR(ACTRUST_ERR_HW_BAD_DATA);
    }
    for (size_t i = 0; i < slen; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            return DEV_ERR(ACTRUST_ERR_HW_BAD_DATA);
        }
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

actrust_err_t actrust_get_hw_id(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (len <= IMEI_LENGTH) {
        return DEV_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }
    if (len > (size_t) INT_MAX) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (atctrl_init() != 0) {
        return DEV_ERR(ACTRUST_ERR_IO);
    }

    int rc = getIMEI(buf, (int) len);
    atctrl_deinit();

    if (rc != 0) {
        return DEV_ERR(ACTRUST_ERR_IO);
    }

    return validate_imei(buf, len);
}

actrust_err_t actrust_get_hw_model(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return read_sysfs("/proc/device-tree/model", buf, len);
}

actrust_err_t actrust_get_fw_version(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return DEV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return read_sysfs("/etc/mversion", buf, len);
}
