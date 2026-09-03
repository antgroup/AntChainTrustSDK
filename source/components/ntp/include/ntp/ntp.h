// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ntp.h
 * @brief NTP time synchronisation component
 *
 * This module performs NTP time queries and records the offset between
 * the local wall clock and the NTP server.  The system clock is never
 * modified; instead, callers obtain a corrected UTC timestamp via:
 *
 *     corrected_utc_ms = actrust_wall_time_ms() + offset_ms
 *
 * Synchronisation is driven by the upper layer (e.g. a periodic task);
 * this component does not create any internal threads.
 *
 * @code{.c}
 *   actrust_ntp_t ntp;
 *   actrust_ntp_init(&ntp);
 *
 *   actrust_ntp_sync(ntp);              // called periodically
 *
 *   uint64_t now;
 *   actrust_ntp_now_ms(ntp, &now);      // corrected UTC time
 *
 *   actrust_ntp_deinit(ntp);
 * @endcode
 */

#ifndef ACTRUST_NTP_H
#define ACTRUST_NTP_H

#ifdef __cplusplus
extern "C" {
#endif

/* C standard */
#include <stdint.h>

/* Project */
#include "actrust_errno.h"

/** @brief Opaque NTP instance handle. Obtained via @ref actrust_ntp_init. */
typedef struct actrust_ntp_s *actrust_ntp_t;

/**
 * @brief Create and initialise an NTP instance
 *
 * Allocates an internal context and populates it with compile-time
 * defaults from Kconfig (server, port, timeout).
 *
 * @param[out] out  Receives the NTP handle on success (must not be NULL)
 *
 * @return ACTRUST_OK on success
 * @return Error with @c ACTRUST_ERR_INVALID_ARG if @p out is NULL
 * @return Error with @c ACTRUST_ERR_NO_MEM if allocation fails
 */
actrust_err_t actrust_ntp_init(actrust_ntp_t *out);

/**
 * @brief De-initialise an NTP instance and release resources
 *
 * Deinit waits for calls that entered before closing to finish. Once the owner
 * starts deinit, no thread may begin another API call with this raw handle.
 * After this call succeeds the handle is invalid and must not be used.
 *
 * @param[in] n  NTP handle returned by @ref actrust_ntp_init
 *
 * @return ACTRUST_OK on success
 * @return Error with @c ACTRUST_ERR_INVALID_ARG if @p n is NULL
 */
actrust_err_t actrust_ntp_deinit(actrust_ntp_t n);

/**
 * @brief Perform a single NTP synchronisation
 *
 * Sends an NTP request to the configured server and, on success, updates
 * the internal clock offset state.
 *
 * Synchronisation calls and getters on one handle are serialized. A failed sync
 * leaves the last successfully committed offset unchanged. The configured
 * timeout is one monotonic budget shared by DNS resolution, UDP send, and
 * response receive.
 *
 * This function blocks until the exchange completes or the total budget
 * expires. It is intended to be called from an upper-layer periodic task.
 *
 * @param[in] n  NTP handle
 *
 * @return ACTRUST_OK on success
 * @return Error with @c ACTRUST_ERR_INVALID_ARG if @p n is NULL
 * @return Error with @c ACTRUST_ERR_TIMEOUT if the server does not respond
 * @return Error with @c ACTRUST_ERR_IO on network failure
 * @return Error with @c ACTRUST_ERR_BAD_STATE if the server is unsynchronised
 * or the measured offset exceeds the configured limit
 * @return Error with @c ACTRUST_ERR_HW_FAILURE if timestamp or mutex operations
 *         fail
 */
actrust_err_t actrust_ntp_sync(actrust_ntp_t n);

/**
 * @brief Get the last measured clock offset
 *
 * @param[in]  n              NTP handle
 * @param[out] out_offset_ms  Receives offset in milliseconds.
 *                            corrected_utc = actrust_wall_time_ms() + offset
 *
 * @return ACTRUST_OK on success
 * @return Error with @c ACTRUST_ERR_INVALID_ARG if any argument is NULL
 * @return Error with @c ACTRUST_ERR_NOT_READY if no successful sync yet
 */
actrust_err_t actrust_ntp_get_last_offset_ms(actrust_ntp_t n,
                                             int64_t      *out_offset_ms);

/**
 * @brief Get the NTP-corrected current UTC time
 *
 * Equivalent to @c actrust_wall_time_ms() + last_offset_ms.
 *
 * @param[in]  n           NTP handle
 * @param[out] out_now_ms  Receives corrected UTC time in milliseconds
 *
 * @return ACTRUST_OK on success
 * @return Error with @c ACTRUST_ERR_INVALID_ARG if any argument is NULL
 * @return Error with @c ACTRUST_ERR_NOT_READY if no successful sync yet
 * @return Error with @c ACTRUST_ERR_BAD_STATE if applying the last offset would
 *         underflow or overflow the unsigned timestamp range
 * @return Error with @c ACTRUST_ERR_HW_FAILURE if the wall clock cannot be read
 */
actrust_err_t actrust_ntp_now_ms(actrust_ntp_t n, uint64_t *out_now_ms);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_NTP_H */
