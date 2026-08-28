// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file core_ops.h
 * @brief Core job operation declarations — one per job type.
 */

#ifndef ACTRUST_CORE_OPS_H
#define ACTRUST_CORE_OPS_H

/* C standard */
#include <stdbool.h>

/* Project */
#include "actrust_errno.h"

/* Core */
#include "core/core_job.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Execute ACTRUST_JOB_INIT — initialise core subsystems.
 *
 * @param[in] job  Job to process.
 *
 * @return @c ACTRUST_OK on success.
 */
actrust_err_t core_ops_init(actrust_job_t *job);

/**
 * @brief Execute ACTRUST_JOB_CONNECT — establish a session.
 *
 * @param[in] job  Job to process.
 *
 * @return @c ACTRUST_OK on success.
 */
actrust_err_t core_ops_connect(actrust_job_t *job);

/**
 * @brief Execute ACTRUST_JOB_DISCONNECT — tear down the current session.
 *
 * @param[in] job  Job to process.
 *
 * @return @c ACTRUST_OK on success.
 */
actrust_err_t core_ops_disconnect(actrust_job_t *job);

/**
 * @brief Execute ACTRUST_JOB_REGISTER — perform device registration.
 *
 * @param[in] job  Job to process.
 *
 * @return @c ACTRUST_OK on success.
 */
actrust_err_t core_ops_register(actrust_job_t *job);

/**
 * @brief Read the persisted registration marker.
 *
 * @param[out] out_registered  Receives whether the marker exists.
 *
 * @return @c ACTRUST_OK when the marker is read successfully.
 * @return The first KV error encountered otherwise.
 */
actrust_err_t core_read_local_registered(bool *out_registered);

/**
 * @brief Persist registration and commit the REGISTERED state.
 *
 * The state transition is performed only after the registration marker has
 * been stored and the KV handle has closed successfully.
 *
 * @return @c ACTRUST_OK when persistence and the state transition succeed.
 * @return The first KV error encountered otherwise.
 */
actrust_err_t core_commit_registered(void);

/**
 * @brief Execute ACTRUST_JOB_SEND — publish business data.
 *
 * @param[in] job  Job to process.
 *
 * @return @c ACTRUST_OK on success.
 */
actrust_err_t core_ops_send(actrust_job_t *job);

/**
 * @brief Execute ACTRUST_JOB_DEINIT — shut down core subsystems.
 *
 * @param[in] job  Job to process.
 *
 * @return @c ACTRUST_OK on success.
 */
actrust_err_t core_ops_deinit(actrust_job_t *job);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_CORE_OPS_H */
