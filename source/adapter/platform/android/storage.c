// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file storage.c
 * @brief Android platform storage adapter implementation
 *
 * Implements the AntChainTrustSDK storage interface using regular files on a
 * POSIX filesystem.  Each storage ID maps to a separate binary file under a
 * configurable base directory, which should point at an app-private writable
 * directory for production Android applications.
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
typedef struct {
    int  fd;
    bool closed;
} storage_handle_t;

#define STORAGE_TO_HANDLE(st) ((storage_handle_t *) (st))
#define STORAGE_TO_FD(st)     (STORAGE_TO_HANDLE(st)->fd)

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

static bool storage_valid_range(uint32_t offset, size_t len)
{
    return offset <= (uint32_t) CONFIG_ACTRUST_STORAGE_REGION_SIZE &&
           len <= (size_t) CONFIG_ACTRUST_STORAGE_REGION_SIZE - offset;
}

static bool storage_valid_handle(actrust_storage_t st)
{
    return st != NULL && !STORAGE_TO_HANDLE(st)->closed &&
           STORAGE_TO_FD(st) >= 0;
}

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
 * @brief Ensure a directory path exists and open it without following symlinks
 * @param dir Directory path (will NOT be modified)
 * @return Open directory descriptor on success, @c -1 on failure
 * @note Each path component is opened relative to its verified parent.
 */
static int storage_open_dir(const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return -1;
    }

    int current_fd =
        open(dir[0] == '/' ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (current_fd < 0) {
        return -1;
    }

    const char *cursor = dir;
    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        const char *end = cursor;
        while (*end != '\0' && *end != '/') {
            end++;
        }
        size_t len = (size_t) (end - cursor);
        char   component[256];
        if (len == 0u || len >= sizeof(component)) {
            (void) close(current_fd);
            return -1;
        }
        memcpy(component, cursor, len);
        component[len] = '\0';
        if (mkdirat(current_fd, component, STORAGE_DIR_MODE) != 0 &&
            errno != EEXIST) {
            (void) close(current_fd);
            return -1;
        }
        int next_fd = openat(current_fd, component,
                             O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next_fd < 0) {
            (void) close(current_fd);
            return -1;
        }
        (void) close(current_fd);
        current_fd = next_fd;
        cursor     = end;
    }
    return current_fd;
}

/**
 * @brief Positioned read with zero-fill past EOF
 * @param fd   Open file descriptor
 * @param offset Byte offset
 * @param buf  Destination buffer
 * @param len  Bytes to read
 * @return @c true on success
 * @return @c false if buf is NULL with len > 0 or if the read operation fails
 * @note Uses POSIX pread() for atomic positioned reads.
 *       Automatically handles short reads and signal interrupts (EINTR).
 *       Bytes beyond the end of stored data are zero-filled.
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
 * @param fd   Open file descriptor
 * @param offset Byte offset
 * @param buf  Source buffer
 * @param len  Bytes to write
 * @return @c true on success
 * @return @c false if buf is NULL with len > 0 or if the write operation fails
 * @note Uses POSIX pwrite() for atomic positioned writes.
 *       Automatically handles partial writes and signal interrupts (EINTR).
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

    /* Expand leading '~' in the configured base directory. */
    char base[256];

    if (!storage_expand_tilde(CONFIG_ACTRUST_STORAGE_BASE_DIR, base,
                              sizeof(base))) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    int dir_fd = storage_open_dir(base);
    if (dir_fd < 0) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    char filename[64];
    int  n = snprintf(filename, sizeof(filename), "%s%08x.bin",
                      CONFIG_ACTRUST_STORAGE_FILE_PREFIX, id);
    if (n < 0 || (size_t) n >= sizeof(filename)) {
        (void) close(dir_fd);
        return STORAGE_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    int fd = openat(dir_fd, filename, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                    STORAGE_FILE_MODE);
    (void) close(dir_fd);
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

    if ((uint64_t) st.st_size < (uint64_t) CONFIG_ACTRUST_STORAGE_REGION_SIZE &&
        ftruncate(fd, (off_t) CONFIG_ACTRUST_STORAGE_REGION_SIZE) != 0) {
        (void) close(fd);
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    storage_handle_t *handle = calloc(1u, sizeof(*handle));
    if (handle == NULL) {
        (void) close(fd);
        return STORAGE_ERR(ACTRUST_ERR_NO_MEM);
    }
    handle->fd = fd;
    *out       = (actrust_storage_t) handle;
    return ACTRUST_OK;
}

actrust_err_t actrust_storage_close(actrust_storage_t st)
{
    if (!storage_valid_handle(st)) {
        return STORAGE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    storage_handle_t *handle = STORAGE_TO_HANDLE(st);
    if (close(handle->fd) != 0) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }
    handle->closed = true;
    free(handle);
    return ACTRUST_OK;
}

actrust_err_t actrust_storage_get_capacity(actrust_storage_t st,
                                           uint32_t         *capacity)
{
    if (!storage_valid_handle(st) || capacity == NULL) {
        return STORAGE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    struct stat info;
    if (fstat(STORAGE_TO_FD(st), &info) != 0) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }

    *capacity = (uint32_t) info.st_size;
    return ACTRUST_OK;
}

actrust_err_t actrust_storage_sync(actrust_storage_t st)
{
    if (!storage_valid_handle(st)) {
        return STORAGE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (fsync(STORAGE_TO_FD(st)) != 0) {
        return STORAGE_ERR(ACTRUST_ERR_IO);
    }
    return ACTRUST_OK;
}

actrust_err_t actrust_storage_read(actrust_storage_t st, uint32_t offset,
                                   uint8_t *buf, size_t len)
{
    if (!storage_valid_handle(st) || !storage_valid_range(offset, len) ||
        (buf == NULL && len > 0u)) {
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
    if (!storage_valid_handle(st) || !storage_valid_range(offset, len) ||
        (buf == NULL && len > 0u)) {
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
    if (!storage_valid_handle(st) || !storage_valid_range(offset, len)) {
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
