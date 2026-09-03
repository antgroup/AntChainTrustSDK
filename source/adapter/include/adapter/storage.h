// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file adapter/storage.h
 * @brief Platform storage abstraction layer interface
 *
 * Provides a uniform block-level storage API that platform adapters implement.
 * Each storage instance is identified by a numeric ID and supports
 * random-access read, write, and erase operations on byte-addressable regions.
 * Implementations must provide the logical region capacity and a persistence
 * barrier through @ref actrust_storage_get_capacity and
 * @ref actrust_storage_sync.
 */

#ifndef ACTRUST_STORAGE_H
#define ACTRUST_STORAGE_H

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup adapter_storage Platform Storage
 * @{
 */

/** @brief Storage handle (opaque pointer) */
typedef void *actrust_storage_t;

/** @brief Storage region identifier */
typedef uint32_t actrust_storage_id_t;

/**
 * @brief Open a storage region
 *
 * @param[out] out Pointer to storage handle, receives handle on success
 * @param[in]  id  Storage region identifier
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p out is NULL
 * @return @c ACTRUST_ERR_NO_MEM if insufficient memory
 * @return @c ACTRUST_ERR_IO if the backing storage cannot be opened
 */
actrust_err_t actrust_storage_open(actrust_storage_t   *out,
                                   actrust_storage_id_t id);

/**
 * @brief Close a storage region and release resources
 *
 * @param[in] st Storage handle returned by actrust_storage_open()
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p st is NULL
 */
actrust_err_t actrust_storage_close(actrust_storage_t st);

/**
 * @brief Return the logical capacity of a storage region
 *
 * @param[in]  st       Storage handle
 * @param[out] capacity Receives the capacity in bytes
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p st or @p capacity is NULL
 * @return @c ACTRUST_ERR_IO if the capacity cannot be determined
 */
actrust_err_t actrust_storage_get_capacity(actrust_storage_t st,
                                           uint32_t         *capacity);

/**
 * @brief Flush completed writes to persistent storage
 *
 * @param[in] st Storage handle
 *
 * @return @c ACTRUST_OK when writes are synchronized
 * @return @c ACTRUST_ERR_INVALID_ARG if @p st is NULL
 * @return @c ACTRUST_ERR_IO if synchronization fails
 */
actrust_err_t actrust_storage_sync(actrust_storage_t st);

/**
 * @brief Read data from a storage region
 *
 * @param[in]  st     Storage handle
 * @param[in]  offset Byte offset within the region
 * @param[out] buf    Destination buffer
 * @param[in]  len    Number of bytes to read
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p st is NULL or @p buf is NULL with
 * len > 0
 * @return @c ACTRUST_ERR_IO if the read operation fails
 * @note Bytes beyond the end of stored data are zero-filled
 */
actrust_err_t actrust_storage_read(actrust_storage_t st, uint32_t offset,
                                   uint8_t *buf, size_t len);

/**
 * @brief Write data to a storage region
 *
 * @param[in]  st     Storage handle
 * @param[in]  offset Byte offset within the region
 * @param[in]  buf    Source buffer
 * @param[in]  len    Number of bytes to write
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p st is NULL or @p buf is NULL with
 * len > 0
 * @return @c ACTRUST_ERR_IO if the write operation fails
 * @note Automatically handles partial writes and signal interrupts (EINTR)
 */
actrust_err_t actrust_storage_write(actrust_storage_t st, uint32_t offset,
                                    const uint8_t *buf, size_t len);

/**
 * @brief Erase (zero-fill) a region of storage
 *
 * @param[in]  st     Storage handle
 * @param[in]  offset Byte offset within the region
 * @param[in]  len    Number of bytes to erase
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p st is NULL
 * @return @c ACTRUST_ERR_IO if the erase operation fails
 */
actrust_err_t actrust_storage_erase(actrust_storage_t st, uint32_t offset,
                                    uint32_t len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_STORAGE_H */
