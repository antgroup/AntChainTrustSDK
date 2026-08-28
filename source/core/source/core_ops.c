// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file core_ops.c
 * @brief Core job operations — state transitions and business logic.
 */

/* C standard */
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Common */
#include "common/common.h"

/* Core */
#include "core/core_internal.h"
#include "core/core_ops.h"

/* Component */
#include "cloud/cloud.h"
#include "crypto/crypto.h"
#include "json/json.h"
#include "kv/kv.h"
#include "log/log.h"

/* Adapter */
#include "adapter/device.h"

#define CORE_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_CORE, (reason))

/* JSON overhead for the signable payload {"data":"<escaped>","timestamp":<ms>}
 */
#define CORE_SIGNABLE_OVERHEAD 64u

/* Action codes */
#define CORE_ACTION_REGISTER_REQ 0x1000u /*!< Request registration phase 1 */
#define CORE_ACTION_REGISTER_CHALLENGE                                         \
    0x1001u /*!< Cloud challenges the device */
#define CORE_ACTION_REGISTER_RESPONSE                                          \
    0x1002u /*!< Device responds to challenge */
#define CORE_ACTION_REGISTER_RESULT                                            \
    0x1003u                              /*!< Cloud confirms registration      \
                                          */
#define CORE_ACTION_DATA_PUBLISH 0x2000u /*!< Publish business data */

/* Registration constants */
#define CORE_REGISTER_TIMEOUT_MS 60000u /*!< Registration timeout in ms */
#define CORE_PUBKEY_DER_MAX_LEN  128u /*!< Max DER-encoded public key length */
#define CORE_SIG_DER_MAX_LEN     80u  /*!< Max DER-encoded signature length */
#define CORE_DEVICE_INFO_MAX_LEN                                               \
    64u /*!< Max length for device info strings                                \
         */
#define CORE_REGISTER_JSON_MAX_LEN                                             \
    512u /*!< Max length for registration JSON                                 \
          */
#define CORE_SEND_JSON_OVERHEAD                                                \
    256u /*!< JSON overhead size for sending data                              \
          */

/* KV storage keys */
#define CORE_KV_NAMESPACE "core" /*!< KV namespace for core */
#define CORE_KV_KEY_REGISTERED                                                 \
    "registered" /*!< KV key for registration status */

/* Downlink task configuration */
#define CORE_DOWNLINK_STACK_SIZE      4096u /*!< Stack size for downlink task */
#define CORE_DOWNLINK_PRIORITY        5u    /*!< Priority for downlink task */
#define CORE_DOWNLINK_POP_TIMEOUT_MS  1000u /*!< Queue pop timeout in ms */
#define CORE_DOWNLINK_JOIN_TIMEOUT_MS 5000u /*!< Task join timeout in ms */

/* NTP task configuration */
#define CORE_NTP_STACK_SIZE 4096u /*!< Stack size for NTP sync task */
#define CORE_NTP_PRIORITY   5u    /*!< Priority for NTP sync task */
#define CORE_NTP_JOIN_TIMEOUT_MS                                               \
    ((uint32_t) CONFIG_ACTRUST_NTP_TIMEOUT_MS +                                \
     2000u) /*!< Join timeout (sync timeout + slack) */

static actrust_err_t core_register_generate_session_id(actrust_core_ctx_t *ctx,
                                                       char  *session_id,
                                                       size_t session_id_cap)
{
    if (ctx == NULL || session_id == NULL ||
        session_id_cap < CORE_REGISTER_SESSION_ID_LEN + 1u) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    uint8_t       random[CORE_REGISTER_SESSION_RANDOM_LEN];
    actrust_err_t err =
        actrust_crypto_random(ctx->crypto, random, sizeof(random));
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t session_id_len =
        actrust_hex_encode(random, sizeof(random), session_id);
    memset(random, 0, sizeof(random));

    core_lock();
    (void) memcpy(ctx->reg.session_id, session_id, session_id_len + 1u);
    ctx->reg.session_id_len = session_id_len;
    core_unlock();

    return ACTRUST_OK;
}

static bool core_register_session_matches_locked(
    const actrust_core_ctx_t *ctx, const actrust_json_view_t *view)
{
    if (ctx == NULL || view == NULL || ctx->reg.session_id_len == 0u) {
        return false;
    }

    actrust_json_value_t val;
    if (ACTRUST_IS_ERR(actrust_json_query(view, "session_id", &val))) {
        return false;
    }

    char   session_id[CORE_REGISTER_SESSION_ID_LEN + 1u];
    size_t session_id_len = 0u;
    if (ACTRUST_IS_ERR(actrust_json_get_string(
            &val, session_id, sizeof(session_id), &session_id_len))) {
        return false;
    }

    if (session_id_len != ctx->reg.session_id_len) {
        return false;
    }

    if (memcmp(session_id, ctx->reg.session_id, ctx->reg.session_id_len) != 0) {
        return false;
    }

    return true;
}

/* ========================================================================
 * Downlink dispatch
 * ======================================================================== */

