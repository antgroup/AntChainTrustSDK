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
 * @brief Close a queue and stop all current and future operations.
 *
 * Closing interrupts push/pop calls blocked in the queue. Operations admitted
 * before close return either their normal result or @c ACTRUST_ERR_BAD_STATE;
 * all new push/pop/size calls return @c ACTRUST_ERR_BAD_STATE. Items still
 * queued when close begins are discarded when the queue is destroyed and cannot
 * be popped. Repeated close calls are allowed.
 *
 * @param[in] queue Queue handle.
 *
 * @return @c ACTRUST_OK when the queue is closed and all admitted operations
 *         have exited.
 * @return @c ACTRUST_ERR_INVALID_ARG for an invalid handle.
 * @return The underlying Adapter synchronization error if locking fails.
 */
actrust_err_t actrust_queue_close(actrust_queue_t queue);

/**
 * @brief Destroy a queue and release all resources.
 *
 * Destroy implicitly closes an open queue. Callers may invoke close while
 * push/pop operations are already running or blocked, but the owner must stop
 * all sources of new calls before final destroy because copied raw handles
 * cannot be invalidated after the object is freed.
 *
 * @param[in,out] queue  Pointer to queue handle; set to NULL only after every
 *                       owned primitive is destroyed successfully.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid handle.
 * @return The underlying Adapter error when close or primitive destruction
 *         fails. The handle remains owned by the caller and destroy may be
 *         retried.
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
 * @return @c ACTRUST_ERR_QUEUE_FULL if a non-blocking wait finds no empty
 *         slot.
 * @return @c ACTRUST_ERR_TIMEOUT if the timed wait expires.
 * @return The underlying adapter synchronization error for other wait or
 *         semaphore failures.
 * @return @c ACTRUST_ERR_BAD_STATE if the queue is closing or closed.
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
 * @return @c ACTRUST_ERR_NO_RESOURCE if a non-blocking wait finds no item.
 * @return @c ACTRUST_ERR_TIMEOUT if the timed wait expires.
 * @return The underlying adapter synchronization error for other wait or
 *         semaphore failures.
 * @return @c ACTRUST_ERR_BAD_STATE if the queue is closing or closed.
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
 * @return @c ACTRUST_ERR_BAD_STATE if the queue is closing or closed.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 */
actrust_err_t actrust_queue_size(actrust_queue_t queue, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_QUEUE_H */
