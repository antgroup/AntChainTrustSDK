// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file security.c
 * @brief Android platform security adapter implementation
 *
 * Implements the AntChainTrustSDK security adapter interface using regular
 * files on a POSIX filesystem. Each slot ID maps to a separate binary file
 * under a configurable base directory with restrictive permissions.
 *
 * @warning This file-backed implementation is only a reference/development
 * implementation, not a production secure-store or key-isolation boundary.
 * It does not provide Android Keystore/TEE-backed confidentiality,
 * anti-tamper protection, rollback protection, or non-exportable private-key
 * storage. Products that need to protect private keys must replace or extend
 * this adapter with the target device's actual Android Keystore, TEE, secure
 * element, or equivalent secure storage/key-management service.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/* C standard */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
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
#include "adapter/security.h"

/** @brief Convenience macro to build security-module errors */
#define SEC_ERR(reason)                                                        \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SECURITY, (reason))

/** @brief Directory permission mode (owner-only) */
#define SEC_DIR_MODE ((mode_t) 0700)

/** @brief File permission mode (owner read/write) */
#define SEC_FILE_MODE ((mode_t) 0600)

/** @brief Maximum length for any constructed path */
#define SEC_PATH_MAX 256

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
 */
static bool sec_expand_tilde(const char *path, char *buf, size_t size)
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
    return written >= 0 && (size_t) written < size;
}

/**
 * @brief Ensure a directory and all its parents exist (like @c mkdir @c -p)
 * @param dir Directory path (will NOT be modified)
 * @return @c true on success
 */
