// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file adapter/system.h
 * @brief Cross-platform system abstraction layer interface
 *
 * This module provides unified abstractions for operating system primitives,
 * enabling multi-platform portability.
 */

#ifndef ACTRUST_SYSTEM_H
#define ACTRUST_SYSTEM_H

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup adapter_types Platform Abstraction Types
 * @{
 */

/** @brief Mutex handle (opaque pointer) */
typedef void *actrust_mutex_t;

/** @brief Semaphore handle (opaque pointer) */
typedef void *actrust_sem_t;

/** @brief Thread task handle (opaque pointer) */
typedef void *actrust_task_t;

/** @} */

/**
 * @defgroup adapter_lifecycle Lifecycle Gate
 * @{
 */

/**
 * @brief Acquire the process-wide lifecycle gate.
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_HW_FAILURE if the platform lock operation fails.
 * @note The gate is initialized before first use, allocates no heap memory, and
 *       remains valid for the lifetime of the process. Use it only to serialize
 *       component init/deinit admission, not as a component data lock.
 * @warning The gate is non-recursive; the owning thread must not acquire it
 *          again before releasing it.
 */
actrust_err_t actrust_lifecycle_lock(void);

/**
 * @brief Release the process-wide lifecycle gate.
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_HW_FAILURE if the platform unlock operation fails.
 * @warning Must be called by the thread that acquired the gate.
 */
actrust_err_t actrust_lifecycle_unlock(void);

/** @} */

/**
 * @defgroup adapter_memory Memory Allocation
 * @{
 */

/**
 * @brief Allocate heap memory.
 * @param size Number of bytes to allocate.
 * @return Allocated buffer, or NULL when allocation fails or @p size is 0.
 */
void *actrust_malloc(size_t size);

/**
 * @brief Allocate zero-initialized heap memory.
 * @param nmemb Number of elements to allocate.
 * @param size Size of each element in bytes.
 * @return Allocated buffer, or NULL when allocation fails.
 */
void *actrust_calloc(size_t nmemb, size_t size);

/**
 * @brief Release heap memory allocated by @ref actrust_malloc or
 * @ref actrust_calloc.
 * @param ptr Buffer to release. NULL is accepted.
 */
void actrust_free(void *ptr);

/** @} */

/**
 * @defgroup adapter_time Time Services
 * @{
 */

/**
 * @brief Get monotonic clock time in milliseconds
 * @return Current milliseconds, or 0 on failure
 * @note Unaffected by system time adjustments, suitable for timeout
 * calculations
 */
uint64_t actrust_monotonic_ms(void);

/**
 * @brief Get system wall-clock time in milliseconds
 * @return Current milliseconds (UTC), or 0 on failure
 * @note Affected by system time adjustments, suitable for log timestamps
 */
uint64_t actrust_wall_time_ms(void);

/**
 * @brief Sleep for specified milliseconds
 * @param ms Sleep duration in milliseconds
 * @note Thread-safe, automatically handles signal interrupts (EINTR)
 */
void actrust_sleep_ms(uint32_t ms);

/** @} */

/**
 * @defgroup adapter_mutex Mutexes
 * @{
 */

/**
 * @brief Create a recursive mutex
 * @param[out] out Pointer to mutex handle, receives handle on success
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if out is NULL
 * @return ACTRUST_ERR_NO_MEM if insufficient memory
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 * @note Supports recursive locking (same thread can lock multiple times)
 */
actrust_err_t actrust_mutex_create(actrust_mutex_t *out);

/**
 * @brief Destroy a mutex
 * @param m Mutex handle
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if m is NULL
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 * @warning Destroying a locked mutex results in undefined behavior
 */
actrust_err_t actrust_mutex_destroy(actrust_mutex_t m);

/**
 * @brief Lock a mutex
 * @param m Mutex handle
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if m is NULL
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 * @note Blocking call, waits until lock is acquired
 */
actrust_err_t actrust_mutex_lock(actrust_mutex_t m);

/**
 * @brief Unlock a mutex
 * @param m Mutex handle
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if m is NULL
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 * @warning Unlocking an unlocked mutex or one locked by another thread
 *          results in undefined behavior
 */
actrust_err_t actrust_mutex_unlock(actrust_mutex_t m);

/** @} */

/**
 * @defgroup adapter_semaphore Semaphores
 * @{
 */

/**
 * @brief Create a semaphore
 * @param[out] out Pointer to semaphore handle, receives handle on success
 * @param initial_count Initial count value
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if out is NULL
 * @return ACTRUST_ERR_NO_MEM if insufficient memory
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 */
actrust_err_t actrust_sem_create(actrust_sem_t *out, uint32_t initial_count);

/**
 * @brief Destroy a semaphore
 * @param sem Semaphore handle
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if sem is NULL
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 * @warning Destroying a semaphore with waiting threads results in undefined
 * behavior
 */
actrust_err_t actrust_sem_destroy(actrust_sem_t sem);

/**
 * @brief Wait on a semaphore with timeout support
 * @param sem Semaphore handle
 * @param timeout_ms Timeout in milliseconds
 *        - 0: Non-blocking, returns immediately
 *        - 0xFFFFFFFF: Blocks indefinitely until semaphore is acquired
 *        - Other: Blocks for specified milliseconds
 * @return ACTRUST_OK on successful semaphore acquisition
 * @return ACTRUST_ERR_INVALID_ARG if sem is NULL
 * @return ACTRUST_ERR_WOULD_BLOCK if semaphore unavailable in non-blocking mode
 * @return ACTRUST_ERR_TIMEOUT if timeout expires before acquiring semaphore
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 * @note Automatically handles signal interrupts (EINTR)
 */
actrust_err_t actrust_sem_wait(actrust_sem_t sem, uint32_t timeout_ms);

/**
 * @brief Post to a semaphore (increment count)
 * @param sem Semaphore handle
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if sem is NULL
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 */
actrust_err_t actrust_sem_post(actrust_sem_t sem);

/** @} */

/**
 * @defgroup adapter_task Thread Tasks
 * @{
 */

/**
 * @brief Create a thread task
 * @param[out] out Pointer to task handle, receives handle on success
 * @param name Task name (optional, max 15 bytes), used for debugging
 * @param entry Task entry function void (*)(void*)
 * @param arg Argument passed to entry function
 * @param stack_size Stack size in bytes, 0 uses default
 * @param priority Priority (reserved parameter, currently unimplemented)
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if out or entry is NULL
 * @return ACTRUST_ERR_NO_MEM if insufficient memory
 * @return ACTRUST_ERR_HW_FAILURE if system call fails
 * @warning arg lifetime is managed by user, must remain valid during thread
 * execution
 * @note Created threads are joinable.
 */
actrust_err_t actrust_task_create(actrust_task_t *out, const char *name,
                                  void (*entry)(void *), void     *arg,
                                  uint32_t stack_size, uint32_t priority);

/**
 * @brief Wait for a thread task to exit
 * @param task Task handle returned by @ref actrust_task_create
 * @param timeout_ms Timeout in milliseconds
 *        - 0: Non-blocking poll
 *        - 0xFFFFFFFF: Block indefinitely
 *        - Other: Block up to specified timeout
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if task is NULL
 * @return ACTRUST_ERR_WOULD_BLOCK if task is still running in poll mode
 * @return ACTRUST_ERR_TIMEOUT if timed wait expires
 * @return ACTRUST_ERR_HW_FAILURE on platform failure
 * @note After ACTRUST_OK, caller should not reuse the task handle.
 */
actrust_err_t actrust_task_join(actrust_task_t task, uint32_t timeout_ms);

/** @} */

/**
 * @defgroup adapter_log Log Output
 * @{
 */

/**
 * @brief Platform-specific log output
 * @param buf Log content (UTF-8 encoded)
 * @param len Log length in bytes
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR_INVALID_ARG if buf is NULL or len is 0
 * @return ACTRUST_ERR_IO if output fails
 * @note Thread-safe, automatically handles partial writes and signal interrupts
 */
actrust_err_t actrust_log_out(const char *buf, size_t len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_SYSTEM_H */