static void core_handle_challenge(actrust_core_ctx_t        *ctx,
                                  const actrust_cloud_msg_t *msg)
{
    actrust_json_view_t  view;
    actrust_json_value_t val;
    actrust_sem_t        post_sem = NULL;

    core_lock();
    if (!ctx->reg.in_progress ||
        ctx->reg.phase != ACTRUST_CORE_REG_PHASE_WAIT_CHALLENGE) {
        LOG_WARN(
            "late register challenge dropped: unexpected registration phase");
        goto out;
    }

    if (ACTRUST_IS_ERR(actrust_json_init(&view, (const char *) msg->payload,
                                         msg->payload_len))) {
        ctx->reg.ok = false;
        post_sem    = ctx->reg.challenge_sem;
        goto out;
    }

    if (!core_register_session_matches_locked(ctx, &view)) {
        LOG_WARN("register challenge dropped: session mismatch");
        goto out;
    }

    if (ctx->reg.nonce_len != 0u) {
        LOG_WARN("duplicate register challenge dropped");
        goto out;
    }

    if (ACTRUST_IS_ERR(actrust_json_query(&view, "nonce", &val))) {
        ctx->reg.ok = false;
        post_sem    = ctx->reg.challenge_sem;
        goto out;
    }

    if (ACTRUST_IS_ERR(actrust_json_get_string(&val, (char *) ctx->reg.nonce,
                                               sizeof(ctx->reg.nonce),
                                               &ctx->reg.nonce_len))) {
        ctx->reg.ok = false;
        post_sem    = ctx->reg.challenge_sem;
        goto out;
    }

    ctx->reg.ok = true;
    post_sem    = ctx->reg.challenge_sem;

out:
    if (post_sem != NULL) {
        (void) actrust_sem_post(post_sem);
    }
    core_unlock();
}

static void core_handle_register_result(actrust_core_ctx_t        *ctx,
                                        const actrust_cloud_msg_t *msg)
{
    actrust_json_view_t  view;
    actrust_json_value_t val;
    actrust_sem_t        post_sem = NULL;

    core_lock();
    if (!ctx->reg.in_progress ||
        ctx->reg.phase != ACTRUST_CORE_REG_PHASE_WAIT_RESULT) {
        LOG_WARN("late register result dropped: unexpected registration phase");
        goto out;
    }

    if (ACTRUST_IS_ERR(actrust_json_init(&view, (const char *) msg->payload,
                                         msg->payload_len))) {
        ctx->reg.ok = false;
        post_sem    = ctx->reg.result_sem;
        goto out;
    }

    if (!core_register_session_matches_locked(ctx, &view)) {
        LOG_WARN("register result dropped: session mismatch");
        goto out;
    }

    if (ACTRUST_IS_ERR(actrust_json_query(&view, "status", &val))) {
        ctx->reg.ok = false;
        post_sem    = ctx->reg.result_sem;
        goto out;
    }

    char   status_str[16];
    size_t status_len = 0;

    if (ACTRUST_IS_ERR(actrust_json_get_string(
            &val, status_str, sizeof(status_str), &status_len))) {
        ctx->reg.ok = false;
        post_sem    = ctx->reg.result_sem;
        goto out;
    }

    if (status_len == 2 && memcmp(status_str, "ok", 2) == 0) {
        ctx->reg.ok = true;
    } else {
        ctx->reg.ok = false;
    }

    post_sem = ctx->reg.result_sem;

out:
    if (post_sem != NULL) {
        (void) actrust_sem_post(post_sem);
    }
    core_unlock();
}

static void core_downlink_dispatch(actrust_core_ctx_t        *ctx,
                                   const actrust_cloud_msg_t *msg)
{
    actrust_json_view_t  view;
    actrust_json_value_t val;

    if (ACTRUST_IS_ERR(actrust_json_init(&view, (const char *) msg->payload,
                                         msg->payload_len))) {
        return;
    }

    if (ACTRUST_IS_ERR(actrust_json_query(&view, "action", &val))) {
        return;
    }

    int64_t value = 0;
    if (ACTRUST_IS_ERR(actrust_json_get_int(&val, &value))) {
        return;
    }

    if (value < 0 || value > UINT32_MAX) {
        LOG_WARN("action value out of range: %" PRId64, value);
        return;
    }

    uint32_t action = (uint32_t) value;
    switch (action) {
        case CORE_ACTION_REGISTER_CHALLENGE:
            core_handle_challenge(ctx, msg);
            break;
        case CORE_ACTION_REGISTER_RESULT:
            core_handle_register_result(ctx, msg);
            break;
        default:
            LOG_WARN("received unsupported action: %" PRIu32, action);
            break;
    }
}

/* ========================================================================
 * Downlink task
 * ======================================================================== */

static void core_downlink_task_main(void *arg)
{
    actrust_core_ctx_t  *ctx = (actrust_core_ctx_t *) arg;
    actrust_cloud_msg_t *msg =
        (actrust_cloud_msg_t *) ACTRUST_MALLOC(sizeof(actrust_cloud_msg_t));
    if (msg == NULL) {
        return;
    }

    while (true) {
        actrust_err_t wait_err = actrust_sem_wait(ctx->downlink_stop_sem,
                                                  CORE_DOWNLINK_POP_TIMEOUT_MS);
        if (ACTRUST_IS_OK(wait_err)) {
            /* Stop signal received. */
            break;
        }

        while (ACTRUST_IS_OK(actrust_queue_pop(ctx->downlink_queue, msg, 0u))) {
            core_downlink_dispatch(ctx, msg);
        }
    }

    ACTRUST_FREE(msg);
}