static bool sec_ensure_dir(const char *dir)
{
    if (dir == NULL || dir[0] == '\0') {
        return false;
    }

    size_t len = strlen(dir);
    char   tmp[SEC_PATH_MAX];

    if (len >= sizeof(tmp)) {
        return false;
    }

    memcpy(tmp, dir, len + 1u);

    for (char *p = tmp + 1; *p != '\0'; ++p) {
        if (*p != '/') {
            continue;
        }

        *p = '\0';

        if (mkdir(tmp, SEC_DIR_MODE) != 0) {
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

    if (mkdir(tmp, SEC_DIR_MODE) != 0) {
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
 * @brief Build the filesystem path for a given slot.
 * @param[in]  slot_id  Slot identifier.
 * @param[out] path     Destination buffer.
 * @param[in]  size     Size of @p path in bytes.
 * @return @c true on success.
 */
static bool sec_build_path(actrust_sec_slot_t slot_id, char *path, size_t size)
{
    char base[SEC_PATH_MAX];

    if (!sec_expand_tilde(CONFIG_ACTRUST_SEC_STORE_BASE_DIR, base,
                          sizeof(base))) {
        return false;
    }

    if (!sec_ensure_dir(base)) {
        return false;
    }

    int n = snprintf(path, size, "%s/sec_%08x.bin", base, slot_id);
    return n >= 0 && (size_t) n < size;
}

/**
 * @brief Write all bytes to a file descriptor, retrying on EINTR / short
 * writes.
 * @return @c true on success.
 */
static bool sec_write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t written = 0u;

    while (written < len) {
        ssize_t ret = write(fd, buf + written, len - written);

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

/**
 * @brief Read all bytes from a file descriptor, retrying on EINTR / short
 * reads.
 * @return Number of bytes actually read, or @c (size_t)-1 on error.
 */
static size_t sec_read_all(int fd, uint8_t *buf, size_t len)
{
    size_t total = 0u;

    while (total < len) {
        ssize_t n = read(fd, buf + total, len - total);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return (size_t) -1;
        }

        if (n == 0) {
            break;
        }

        total += (size_t) n;
    }

    return total;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

actrust_err_t actrust_sec_store_write(actrust_sec_slot_t slot_id,
                                      const uint8_t *data, size_t len)
{
    if (data == NULL && len > 0u) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char path[SEC_PATH_MAX];
    if (!sec_build_path(slot_id, path, sizeof(path))) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    int fd =
        open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, SEC_FILE_MODE);
    if (fd < 0) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        (void) close(fd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    if (fchmod(fd, SEC_FILE_MODE) != 0) {
        (void) close(fd);
        (void) unlink(path);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    if (!sec_write_all(fd, data, len)) {
        (void) close(fd);
        (void) unlink(path);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    (void) close(fd);
    return ACTRUST_OK;
}

actrust_err_t actrust_sec_store_read(actrust_sec_slot_t slot_id, uint8_t *out,
                                     size_t out_len, size_t *actual_len)
{
    if (out == NULL || actual_len == NULL) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char path[SEC_PATH_MAX];
    if (!sec_build_path(slot_id, path, sizeof(path))) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        return SEC_ERR(ACTRUST_ERR_NO_RESOURCE);
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        (void) close(fd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    size_t file_size = (size_t) st.st_size;

    if (file_size > out_len) {
        (void) close(fd);
        *actual_len = file_size;
        return SEC_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    size_t n = sec_read_all(fd, out, file_size);
    (void) close(fd);

    if (n == (size_t) -1) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    *actual_len = n;
    return ACTRUST_OK;
}

actrust_err_t actrust_sec_store_delete(actrust_sec_slot_t slot_id)
{
    char path[SEC_PATH_MAX];
    if (!sec_build_path(slot_id, path, sizeof(path))) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    if (unlink(path) != 0) {
        if (errno == ENOENT) {
            return SEC_ERR(ACTRUST_ERR_NO_RESOURCE);
        }
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Platform RNG - uses Android's kernel CSPRNG device.
 * ======================================================================== */

actrust_err_t actrust_sec_random(uint8_t *out, size_t len)
{
    if (out == NULL || len == 0u) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    size_t total = 0u;
    while (total < len) {
        size_t chunk = len - total;
        if (chunk > (size_t) SSIZE_MAX) {
            chunk = (size_t) SSIZE_MAX;
        }

        ssize_t n = read(fd, out + total, chunk);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void) close(fd);
            return SEC_ERR(ACTRUST_ERR_IO);
        }
        if (n == 0) {
            (void) close(fd);
            return SEC_ERR(ACTRUST_ERR_IO);
        }
        total += (size_t) n;
    }

    (void) close(fd);
    return ACTRUST_OK;
}

/* ========================================================================
 * Secure Key Management — not available without TEE / SE
 * ======================================================================== */

actrust_err_t actrust_sec_key_generate(actrust_sec_slot_t     slot_id,
                                       actrust_sec_key_type_t type)
{
    (void) slot_id;
    (void) type;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_sec_key_delete(actrust_sec_slot_t slot_id)
{
    (void) slot_id;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_sec_key_get_public(actrust_sec_slot_t slot_id,
                                         uint8_t *out, size_t out_cap,
                                         size_t *out_len)
{
    (void) slot_id;
    (void) out;
    (void) out_cap;
    (void) out_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

/* ========================================================================
 * Secure Hash — not available without TEE / SE
 * ======================================================================== */

actrust_err_t actrust_sec_hash(actrust_sec_hash_alg_t alg, const uint8_t *input,
                               size_t input_len, uint8_t *out, size_t out_cap,
                               size_t *out_len)
{
    (void) alg;
    (void) input;
    (void) input_len;
    (void) out;
    (void) out_cap;
    (void) out_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

/* ========================================================================
 * Secure AES — not available without TEE / SE
 * ======================================================================== */

actrust_err_t actrust_sec_aes_encrypt(
    actrust_sec_slot_t key_slot, actrust_sec_sym_alg_t alg,
    actrust_sec_padding_t padding, const uint8_t *iv, size_t iv_len,
    const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t input_len,
    uint8_t *output, size_t output_cap, size_t *output_len, uint8_t *tag,
    size_t *tag_len)
{
    (void) key_slot;
    (void) alg;
    (void) padding;
    (void) iv;
    (void) iv_len;
    (void) aad;
    (void) aad_len;
    (void) input;
    (void) input_len;
    (void) output;
    (void) output_cap;
    (void) output_len;
    (void) tag;
    (void) tag_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_sec_aes_decrypt(
    actrust_sec_slot_t key_slot, actrust_sec_sym_alg_t alg,
    actrust_sec_padding_t padding, const uint8_t *iv, size_t iv_len,
    const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t input_len,
    uint8_t *output, size_t output_cap, size_t *output_len, const uint8_t *tag,
    size_t tag_len)
{
    (void) key_slot;
    (void) alg;
    (void) padding;
    (void) iv;
    (void) iv_len;
    (void) aad;
    (void) aad_len;
    (void) input;
    (void) input_len;
    (void) output;
    (void) output_cap;
    (void) output_len;
    (void) tag;
    (void) tag_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

/* ========================================================================
 * Secure Crypto — not available without TEE / SE
 * ======================================================================== */

actrust_err_t actrust_sec_ecdsa_sign(actrust_sec_slot_t slot_id,
                                     const uint8_t *digest, size_t dig_len,
                                     uint8_t *sig, size_t sig_cap,
                                     size_t *sig_len)
{
    (void) slot_id;
    (void) digest;
    (void) dig_len;
    (void) sig;
    (void) sig_cap;
    (void) sig_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}

actrust_err_t actrust_sec_ecdsa_verify(actrust_sec_slot_t slot_id,
                                       const uint8_t *digest, size_t dig_len,
                                       const uint8_t *sig, size_t sig_len)
{
    (void) slot_id;
    (void) digest;
    (void) dig_len;
    (void) sig;
    (void) sig_len;
    return SEC_ERR(ACTRUST_ERR_UNSUPPORTED);
}
