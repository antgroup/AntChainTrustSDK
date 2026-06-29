// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file storage.c
 * @brief SIMCom A7606E-H platform storage adapter implementation
 *
 * Implements the AntChainTrustSDK storage interface using regular files on a
 * POSIX filesystem.  The A7606E-H runs OpenWrt Linux with a writable /data
 * partition (UBI/UBIFS).  Each storage ID maps to a separate binary file
 * under a configurable base directory.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/* C standard */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Project */
#include "actrust_config.h"
#include "actrust_errno.h"

/* Adapter */
#include "adapter/storage.h"

/** @brief Convenience macro to build storage-module errors */
#define STORAGE_ERR(reason)                                                    \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_STORAGE, (reason))

/** @brief Directory permission mode (owner-only) */
#define STORAGE_DIR_MODE ((mode_t) 0700)

/** @brief File permission mode (owner read/write) */
#define STORAGE_FILE_MODE ((mode_t) 0600)

/**
 * @brief Convert between actrust_storage_t and file descriptor.
 *
 * Keep NULL reserved as the invalid public handle while still allowing POSIX
 * fd 0, which is a valid descriptor.
 */
#define STORAGE_TO_FD(st) ((int) ((intptr_t) (st) - 1))
#define FD_TO_STORAGE(fd) ((actrust_storage_t) (intptr_t) ((fd) + 1))

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

/**
 * @brief Expand a leading '~' to the user's home directory
 * @param[in]  path  Original path (may start with '~/' or '~').
 * @param[out] buf   Destination buffer for the expanded path.
 * @param[in]  size  Size of @p buf in bytes.
 * @return @c true on success, @c false if the HOME variable is unset or
 *         the expanded path would exceed @p size.
 * @note Only the leading "~/" (or a bare "~") is expanded.
 */
static bool storage_expand_tilde(const char *path, char *buf, size_t size)
{
    if (path == NULL || buf == NULL || size == 0u) {
        return false;
    }

    if (path[0] != '~') {
        size_t path_len = strlen(path);
        if (path_len >= size) {
            return false;
        }

        memcpy(buf, path, path_len + 1u);
        return true;
    }

    if (path[1] != '\0' && path[1] != '/') {
        return false;
    }

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return false;
    }

    int written = snprintf(buf, size, "%s%s", home, path + 1);
    if (written < 0 || (size_t) written >= size) {
        return false;
    }

    return true;
}

/**
 * @brief Ensure a directory and all its parents exist (like @c mkdir @c -p)
 * @param dir Directory path (will NOT be modified)
 * @return @c true on success
 */
static bool storage_ensure_dir(const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return false;
    }

    size_t len = strlen(dir);
    char   tmp[256];

    if (len >= sizeof(tmp)) {
        return false;
    }

    memcpy(tmp, dir, len + 1u);

    for (char *p = tmp + 1; *p != '\0'; ++p) {
        if (*p != '/') {
            continue;
        }

        *p = '\0';

        if (mkdir(tmp, STORAGE_DIR_MODE) != 0) {
            if (errno != EEXIST) {
                return false;
            }

            struct stat st;
            if (lstat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
                return false;
            }
        }

        *p = '/';
    }

    if (mkdir(tmp, STORAGE_DIR_MODE) != 0) {
        if (errno != EEXIST) {
            return false;
        }

        struct stat st;
        if (lstat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Positioned read with zero-fill past EOF
 */
static bool storage_pread(int fd, uint32_t offset, uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return true;
    }

    if (buf == NULL) {
        return false;
    }

    size_t total = 0u;

    while (total < len) {
        ssize_t n =
            pread(fd, buf + total, len - total, (off_t) offset + (off_t) total);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (n == 0) {
            break;
        }

        total += (size_t) n;
    }

    if (total < len) {
        memset(buf + total, 0, len - total);
    }

    return true;
}

/**
 * @brief Positioned write with automatic retry
 */
static bool storage_pwrite(int fd, uint32_t offset, const uint8_t *buf,
                           size_t len)
{
    if (len == 0u) {
        return true;
    }

    if (buf == NULL) {
        return false;
    }

    size_t written = 0u;

    while (written < len) {
        ssize_t ret = pwrite(fd, buf + written, len - written,
                             (off_t) offset + (off_t) written);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (ret == 0) {
            return false;
        }

        written += (size_t) ret;
    }

    return true;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

actrust_err_t actrust_storage_open(actrust_storage_t   *out,
                                   actrust_storage_id_t id)
{
    if (out == NULL) {
        return STORAGE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char base[256];

    if (!storage_expand_tilde(CONFIG_ACTRUST_STORAGE_BASE_DIR, base,
                              sizeof(base))) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    if (!storage_ensure_dir(base)) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    char path[256];
    int  n = snprintf(path, sizeof(path), "%s/%s%08x.bin", base,
                      CONFIG_ACTRUST_STORAGE_FILE_PREFIX, id);

    if (n < 0 || (size_t) n >= sizeof(path)) {
        return STORAGE_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, STORAGE_FILE_MODE);
    if (fd < 0) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        (void) close(fd);
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    if (fchmod(fd, STORAGE_FILE_MODE) != 0) {
        (void) close(fd);
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    *out = FD_TO_STORAGE(fd);
    return ACTRUST_OK;
}

actrust_err_t actrust_storage_close(actrust_storage_t st)
{
    if (st == NULL) {
        return STORAGE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    (void) close(STORAGE_TO_FD(st));
    return ACTRUST_OK;
}

actrust_err_t actrust_storage_read(actrust_storage_t st, uint32_t offset,
                                   uint8_t *buf, size_t len)
{
    if (st == NULL || (buf == NULL && len > 0u)) {
        return STORAGE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (!storage_pread(STORAGE_TO_FD(st), offset, buf, len)) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    return ACTRUST_OK;
}

actrust_err_t actrust_storage_write(actrust_storage_t st, uint32_t offset,
                                    const uint8_t *buf, size_t len)
{
    if (st == NULL || (buf == NULL && len > 0u)) {
        return STORAGE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    int fd = STORAGE_TO_FD(st);

    if (!storage_pwrite(fd, offset, buf, len)) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    /* Persist to media: without fsync() a write can sit in the page cache
     * indefinitely and be lost on power-cut. */
    if (len > 0u && fsync(fd) != 0) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    return ACTRUST_OK;
}

actrust_err_t actrust_storage_erase(actrust_storage_t st, uint32_t offset,
                                    uint32_t len)
{
    if (st == NULL) {
        return STORAGE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (len == 0u) {
        return ACTRUST_OK;
    }

    uint8_t zeros[256];
    memset(zeros, 0, sizeof(zeros));

    uint32_t remaining = len;
    uint32_t pos       = offset;

    int fd = STORAGE_TO_FD(st);

    while (remaining > 0u) {
        uint32_t chunk = (remaining > (uint32_t) sizeof(zeros))
                             ? (uint32_t) sizeof(zeros)
                             : remaining;

        if (!storage_pwrite(fd, pos, zeros, chunk)) {
            return STORAGE_ERR(ACTRUST_ERR_IO);
        }

        pos += chunk;
        remaining -= chunk;
    }

    /* Persist the erase. See actrust_storage_write() for the rationale. */
    if (fsync(fd) != 0) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    return ACTRUST_OK;
}
