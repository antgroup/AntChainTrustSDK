// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file kv.h
 * @brief Key-Value persistent storage component
 *
 * This module provides a namespace-scoped, thread-safe key-value store built
 * on top of the AntChainTrustSDK storage adapter layer.  Typical usage:
 *
 * @code{.c}
 *   actrust_kv_init();                               // once at boot
 *
 *   actrust_kv_t kv;
 *   actrust_kv_open("cfg", 3, &kv);                  // open namespace
 *
 *   actrust_kv_set(kv, "ssid", 4, "MyWiFi", 6);      // store
 *
 *   char buf[64];
 *   size_t len;
 *   actrust_kv_get(kv, "ssid", 4, buf, sizeof(buf), &len);  // retrieve
 *
 *   actrust_kv_close(kv);                             // release
 * @endcode
 */

#ifndef ACTRUST_KV_H
#define ACTRUST_KV_H

#ifdef __cplusplus
extern "C" {
#endif

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_config.h"
#include "actrust_errno.h"

/** @brief Opaque KV instance handle.  Obtained via @ref actrust_kv_open. */
typedef struct actrust_kv *actrust_kv_t;

/**
 * @brief Initialise all configured KV namespaces.
 *
 * For each namespace in the compile-time map this function opens the
 * corresponding storage region and formats it **only if it has not been
 * formatted yet** (i.e. the magic / version do not match).  Already
 * initialised regions are left untouched so that persistent data survives
 * across reboots.
 *
 * Must be called once during system startup before any @ref actrust_kv_open.
 *
 * @return ACTRUST_OK on success.
 * @return A KV or storage-layer error code on failure.
 */
actrust_err_t actrust_kv_init(void);

/**
 * @brief Open a KV namespace and return a handle.
 *
 * The namespace must have been previously initialised by @ref actrust_kv_init.
 * The returned handle owns a storage connection and a mutex; the caller is
 * responsible for releasing them via @ref actrust_kv_close.
 *
 * @param[in]  ns      Namespace string (not necessarily NUL-terminated).
 * @param[in]  ns_len  Length of @p ns in bytes.  Must be > 0 and
 *                     < @c CONFIG_ACTRUST_KV_NAMESPACE_MAX_LEN.
 * @param[out] out_kv  Receives the KV handle on success.
 *
 * @return ACTRUST_OK on success.
 * @return Error with reason @c ACTRUST_ERR_INVALID_ARG if any argument is
 *         invalid or the namespace is not in the compiled-in map.
 * @return Error with reason @c ACTRUST_ERR_NO_MEM if heap allocation fails.
 * @return Error with reason @c ACTRUST_ERR_BAD_STATE if the storage has not
 *         been formatted by @ref actrust_kv_init.
 */
actrust_err_t actrust_kv_open(const char *ns, size_t ns_len,
                              actrust_kv_t *out_kv);

/**
 * @brief Close a KV handle, releasing all associated resources.
 *
 * After this call the handle is invalid and must not be used.  Best-effort
 * cleanup is performed: if the lock cannot be acquired the storage and mutex
 * are still torn down, and the first error encountered is returned.
 *
 * @param[in] kv  KV handle previously returned by @ref actrust_kv_open.
 *
 * @return ACTRUST_OK on success.
 * @return The first error encountered during cleanup on failure.
 */
actrust_err_t actrust_kv_close(actrust_kv_t kv);

/**
 * @brief Store or overwrite a key-value pair.
 *
 * If the key already exists its value is updated in-place.  If the key does
 * not exist a new record is allocated from the first free slot.
 *
 * @param[in] kv         KV handle.
 * @param[in] key        Key bytes (must not be NULL).
 * @param[in] key_len    Key length in bytes
 *                       (1 .. @c CONFIG_ACTRUST_KV_MAX_KEY_LEN).
 * @param[in] value      Value bytes (may be NULL when @p value_len is 0).
 * @param[in] value_len  Value length in bytes
 *                       (0 .. @c CONFIG_ACTRUST_KV_MAX_VALUE_LEN).
 *
 * @return ACTRUST_OK on success.
 * @return Error with reason @c ACTRUST_ERR_INVALID_ARG if any argument is
 *         out of range.
 * @return Error with reason @c ACTRUST_ERR_NO_RESOURCE if no free record slot
 *         is available.
 * @return Error with reason @c ACTRUST_ERR_IO if a storage read fails or a
 *         record fails persistent metadata or CRC validation.
 */
actrust_err_t actrust_kv_set(actrust_kv_t kv, const char *key, size_t key_len,
                             const void *value, size_t value_len);

/**
 * @brief Retrieve the value associated with a key.
 *
 * On success @p *out_len is set to the actual value length.  If the caller's
 * buffer is too small the function returns @c ACTRUST_ERR_BUF_TOO_SMALL but
 * still populates @p *out_len so the caller can retry with a larger buffer.
 *
 * @param[in]  kv         KV handle.
 * @param[in]  key        Key bytes.
 * @param[in]  key_len    Key length in bytes.
 * @param[out] out_value  Buffer to receive the value (may be NULL when
 *                        @p out_cap is 0, to query the required size).
 * @param[in]  out_cap    Size of @p out_value in bytes.
 * @param[out] out_len    Receives the actual value length.  Always set when
 *                        the return value is ACTRUST_OK or
 * ACTRUST_ERR_BUF_TOO_SMALL.
 *
 * @return ACTRUST_OK on success.
 * @return Error with reason @c ACTRUST_ERR_NO_RESOURCE if the key does not
 *         exist.
 * @return Error with reason @c ACTRUST_ERR_IO if a record cannot be read or
 *         fails persistent metadata or CRC validation.
 * @return Error with reason @c ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap is
 *         smaller than the actual value length.
 */
actrust_err_t actrust_kv_get(actrust_kv_t kv, const char *key, size_t key_len,
                             void *out_value, size_t out_cap, size_t *out_len);

/**
 * @brief Delete a key-value pair.
 *
 * @param[in] kv       KV handle.
 * @param[in] key      Key bytes.
 * @param[in] key_len  Key length in bytes.
 *
 * @return ACTRUST_OK on success.
 * @return Error with reason @c ACTRUST_ERR_INVALID_ARG if any argument is
 *         invalid.
 * @return Error with reason @c ACTRUST_ERR_NO_RESOURCE if the key does not
 *         exist.
 * @return Error with reason @c ACTRUST_ERR_IO if a storage read fails or a
 *         record fails persistent metadata or CRC validation.
 */
actrust_err_t actrust_kv_del(actrust_kv_t kv, const char *key, size_t key_len);

/**
 * @brief Check whether a key exists in the namespace.
 *
 * This is a lightweight alternative to @ref actrust_kv_get when only the
 * existence of a key needs to be determined (no data is copied).
 *
 * @param[in]  kv         KV handle.
 * @param[in]  key        Key bytes.
 * @param[in]  key_len    Key length in bytes.
 * @param[out] out_exist  Set to @c true if the key exists, @c false otherwise.
 *
 * @return ACTRUST_OK on success (regardless of whether the key exists).
 * @return Error with reason @c ACTRUST_ERR_IO if a storage read fails or a
 *         record fails persistent metadata or CRC validation.
 */
actrust_err_t actrust_kv_exists(actrust_kv_t kv, const char *key,
                                size_t key_len, bool *out_exist);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_KV_H */
