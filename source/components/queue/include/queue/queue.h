// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file queue.h
 * @brief Thread-safe bounded queue component.
 */

#ifndef ACTRUST_QUEUE_H
#define ACTRUST_QUEUE_H

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque queue handle returned by @ref actrust_queue_create. */
typedef struct actrust_queue *actrust_queue_t;

/**
 * @brief Create a queue instance.
 *
 * @param[out] out_queue  Receives a valid queue handle on success.
 * @param[in]  capacity   Number of elements the queue can hold (> 0).
 * @param[in]  item_size  Size of each element in bytes (> 0).
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_NO_MEM when allocation fails.
 */
actrust_err_t actrust_queue_create(actrust_queue_t *out_queue, size_t capacity,
                                   size_t item_size);

/**
 * @brief Destroy a queue and release all resources.
 *
 * @warning Callers must ensure no concurrent push/pop operations are running
 *          when destroying a queue.
 *
 * @param[in,out] queue  Pointer to queue handle; set to NULL on return.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid handle.
 */
actrust_err_t actrust_queue_destroy(actrust_queue_t *queue);

/**
 * @brief Push an element into the queue.
 *
 * @param[in] queue       Queue handle.
 * @param[in] item        Pointer to source element bytes (must not be NULL).
 * @param[in] timeout_ms  Wait timeout in milliseconds.
 *                         - 0: non-blocking, returns immediately if full.
 *                         - @c UINT32_MAX: blocks indefinitely.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_QUEUE_FULL if no empty slot is acquired.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 */
actrust_err_t actrust_queue_push(actrust_queue_t queue, const void *item,
                                 uint32_t timeout_ms);

/**
 * @brief Pop an element from the queue.
 *
 * @param[in]  queue       Queue handle.
 * @param[out] out_item    Buffer to receive one element (must not be NULL).
 * @param[in]  timeout_ms  Wait timeout in milliseconds.
 *                          - 0: non-blocking, returns immediately if empty.
 *                          - @c UINT32_MAX: blocks indefinitely.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_NO_RESOURCE if no item is acquired.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 */
actrust_err_t actrust_queue_pop(actrust_queue_t queue, void *out_item,
                                uint32_t timeout_ms);

/**
 * @brief Get the current number of elements in the queue.
 *
 * @param[in]  queue     Queue handle.
 * @param[out] out_size  Receives the current element count.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 */
actrust_err_t actrust_queue_size(actrust_queue_t queue, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_QUEUE_H */
