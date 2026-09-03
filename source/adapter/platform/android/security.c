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
#include <stdint.h>
#include <string.h>
#include <sys/file.h>
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

actrust_sec_capabilities_t actrust_sec_get_capabilities(void)
{
    return 0u;
}

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

static bool sec_stat_is_directory(const struct stat *st)
{
    return st != NULL && S_ISDIR(st->st_mode) && st->st_uid == geteuid() &&
           (st->st_mode & (mode_t) 0777) == SEC_DIR_MODE;
}

static bool sec_stat_is_directory_component(const struct stat *st)
{
    return st != NULL && S_ISDIR(st->st_mode);
}

static bool sec_stat_is_regular(const struct stat *st)
{
    return st != NULL && S_ISREG(st->st_mode) && st->st_uid == geteuid() &&
           (st->st_mode & (mode_t) 0777) == SEC_FILE_MODE;
}

static int sec_open_store_dir(bool create)
{
    char base[SEC_PATH_MAX];

    if (!sec_expand_tilde(CONFIG_ACTRUST_SEC_STORE_BASE_DIR, base,
                          sizeof(base))) {
        errno = EINVAL;
        return -1;
    }

    const char *cursor = base;
    int         dirfd =
        base[0] == '/'
                    ? open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)
                    : open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dirfd < 0) {
        return -1;
    }

    while (*cursor != '\0') {
        while (*cursor == '/') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        const char *end = cursor;
        while (*end != '\0' && *end != '/') {
            ++end;
        }

        size_t component_len = (size_t) (end - cursor);
        if (component_len == 0u || component_len >= SEC_PATH_MAX ||
            (component_len == 1u && cursor[0] == '.') ||
            (component_len == 2u && cursor[0] == '.' && cursor[1] == '.')) {
            (void) close(dirfd);
            errno = EINVAL;
            return -1;
        }

        char component[SEC_PATH_MAX];
        memcpy(component, cursor, component_len);
        component[component_len] = '\0';

        int nextfd = openat(dirfd, component,
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (nextfd < 0 && errno == ENOENT && create) {
            if (mkdirat(dirfd, component, SEC_DIR_MODE) != 0 &&
                errno != EEXIST) {
                int saved_errno = errno;
                (void) close(dirfd);
                errno = saved_errno;
                return -1;
            }
            nextfd = openat(dirfd, component,
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        if (nextfd < 0) {
            int saved_errno = errno;
            (void) close(dirfd);
            errno = saved_errno;
            return -1;
        }

        struct stat next_st;
        if (fstat(nextfd, &next_st) != 0) {
            int saved_errno = errno;
            (void) close(nextfd);
            (void) close(dirfd);
            errno = saved_errno;
            return -1;
        }
        if (!sec_stat_is_directory_component(&next_st)) {
            (void) close(nextfd);
            (void) close(dirfd);
            errno = EACCES;
            return -1;
        }

        (void) close(dirfd);
        dirfd  = nextfd;
        cursor = end;
    }

    struct stat st;
    if (fstat(dirfd, &st) != 0) {
        int saved_errno = errno;
        (void) close(dirfd);
        errno = saved_errno;
        return -1;
    }
    if (!sec_stat_is_directory(&st)) {
        (void) close(dirfd);
        errno = EACCES;
        return -1;
    }

    return dirfd;
}

static bool sec_make_name(actrust_sec_slot_t slot_id, char *name, size_t size)
{
    int n = snprintf(name, size, "sec_%08x.bin", slot_id);
    return n > 0 && (size_t) n < size;
}

static bool sec_make_temp_name(actrust_sec_slot_t slot_id, char *name,
                               size_t size, unsigned int sequence)
{
    int n = snprintf(name, size, ".sec_%08x.%ld.%u.tmp", slot_id,
                     (long) getpid(), sequence);
    return n > 0 && (size_t) n < size;
}

static bool sec_make_backup_name(actrust_sec_slot_t slot_id, char *name,
                                 size_t size, unsigned int sequence)
{
    int n = snprintf(name, size, ".sec_%08x.%ld.%u.old", slot_id,
                     (long) getpid(), sequence);
    return n > 0 && (size_t) n < size;
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
            return (size_t) -1;
        }

        total += (size_t) n;
    }

    return total;
}