static actrust_err_t core_downlink_start(actrust_core_ctx_t *ctx)
{
    actrust_err_t err = actrust_sem_create(&ctx->downlink_stop_sem, 0);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_task_create(&ctx->downlink_task, "core_downlink",
                              core_downlink_task_main, ctx,
                              CORE_DOWNLINK_STACK_SIZE, CORE_DOWNLINK_PRIORITY);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_sem_destroy(ctx->downlink_stop_sem);
        ctx->downlink_stop_sem = NULL;
        return err;
    }

    return ACTRUST_OK;
}

static actrust_err_t core_downlink_stop(actrust_core_ctx_t *ctx)
{
    if (ctx->downlink_stop_sem != NULL && ctx->downlink_task != NULL) {
        (void) actrust_sem_post(ctx->downlink_stop_sem);
    }
    if (ctx->downlink_task != NULL) {
        actrust_err_t join_err = actrust_task_join(
            ctx->downlink_task, CORE_DOWNLINK_JOIN_TIMEOUT_MS);
        if (ACTRUST_IS_ERR(join_err)) {
            LOG_WARN("downlink task join failed: 0x%08" PRIx32, join_err);
            return join_err;
        }
        ctx->downlink_task = NULL;
    }
    if (ctx->downlink_stop_sem != NULL) {
        actrust_err_t sem_err = actrust_sem_destroy(ctx->downlink_stop_sem);
        if (ACTRUST_IS_ERR(sem_err)) {
            LOG_WARN("downlink stop semaphore destroy failed: 0x%08" PRIx32,
                     sem_err);
            return sem_err;
        }
        ctx->downlink_stop_sem = NULL;
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * NTP periodic sync task
 * ======================================================================== */

static void core_ntp_task_main(void *arg)
{
    actrust_core_ctx_t *ctx = (actrust_core_ctx_t *) arg;
    uint32_t            interval_ms =
        (uint32_t) CONFIG_ACTRUST_NTP_SYNC_PERIOD_SEC * 1000u;

    /* Initial sync at connect time */
    actrust_err_t err = actrust_ntp_sync(ctx->ntp);
    if (ACTRUST_IS_ERR(err)) {
        LOG_WARN("ntp initial sync failed: 0x%08" PRIx32, err);
    }

    /* Periodic sync until stop signal arrives via ntp_stop_sem */
    while (true) {
        actrust_err_t wait_err =
            actrust_sem_wait(ctx->ntp_stop_sem, interval_ms);
        if (ACTRUST_IS_OK(wait_err)) {
            /* Stop signal received */
            break;
        }

        err = actrust_ntp_sync(ctx->ntp);
        if (ACTRUST_IS_ERR(err)) {
            LOG_WARN("ntp periodic sync failed: 0x%08" PRIx32, err);
        }
    }
}

static actrust_err_t core_ntp_start(actrust_core_ctx_t *ctx)
{
    actrust_err_t err = actrust_sem_create(&ctx->ntp_stop_sem, 0);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_task_create(&ctx->ntp_task, "core_ntp", core_ntp_task_main,
                              ctx, CORE_NTP_STACK_SIZE, CORE_NTP_PRIORITY);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_sem_destroy(ctx->ntp_stop_sem);
        ctx->ntp_stop_sem = NULL;
        return err;
    }

    return ACTRUST_OK;
}

static actrust_err_t core_ntp_stop(actrust_core_ctx_t *ctx)
{
    if (ctx->ntp_stop_sem != NULL && ctx->ntp_task != NULL) {
        (void) actrust_sem_post(ctx->ntp_stop_sem);
    }
    if (ctx->ntp_task != NULL) {
        actrust_err_t join_err =
            actrust_task_join(ctx->ntp_task, CORE_NTP_JOIN_TIMEOUT_MS);
        if (ACTRUST_IS_ERR(join_err)) {
            LOG_WARN("ntp task join failed: 0x%08" PRIx32, join_err);
            return join_err;
        }
        ctx->ntp_task = NULL;
    }
    if (ctx->ntp_stop_sem != NULL) {
        actrust_err_t sem_err = actrust_sem_destroy(ctx->ntp_stop_sem);
        if (ACTRUST_IS_ERR(sem_err)) {
            LOG_WARN("ntp stop semaphore destroy failed: 0x%08" PRIx32,
                     sem_err);
            return sem_err;
        }
        ctx->ntp_stop_sem = NULL;
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Init / Deinit
 * ======================================================================== */

static actrust_err_t core_store_claim_cert(const actrust_job_t *job)
{
    actrust_err_t err =
        actrust_crypto_cert_write(ACTRUST_CLOUD_CLAIM_CERT_ID,
                                  (const uint8_t *) job->params.init.claim_cert,
                                  job->params.init.claim_cert_len);
    return err;
}

static actrust_err_t core_store_claim_key(actrust_core_ctx_t  *ctx,
                                          const actrust_job_t *job)
{
    return actrust_crypto_key_import(
        ctx->crypto, ACTRUST_CLOUD_CLAIM_KEY_ID,
        ACTRUST_CRYPTO_FORMAT_PRIVATE_PEM,
        (const uint8_t *) job->params.init.claim_key,
        job->params.init.claim_key_len, NULL);
}

static actrust_err_t core_store_claim_credentials(actrust_core_ctx_t  *ctx,
                                                  const actrust_job_t *job)
{
    if (job == NULL || !job->params.init.has_config) {
        return ACTRUST_OK;
    }

    actrust_err_t err = core_store_claim_cert(job);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = core_store_claim_key(ctx, job);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    return ACTRUST_OK;
}

actrust_err_t core_ops_init(actrust_job_t *job)
{
    actrust_core_ctx_t *ctx = core_get_ctx();
    actrust_err_t       err;

    /* actrust_kv_init is a global one-shot initialiser with no matching
     * deinit; on failure there is nothing to roll back. */
    err = actrust_kv_init();
    if (ACTRUST_IS_ERR(err)) {
        core_set_state(ACTRUST_CORE_INIT_FAILED);
        return err;
    }

    err = actrust_crypto_init(&ctx->crypto);
    if (ACTRUST_IS_ERR(err)) {
        core_set_state(ACTRUST_CORE_INIT_FAILED);
        return err;
    }

    err = actrust_ntp_init(&ctx->ntp);
    if (ACTRUST_IS_ERR(err)) {
        goto fail_crypto;
    }

    err = actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &ctx->cloud,
                             &ctx->downlink_queue);
    if (ACTRUST_IS_ERR(err)) {
        goto fail_ntp;
    }

    err = core_store_claim_credentials(ctx, job);
    if (ACTRUST_IS_ERR(err)) {
        goto fail_cloud;
    }

    /* Try to open an existing signing key (succeeds if already registered).
     * A missing key on first boot is expected, so we only log and continue. */
    actrust_err_t key_err = actrust_crypto_key_open(
        ctx->crypto, ACTRUST_CRYPTO_KEY_ID_EC_0, &ctx->sign_key);
    if (ACTRUST_IS_ERR(key_err)) {
        LOG_DEBUG("no existing signing key (first boot): 0x%08" PRIx32,
                  key_err);
    }

    core_set_state(ACTRUST_CORE_READY);
    return ACTRUST_OK;

fail_cloud:
    (void) actrust_cloud_deinit(ctx->cloud);
    ctx->cloud          = NULL;
    ctx->downlink_queue = NULL;
fail_ntp:
    (void) actrust_ntp_deinit(ctx->ntp);
    ctx->ntp = NULL;
fail_crypto:
    (void) actrust_crypto_deinit(&ctx->crypto);
    ctx->crypto = NULL;
    core_set_state(ACTRUST_CORE_INIT_FAILED);
    return err;
}

actrust_err_t core_ops_deinit(actrust_job_t *job)
{
    (void) job;
    actrust_core_ctx_t *ctx = core_get_ctx();

    core_set_state(ACTRUST_CORE_DEINIT);

    actrust_err_t err = core_ntp_stop(ctx);
    if (ACTRUST_IS_ERR(err)) {
        LOG_WARN("ntp stop during deinit failed: 0x%08" PRIx32, err);
        return err;
    }

    err = core_downlink_stop(ctx);
    if (ACTRUST_IS_ERR(err)) {
        LOG_WARN("downlink stop during deinit failed: 0x%08" PRIx32, err);
        return err;
    }

    if (ctx->sign_key != NULL) {
        err = actrust_crypto_key_close(ctx->crypto, &ctx->sign_key);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
    }

    if (ctx->cloud != NULL) {
        err = actrust_cloud_deinit(ctx->cloud);
        if (ACTRUST_IS_ERR(err)) {
            LOG_WARN("cloud deinit during deinit failed: 0x%08" PRIx32, err);
            return err;
        }
        ctx->cloud          = NULL;
        ctx->downlink_queue = NULL;
    }

    if (ctx->ntp != NULL) {
        err = actrust_ntp_deinit(ctx->ntp);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        ctx->ntp = NULL;
    }

    if (ctx->crypto != NULL) {
        err = actrust_crypto_deinit(&ctx->crypto);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Connect / Disconnect
 * ======================================================================== */

actrust_err_t core_read_local_registered(bool *out_registered)
{
    if (out_registered == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *out_registered = false;

    actrust_kv_t  kv = NULL;
    actrust_err_t err =
        actrust_kv_open(CORE_KV_NAMESPACE, sizeof(CORE_KV_NAMESPACE) - 1u, &kv);
    if (ACTRUST_IS_ERR(err)) {
        LOG_WARN(
            "kv open failed while reading registration status: 0x%08" PRIx32,
            err);
        return err;
    }

    err =
        actrust_kv_exists(kv, CORE_KV_KEY_REGISTERED,
                          sizeof(CORE_KV_KEY_REGISTERED) - 1u, out_registered);
    if (ACTRUST_IS_ERR(err)) {
        LOG_WARN("kv exists failed while reading registration status: "
                 "0x%08" PRIx32,
                 err);
    }

    actrust_err_t close_err = actrust_kv_close(kv);
    if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(close_err)) {
        err = close_err;
    }
    return err;
}

static actrust_err_t core_persist_registered(void)
{
    actrust_kv_t  kv = NULL;
    actrust_err_t err =
        actrust_kv_open(CORE_KV_NAMESPACE, sizeof(CORE_KV_NAMESPACE) - 1u, &kv);
    if (ACTRUST_IS_ERR(err)) {
        LOG_WARN(
            "failed to open kv namespace for registration status: 0x%08" PRIx32,
            err);
        return err;
    }

    err = actrust_kv_set(kv, CORE_KV_KEY_REGISTERED,
                         sizeof(CORE_KV_KEY_REGISTERED) - 1u, "1", 1u);
    if (ACTRUST_IS_ERR(err)) {
        LOG_WARN("failed to persist registration status: 0x%08" PRIx32, err);
    }

    actrust_err_t close_err = actrust_kv_close(kv);
    if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(close_err)) {
        err = close_err;
    }
    return err;
}

actrust_err_t core_commit_registered(void)
{
    actrust_err_t err = core_persist_registered();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    core_set_state(ACTRUST_CORE_REGISTERED);
    return ACTRUST_OK;
}

actrust_err_t core_ops_connect(actrust_job_t *job)
{
    (void) job;
    actrust_core_ctx_t *ctx = core_get_ctx();
    actrust_err_t       err;
    actrust_err_t       cleanup_err;
    bool                local_registered = false;

    err = actrust_cloud_connect(ctx->cloud);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = core_ntp_start(ctx);
    if (ACTRUST_IS_ERR(err)) {
        goto fail;
    }

    err = core_read_local_registered(&local_registered);
    if (ACTRUST_IS_ERR(err)) {
        goto fail;
    }

    err = core_downlink_start(ctx);
    if (ACTRUST_IS_ERR(err)) {
        goto fail;
    }

    if (local_registered && ctx->sign_key != NULL) {
        core_set_state(ACTRUST_CORE_REGISTERED);
    } else {
        if (local_registered && ctx->sign_key == NULL) {
            LOG_WARN("local registration marker exists without signing key");
        }
        core_set_state(ACTRUST_CORE_UNREGISTERED);
    }

    return ACTRUST_OK;

fail:
    cleanup_err = core_downlink_stop(ctx);
    if (ACTRUST_IS_ERR(cleanup_err)) {
        LOG_WARN("downlink stop during connect cleanup failed: 0x%08" PRIx32,
                 cleanup_err);
    }
    cleanup_err = core_ntp_stop(ctx);
    if (ACTRUST_IS_ERR(cleanup_err)) {
        LOG_WARN("ntp stop during connect cleanup failed: 0x%08" PRIx32,
                 cleanup_err);
    }
    cleanup_err = actrust_cloud_disconnect(ctx->cloud);
    if (ACTRUST_IS_ERR(cleanup_err)) {
        LOG_WARN("cloud disconnect during connect cleanup failed: 0x%08" PRIx32,
                 cleanup_err);
    }
    return err;
}

actrust_err_t core_ops_disconnect(actrust_job_t *job)
{
    (void) job;
    actrust_core_ctx_t *ctx = core_get_ctx();

    actrust_err_t err = core_ntp_stop(ctx);
    if (ACTRUST_IS_ERR(err)) {
        LOG_WARN("ntp stop during disconnect failed: 0x%08" PRIx32, err);
    }
    actrust_err_t cleanup_err = core_downlink_stop(ctx);
    if (ACTRUST_IS_ERR(cleanup_err)) {
        LOG_WARN("downlink stop during disconnect failed: 0x%08" PRIx32,
                 cleanup_err);
        if (ACTRUST_IS_OK(err)) {
            err = cleanup_err;
        }
    }

    cleanup_err = actrust_cloud_disconnect(ctx->cloud);
    if (ACTRUST_IS_ERR(cleanup_err)) {
        LOG_WARN("cloud disconnect failed: 0x%08" PRIx32, cleanup_err);
        if (ACTRUST_IS_OK(err)) {
            err = cleanup_err;
        }
    }

    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    core_set_state(ACTRUST_CORE_READY);
    return ACTRUST_OK;
}

/* ========================================================================
 * JSON helpers
 * ======================================================================== */

static actrust_err_t core_build_register_req(
    const char *pubkey_hex, const char *device_id, const char *hw_model,
    const char *fw_version, const char *session_id, char *buf, size_t buf_len,
    char **out, size_t *out_len)
{
    if (pubkey_hex == NULL || device_id == NULL || hw_model == NULL ||
        fw_version == NULL || session_id == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t pubkey_hex_len = strlen(pubkey_hex);
    size_t device_id_len  = strlen(device_id);
    size_t hw_model_len   = strlen(hw_model);
    size_t fw_version_len = strlen(fw_version);
    size_t session_id_len = strlen(session_id);

    actrust_json_builder_t b;

    actrust_err_t err = actrust_json_builder_init(&b, buf, buf_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_int(&b, "action", CORE_ACTION_REGISTER_REQ);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_string(&b, "public_key", pubkey_hex,
                                          pubkey_hex_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_string(&b, "device_id", device_id,
                                          device_id_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err =
        actrust_json_builder_add_string(&b, "hw_model", hw_model, hw_model_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_string(&b, "fw_version", fw_version,
                                          fw_version_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_string(&b, "session_id", session_id,
                                          session_id_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    return actrust_json_builder_finish(&b, out, out_len);
}

static actrust_err_t core_build_challenge_resp(const char *sig_hex,
                                               const char *session_id,
                                               char *buf, size_t buf_len,
                                               char **out, size_t *out_len)
{
    if (sig_hex == NULL || session_id == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t sig_hex_len    = strlen(sig_hex);
    size_t session_id_len = strlen(session_id);

    actrust_json_builder_t b;

    actrust_err_t err = actrust_json_builder_init(&b, buf, buf_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_int(&b, "action",
                                       CORE_ACTION_REGISTER_RESPONSE);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err =
        actrust_json_builder_add_string(&b, "signature", sig_hex, sig_hex_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_string(&b, "session_id", session_id,
                                          session_id_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    return actrust_json_builder_finish(&b, out, out_len);
}

static actrust_err_t core_build_data_publish(
    const char *payload_json, size_t payload_json_len, const char *sig_hex,
    size_t sig_hex_len, char *buf, size_t buf_len, char **out, size_t *out_len)
{
    if (payload_json == NULL || sig_hex == NULL) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_json_builder_t b;

    actrust_err_t err = actrust_json_builder_init(&b, buf, buf_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_open_object(&b, "business");
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_int(&b, "action", CORE_ACTION_DATA_PUBLISH);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_add_string(&b, "payload", payload_json,
                                          payload_json_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err =
        actrust_json_builder_add_string(&b, "signature", sig_hex, sig_hex_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_json_builder_close_object(&b);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    return actrust_json_builder_finish(&b, out, out_len);
}

/* ========================================================================
 * Register — blockchain platform registration via challenge-response
 * ======================================================================== */

/**
 * @brief Fetch device id / hw model / fw version into caller-supplied buffers.
 */
static actrust_err_t core_collect_device_info(
    char *device_id, size_t device_id_cap, char *hw_model, size_t hw_model_cap,
    char *fw_version, size_t fw_version_cap)
{
    actrust_err_t err = actrust_get_hw_id(device_id, device_id_cap);
    if (ACTRUST_IS_ERR(err)) {
        LOG_ERROR("failed to get device id: 0x%08" PRIx32, err);
        return err;
    }

    err = actrust_get_hw_model(hw_model, hw_model_cap);
    if (ACTRUST_IS_ERR(err)) {
        LOG_ERROR("failed to get hw model: 0x%08" PRIx32, err);
        return err;
    }

    err = actrust_get_fw_version(fw_version, fw_version_cap);
    if (ACTRUST_IS_ERR(err)) {
        LOG_ERROR("failed to get fw version: 0x%08" PRIx32, err);
        return err;
    }
    return ACTRUST_OK;
}

/**
 * @brief Generate the device signing key and return its public part as hex.
 *
 * On success @c ctx->sign_key holds the new EC key and @p pubkey_hex
 * contains its DER-then-hex-encoded public component.
 */
static actrust_err_t core_register_make_keypair(actrust_core_ctx_t *ctx,
                                                char               *pubkey_hex,
                                                size_t pubkey_hex_cap)
{
    actrust_err_t err = actrust_crypto_key_generate(
        ctx->crypto, ACTRUST_CRYPTO_KEY_ID_EC_0, &ctx->sign_key);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    uint8_t pubkey_der[CORE_PUBKEY_DER_MAX_LEN];
    size_t  pubkey_der_len = 0;

    err =
        actrust_crypto_key_export_public(ctx->crypto, ctx->sign_key, pubkey_der,
                                         sizeof(pubkey_der), &pubkey_der_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    if (pubkey_der_len * 2u + 1u > pubkey_hex_cap) {
        return CORE_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }
    actrust_hex_encode(pubkey_der, pubkey_der_len, pubkey_hex);
    return ACTRUST_OK;
}

/**
 * @brief Send the register request and wait for the challenge.
 *
 * On success @c ctx->reg.nonce holds the freshly-arrived challenge nonce.
 * @p json_buf is borrowed by the call and may be overwritten before return.
 */
static actrust_err_t core_register_request_challenge(
    actrust_core_ctx_t *ctx, const char *pubkey_hex, const char *device_id,
    const char *hw_model, const char *fw_version, char *json_buf,
    size_t json_buf_cap)
{
    char          session_id[CORE_REGISTER_SESSION_ID_LEN + 1u];
    char         *json_out      = NULL;
    size_t        json_len      = 0u;
    bool          register_ok   = false;
    size_t        nonce_len     = 0u;
    actrust_sem_t challenge_sem = NULL;
    actrust_err_t err =
        core_register_generate_session_id(ctx, session_id, sizeof(session_id));
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = core_build_register_req(pubkey_hex, device_id, hw_model, fw_version,
                                  session_id, json_buf, json_buf_cap, &json_out,
                                  &json_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    core_lock();
    ctx->reg.phase     = ACTRUST_CORE_REG_PHASE_WAIT_CHALLENGE;
    ctx->reg.ok        = false;
    ctx->reg.nonce_len = 0u;
    challenge_sem      = ctx->reg.challenge_sem;
    core_unlock();

    if (challenge_sem == NULL) {
        core_lock();
        ctx->reg.phase = ACTRUST_CORE_REG_PHASE_IDLE;
        core_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    err =
        actrust_cloud_send_register(ctx->cloud, ACTRUST_CLOUD_REGISTER_REQUEST,
                                    (const uint8_t *) json_out, json_len);
    if (ACTRUST_IS_ERR(err)) {
        core_lock();
        ctx->reg.phase = ACTRUST_CORE_REG_PHASE_IDLE;
        core_unlock();
        return err;
    }

    err = actrust_sem_wait(challenge_sem, CORE_REGISTER_TIMEOUT_MS);
    core_lock();
    ctx->reg.phase = ACTRUST_CORE_REG_PHASE_IDLE;
    register_ok    = ctx->reg.ok;
    nonce_len      = ctx->reg.nonce_len;
    core_unlock();

    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    if (nonce_len == 0u) {
        return CORE_ERR(ACTRUST_ERR_IO);
    }
    return register_ok ? ACTRUST_OK : CORE_ERR(ACTRUST_ERR_IO);
}

/**
 * @brief Sign the received nonce, send the response and wait for the result.
 */
static actrust_err_t core_register_answer_challenge(actrust_core_ctx_t *ctx,
                                                    char  *json_buf,
                                                    size_t json_buf_cap)
{
    uint8_t       nonce[CORE_NONCE_MAX_LEN];
    size_t        nonce_len = 0u;
    char          session_id[CORE_REGISTER_SESSION_ID_LEN + 1u];
    size_t        session_id_len = 0u;
    bool          register_ok    = false;
    actrust_sem_t result_sem     = NULL;

    core_lock();
    nonce_len      = ctx->reg.nonce_len;
    session_id_len = ctx->reg.session_id_len;
    if (nonce_len == 0u || session_id_len == 0u ||
        session_id_len > CORE_REGISTER_SESSION_ID_LEN) {
        core_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    (void) memcpy(nonce, ctx->reg.nonce, nonce_len);
    (void) memcpy(session_id, ctx->reg.session_id, session_id_len + 1u);
    core_unlock();

    uint8_t       sig[CORE_SIG_DER_MAX_LEN];
    size_t        sig_len = 0;
    actrust_err_t err     = actrust_crypto_ecdsa_sign(
        ctx->crypto, ctx->sign_key, ACTRUST_CRYPTO_HASH_SHA256,
        ACTRUST_CRYPTO_INPUT_MESSAGE, nonce, nonce_len, sig, sizeof(sig),
        &sig_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    char sig_hex[CORE_SIG_DER_MAX_LEN * 2 + 1];
    actrust_hex_encode(sig, sig_len, sig_hex);

    char  *json_out = NULL;
    size_t json_len = 0;
    err = core_build_challenge_resp(sig_hex, session_id, json_buf, json_buf_cap,
                                    &json_out, &json_len);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    core_lock();
    ctx->reg.phase = ACTRUST_CORE_REG_PHASE_WAIT_RESULT;
    ctx->reg.ok    = false;
    result_sem     = ctx->reg.result_sem;
    core_unlock();

    if (result_sem == NULL) {
        core_lock();
        ctx->reg.phase = ACTRUST_CORE_REG_PHASE_IDLE;
        core_unlock();
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    err =
        actrust_cloud_send_register(ctx->cloud, ACTRUST_CLOUD_REGISTER_RESPONSE,
                                    (const uint8_t *) json_out, json_len);
    if (ACTRUST_IS_ERR(err)) {
        core_lock();
        ctx->reg.phase = ACTRUST_CORE_REG_PHASE_IDLE;
        core_unlock();
        return err;
    }

    err = actrust_sem_wait(result_sem, CORE_REGISTER_TIMEOUT_MS);
    core_lock();
    ctx->reg.phase = ACTRUST_CORE_REG_PHASE_IDLE;
    register_ok    = ctx->reg.ok;
    core_unlock();

    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    return register_ok ? ACTRUST_OK : CORE_ERR(ACTRUST_ERR_IO);
}

static actrust_err_t core_ops_do_register(actrust_core_ctx_t *ctx)
{
    actrust_sem_t challenge_sem = NULL;
    actrust_sem_t result_sem    = NULL;
    actrust_err_t err           = actrust_sem_create(&challenge_sem, 0);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    err = actrust_sem_create(&result_sem, 0);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_sem_destroy(challenge_sem);
        return err;
    }

    core_lock();
    ctx->reg.challenge_sem  = challenge_sem;
    ctx->reg.result_sem     = result_sem;
    ctx->reg.in_progress    = true;
    ctx->reg.ok             = false;
    ctx->reg.nonce_len      = 0u;
    ctx->reg.session_id_len = 0u;
    memset(ctx->reg.session_id, 0, sizeof(ctx->reg.session_id));
    ctx->reg.phase = ACTRUST_CORE_REG_PHASE_IDLE;
    core_unlock();

    char pubkey_hex[CORE_PUBKEY_DER_MAX_LEN * 2 + 1];
    err = core_register_make_keypair(ctx, pubkey_hex, sizeof(pubkey_hex));
    if (ACTRUST_IS_ERR(err)) {
        goto out;
    }

    char device_id[CORE_DEVICE_INFO_MAX_LEN];
    char hw_model[CORE_DEVICE_INFO_MAX_LEN];
    char fw_version[CORE_DEVICE_INFO_MAX_LEN];
    err = core_collect_device_info(device_id, sizeof(device_id), hw_model,
                                   sizeof(hw_model), fw_version,
                                   sizeof(fw_version));
    if (ACTRUST_IS_ERR(err)) {
        goto out;
    }

    char json_buf[CORE_REGISTER_JSON_MAX_LEN];
    err =
        core_register_request_challenge(ctx, pubkey_hex, device_id, hw_model,
                                        fw_version, json_buf, sizeof(json_buf));
    if (ACTRUST_IS_ERR(err)) {
        goto out;
    }

    err = core_register_answer_challenge(ctx, json_buf, sizeof(json_buf));

out:
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_crypto_key_close(ctx->crypto, &ctx->sign_key);
        ctx->sign_key = NULL;
        (void) actrust_crypto_key_destroy(ctx->crypto,
                                          ACTRUST_CRYPTO_KEY_ID_EC_0);
    }
    core_lock();
    ctx->reg.in_progress    = false;
    ctx->reg.phase          = ACTRUST_CORE_REG_PHASE_IDLE;
    ctx->reg.session_id_len = 0u;
    memset(ctx->reg.session_id, 0, sizeof(ctx->reg.session_id));
    challenge_sem          = ctx->reg.challenge_sem;
    result_sem             = ctx->reg.result_sem;
    ctx->reg.challenge_sem = NULL;
    ctx->reg.result_sem    = NULL;
    core_unlock();

    if (challenge_sem != NULL) {
        (void) actrust_sem_destroy(challenge_sem);
    }
    if (result_sem != NULL) {
        (void) actrust_sem_destroy(result_sem);
    }
    return err;
}

actrust_err_t core_ops_register(actrust_job_t *job)
{
    (void) job;

    /* Re-check state at execution time to guard against duplicate jobs that
     * were enqueued before a prior registration completed. */
    actrust_core_state_t state = core_get_state();
    if (state != ACTRUST_CORE_UNREGISTERED) {
        return CORE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_core_ctx_t *ctx = core_get_ctx();

    actrust_err_t err = core_ops_do_register(ctx);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    return core_commit_registered();
}

/* ========================================================================
 * Send — sign payload and publish via cloud
 * ======================================================================== */

actrust_err_t core_ops_send(actrust_job_t *job)
{
    actrust_core_ctx_t *ctx = core_get_ctx();

    if (job->params.send.payload == NULL ||
        job->params.send.payload_len == 0u) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const char *data     = job->params.send.payload;
    size_t      data_len = job->params.send.payload_len;

    /* Obtain NTP timestamp — safe here because we run on the service task
     * where ctx->ntp is only accessed by this thread. */
    uint64_t      timestamp_ms = 0;
    actrust_err_t err          = actrust_ntp_now_ms(ctx->ntp, &timestamp_ms);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    /* Build signable payload: {"data":"<escaped>","timestamp":<ms>} */
    if (data_len > (SIZE_MAX - CORE_SIGNABLE_OVERHEAD) / 6u) {
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    size_t signable_cap = data_len * 6u + CORE_SIGNABLE_OVERHEAD;
    char  *signable_buf = (char *) ACTRUST_MALLOC(signable_cap);
    if (signable_buf == NULL) {
        return CORE_ERR(ACTRUST_ERR_NO_MEM);
    }

    actrust_json_builder_t b;
    char                  *signable_json = NULL;
    size_t                 signable_len  = 0;

    err = actrust_json_builder_init(&b, signable_buf, signable_cap);
    if (ACTRUST_IS_ERR(err)) {
        ACTRUST_FREE(signable_buf);
        return err;
    }

    err = actrust_json_builder_add_string(&b, "data", data, data_len);
    if (ACTRUST_IS_ERR(err)) {
        ACTRUST_FREE(signable_buf);
        return err;
    }

    err = actrust_json_builder_add_int(&b, "timestamp", (int64_t) timestamp_ms);
    if (ACTRUST_IS_ERR(err)) {
        ACTRUST_FREE(signable_buf);
        return err;
    }

    err = actrust_json_builder_finish(&b, &signable_json, &signable_len);
    if (ACTRUST_IS_ERR(err)) {
        ACTRUST_FREE(signable_buf);
        return err;
    }

    /* Sign the signable JSON */
    uint8_t sig_der[CORE_SIG_DER_MAX_LEN];
    size_t  sig_der_len = 0;

    err = actrust_crypto_ecdsa_sign(
        ctx->crypto, ctx->sign_key, ACTRUST_CRYPTO_HASH_SHA256,
        ACTRUST_CRYPTO_INPUT_MESSAGE, (const uint8_t *) signable_json,
        signable_len, sig_der, sizeof(sig_der), &sig_der_len);
    if (ACTRUST_IS_ERR(err)) {
        ACTRUST_FREE(signable_buf);
        return err;
    }

    char   sig_hex[CORE_SIG_DER_MAX_LEN * 2 + 1];
    size_t sig_hex_len = actrust_hex_encode(sig_der, sig_der_len, sig_hex);

    /* The signable JSON is embedded as a JSON string and escaped again. */
    if (sig_hex_len > SIZE_MAX - CORE_SEND_JSON_OVERHEAD) {
        ACTRUST_FREE(signable_buf);
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    size_t publish_overhead = sig_hex_len + CORE_SEND_JSON_OVERHEAD;
    if (signable_len > (SIZE_MAX - publish_overhead) / 2u) {
        ACTRUST_FREE(signable_buf);
        return CORE_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    size_t buf_cap = signable_len * 2u + publish_overhead;
    char  *buf     = (char *) ACTRUST_MALLOC(buf_cap);
    if (buf == NULL) {
        ACTRUST_FREE(signable_buf);
        return CORE_ERR(ACTRUST_ERR_NO_MEM);
    }

    char  *json_out = NULL;
    size_t json_len = 0;

    err = core_build_data_publish(signable_json, signable_len, sig_hex,
                                  sig_hex_len, buf, buf_cap, &json_out,
                                  &json_len);
    if (ACTRUST_IS_ERR(err)) {
        goto out;
    }

    err = actrust_cloud_send_data(ctx->cloud, (const uint8_t *) json_out,
                                  json_len);

out:
    ACTRUST_FREE(buf);
    ACTRUST_FREE(signable_buf);
    return err;
}