static bool sec_copy_file(int srcfd, int dstfd, off_t size)
{
    uint8_t   buffer[4096];
    uintmax_t remaining = (uintmax_t) size;

    while (remaining > 0u) {
        size_t chunk =
            remaining > sizeof(buffer) ? sizeof(buffer) : (size_t) remaining;
        ssize_t n = read(srcfd, buffer, chunk);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0 || !sec_write_all(dstfd, buffer, (size_t) n)) {
            return false;
        }
        remaining -= (uintmax_t) n;
    }

    return true;
}

static bool sec_create_backup(int dirfd, const char *name,
                              const char *backup_name)
{
    int         srcfd       = -1;
    int         dstfd       = -1;
    bool        created     = false;
    bool        success     = false;
    off_t       source_size = 0;
    struct stat st;

    srcfd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (srcfd < 0 || fstat(srcfd, &st) != 0 || !sec_stat_is_regular(&st) ||
        st.st_size < 0 || (uintmax_t) st.st_size > (uintmax_t) SIZE_MAX) {
        goto cleanup;
    }
    source_size = st.st_size;

    dstfd = openat(dirfd, backup_name,
                   O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                   SEC_FILE_MODE);
    if (dstfd < 0) {
        goto cleanup;
    }
    created = true;

    if (fstat(dstfd, &st) != 0 || !S_ISREG(st.st_mode) ||
        fchmod(dstfd, SEC_FILE_MODE) != 0 || lseek(srcfd, 0, SEEK_SET) < 0 ||
        !sec_copy_file(srcfd, dstfd, source_size) || fsync(dstfd) != 0) {
        goto cleanup;
    }

    success = true;

cleanup: {
    int saved_errno = errno;
    if (srcfd >= 0 && close(srcfd) != 0) {
        success = false;
    }
    if (dstfd >= 0 && close(dstfd) != 0) {
        success = false;
    }
    if (!success && created) {
        (void) unlinkat(dirfd, backup_name, 0);
    }
    if (!success) {
        errno = saved_errno;
    }
}
    return success;
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

    int dirfd = sec_open_store_dir(true);
    if (dirfd < 0) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    if (flock(dirfd, LOCK_EX) != 0) {
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    char name[SEC_PATH_MAX];
    char temp_name[SEC_PATH_MAX];
    if (!sec_make_name(slot_id, name, sizeof(name))) {
        (void) flock(dirfd, LOCK_UN);
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }
    int fd = -1;
    for (unsigned int sequence = 0u; sequence < 100u; ++sequence) {
        if (!sec_make_temp_name(slot_id, temp_name, sizeof(temp_name),
                                sequence)) {
            (void) flock(dirfd, LOCK_UN);
            (void) close(dirfd);
            return SEC_ERR(ACTRUST_ERR_IO);
        }
        fd = openat(dirfd, temp_name,
                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                    SEC_FILE_MODE);
        if (fd >= 0 || errno != EEXIST) {
            break;
        }
    }
    if (fd < 0) {
        (void) flock(dirfd, LOCK_UN);
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    struct stat  st;
    bool         success         = false;
    bool         backup_created  = false;
    bool         renamed         = false;
    bool         old_exists      = false;
    bool         preserve_backup = false;
    char         backup_name[SEC_PATH_MAX];
    unsigned int backup_sequence = 0u;
    if (!sec_make_backup_name(slot_id, backup_name, sizeof(backup_name),
                              backup_sequence)) {
        (void) close(fd);
        (void) unlinkat(dirfd, temp_name, 0);
        (void) flock(dirfd, LOCK_UN);
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }
    actrust_err_t result = SEC_ERR(ACTRUST_ERR_IO);

    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        fchmod(fd, SEC_FILE_MODE) != 0 || !sec_write_all(fd, data, len) ||
        fsync(fd) != 0) {
        goto cleanup;
    }

    if (close(fd) != 0) {
        fd = -1;
        goto cleanup;
    }
    fd = -1;

    if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!sec_stat_is_regular(&st)) {
            errno = EACCES;
            goto cleanup;
        }
        old_exists = true;

        for (backup_sequence = 0u; backup_sequence < 100u; ++backup_sequence) {
            if (!sec_make_backup_name(slot_id, backup_name, sizeof(backup_name),
                                      backup_sequence)) {
                goto cleanup;
            }
            if (sec_create_backup(dirfd, name, backup_name)) {
                backup_created = true;
                break;
            }
            if (errno != EEXIST) {
                goto cleanup;
            }
        }
        if (!backup_created) {
            goto cleanup;
        }
    } else if (errno != ENOENT) {
        goto cleanup;
    }

    if (renameat(dirfd, temp_name, dirfd, name) != 0) {
        goto cleanup;
    }
    renamed = true;

    if (fsync(dirfd) != 0) {
        goto rollback;
    }

    if (backup_created && unlinkat(dirfd, backup_name, 0) != 0) {
        goto rollback;
    }
    backup_created = false;

    success = true;
    result  = ACTRUST_OK;
    goto cleanup;

rollback:
    if (renamed) {
        if (backup_created) {
            if (renameat(dirfd, backup_name, dirfd, name) == 0) {
                backup_created = false;
            } else {
                preserve_backup = true;
            }
        } else if (!old_exists) {
            (void) unlinkat(dirfd, name, 0);
        }
    }
    (void) fsync(dirfd);

cleanup:
    if (fd >= 0) {
        (void) close(fd);
    }
    if (!renamed) {
        (void) unlinkat(dirfd, temp_name, 0);
    }
    if (backup_created && !preserve_backup) {
        (void) unlinkat(dirfd, backup_name, 0);
    }
    (void) flock(dirfd, LOCK_UN);
    (void) close(dirfd);
    return success ? ACTRUST_OK : result;
}

actrust_err_t actrust_sec_store_read(actrust_sec_slot_t slot_id, uint8_t *out,
                                     size_t out_len, size_t *actual_len)
{
    if (out == NULL || actual_len == NULL) {
        return SEC_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    int dirfd = sec_open_store_dir(false);
    if (dirfd < 0) {
        return SEC_ERR(errno == ENOENT ? ACTRUST_ERR_NO_RESOURCE
                                       : ACTRUST_ERR_IO);
    }

    char name[SEC_PATH_MAX];
    if (!sec_make_name(slot_id, name, sizeof(name))) {
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    int fd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        actrust_err_t result =
            SEC_ERR(errno == ENOENT ? ACTRUST_ERR_NO_RESOURCE : ACTRUST_ERR_IO);
        (void) close(dirfd);
        return result;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || !sec_stat_is_regular(&st) || st.st_size < 0 ||
        (uintmax_t) st.st_size > (uintmax_t) SIZE_MAX) {
        (void) close(fd);
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    size_t file_size = (size_t) st.st_size;
    if (file_size > out_len) {
        (void) close(fd);
        (void) close(dirfd);
        *actual_len = file_size;
        return SEC_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    size_t n        = sec_read_all(fd, out, file_size);
    bool   close_ok = close(fd) == 0;
    (void) close(dirfd);

    if (n == (size_t) -1 || !close_ok) {
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    *actual_len = n;
    return ACTRUST_OK;
}

actrust_err_t actrust_sec_store_delete(actrust_sec_slot_t slot_id)
{
    int dirfd = sec_open_store_dir(false);
    if (dirfd < 0) {
        return SEC_ERR(errno == ENOENT ? ACTRUST_ERR_NO_RESOURCE
                                       : ACTRUST_ERR_IO);
    }

    if (flock(dirfd, LOCK_EX) != 0) {
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    char name[SEC_PATH_MAX];
    if (!sec_make_name(slot_id, name, sizeof(name))) {
        (void) flock(dirfd, LOCK_UN);
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    struct stat st;
    if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        actrust_err_t result =
            SEC_ERR(errno == ENOENT ? ACTRUST_ERR_NO_RESOURCE : ACTRUST_ERR_IO);
        (void) flock(dirfd, LOCK_UN);
        (void) close(dirfd);
        return result;
    }
    if (!sec_stat_is_regular(&st)) {
        (void) flock(dirfd, LOCK_UN);
        (void) close(dirfd);
        return SEC_ERR(ACTRUST_ERR_IO);
    }

    if (unlinkat(dirfd, name, 0) != 0) {
        actrust_err_t result =
            SEC_ERR(errno == ENOENT ? ACTRUST_ERR_NO_RESOURCE : ACTRUST_ERR_IO);
        (void) flock(dirfd, LOCK_UN);
        (void) close(dirfd);
        return result;
    }

    bool sync_ok  = fsync(dirfd) == 0;
    bool close_ok = (flock(dirfd, LOCK_UN) == 0) && (close(dirfd) == 0);
    return sync_ok && close_ok ? ACTRUST_OK : SEC_ERR(ACTRUST_ERR_IO);
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
