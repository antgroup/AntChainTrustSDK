// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file hello_actrust.c
 * @brief Minimal end-to-end example: bring up Core, register the device,
 *        publish one business payload, then tear everything down.
 *
 * The AntChainTrustSDK public API is asynchronous: every call submits a job and
 * the result is delivered later through the global callback. This example keeps
 * all SDK calls on the main thread and polls callback results from a simple
 * state machine.
 *
 * Build (from the repo root):
 * @code{.sh}
 *   ./build.sh linux_x86 --clean-build  # produces the static libraries
 *   cmake -S . -B build -DACTRUST_BUILD_EXAMPLES=ON
 *   cmake --build build --target actrust_example_hello
 * @endcode
 *
 * First-boot provisioning needs a claim certificate/key pair. Point the two
 * environment variables below at PEM files before running:
 * @code{.sh}
 *   ACTRUST_CLAIM_CERT=/path/to/client.crt \
 *   ACTRUST_CLAIM_KEY=/path/to/client.key  \
 *   ./build/examples/actrust_example_hello
 * @endcode
 * If the device already holds stored credentials, both may be omitted and
 * actrust_init() is called with NULL.
 */

/* C standard */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* C standard — POSIX */
#include <pthread.h>

/* Project */
#include "actrust.h"
#include "actrust_errno.h"

#define EXAMPLE_PEM_BUF_MAX 4096u
#define EXAMPLE_POLL_SEC    1u

/** @brief Local states used to drive the asynchronous Core lifecycle. */
typedef enum {
    EXAMPLE_STATE_INIT = 0,
    EXAMPLE_STATE_INIT_PENDING,
    EXAMPLE_STATE_CONNECT,
    EXAMPLE_STATE_CONNECT_PENDING,
    EXAMPLE_STATE_REGISTER,
    EXAMPLE_STATE_REGISTER_PENDING,
    EXAMPLE_STATE_PUBLISH,
    EXAMPLE_STATE_PUBLISH_PENDING,
    EXAMPLE_STATE_DISCONNECT,
    EXAMPLE_STATE_DISCONNECT_PENDING,
    EXAMPLE_STATE_DEINIT,
    EXAMPLE_STATE_DEINIT_PENDING,
    EXAMPLE_STATE_DONE,
} example_state_t;

/** @brief Shared state between main() and the completion callback. */
typedef struct {
    actrust_err_t last_result; /**< Most recent callback result. */
    actrust_core_state_t
        last_state; /**< Core state delivered to the most recent callback. */
    example_state_t state;            /**< Current state machine state. */
    pthread_mutex_t callback_lock;    /**< Protects callback result handoff. */
    bool            callback_pending; /**< Set by callback, consumed by main. */
    bool            deinit_required;  /**< Core resources need deinit. */
    bool            connected;        /**< Runtime session is connected. */
    bool            exit_success;     /**< Process should exit successfully. */
} example_ctx_t;

static example_ctx_t g_example;

/* ------------------------------------------------------------------------ */

static void example_log_err(const char *what, actrust_err_t err)
{
    fprintf(stderr, "[%s] failed: module=0x%04x reason=0x%04x\n", what,
            ACTRUST_ERR_MODULE(err), ACTRUST_ERR_CODE(err));
}

static bool example_log_result(const char *what, actrust_err_t result)
{
    bool failed = ACTRUST_IS_ERR(result);
    if (failed) {
        example_log_err(what, result);
        return false;
    }

    printf("[%s] ok\n", what);
    return true;
}

static void example_finish(example_ctx_t *ctx, bool success)
{
    ctx->exit_success = success ? true : false;
    ctx->state        = EXAMPLE_STATE_DONE;
}

/**
 * @brief Read an entire file into @p buf as a NUL-terminated string.
 *
 * @return @c ACTRUST_OK on success; @c ACTRUST_ERR_IO if the file cannot be
 * read;
 *         @c ACTRUST_ERR_BUF_TOO_SMALL if it does not fit in @p buf.
 */
static actrust_err_t example_read_pem(const char *path, char *buf,
                                      size_t buf_cap, size_t *out_len)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return ACTRUST_ERR_MAKE(0, ACTRUST_ERR_IO);
    }

    size_t n   = fread(buf, 1u, buf_cap - 1u, file);
    int    eof = feof(file);
    (void) fclose(file);

    if (n == 0u) {
        return ACTRUST_ERR_MAKE(0, ACTRUST_ERR_IO);
    }

    if (!eof) {
        return ACTRUST_ERR_MAKE(0, ACTRUST_ERR_BUF_TOO_SMALL);
    }

    buf[n]   = '\0';
    *out_len = n;
    return ACTRUST_OK;
}

static void example_callback(actrust_err_t result, actrust_core_state_t state,
                             void *user_data)
{
    example_ctx_t *ctx = (example_ctx_t *) user_data;

    (void) pthread_mutex_lock(&ctx->callback_lock);
    ctx->last_result      = result;
    ctx->last_state       = state;
    ctx->callback_pending = true;
    (void) pthread_mutex_unlock(&ctx->callback_lock);
}

static bool example_has_callback(example_ctx_t *ctx)
{
    bool ready = false;

    (void) pthread_mutex_lock(&ctx->callback_lock);
    ready = ctx->callback_pending;
    (void) pthread_mutex_unlock(&ctx->callback_lock);

    return ready;
}

static bool example_take_callback(example_ctx_t *ctx, actrust_err_t *out_result,
                                  actrust_core_state_t *out_state)
{
    (void) pthread_mutex_lock(&ctx->callback_lock);

    bool ready = ctx->callback_pending;
    if (!ready) {
        (void) pthread_mutex_unlock(&ctx->callback_lock);
        return false;
    }

    ctx->callback_pending = false;
    *out_result           = ctx->last_result;
    *out_state            = ctx->last_state;

    (void) pthread_mutex_unlock(&ctx->callback_lock);
    return true;
}

static bool example_state_waits_callback(example_state_t state)
{
    bool waits = state == EXAMPLE_STATE_INIT_PENDING ||
                 state == EXAMPLE_STATE_CONNECT_PENDING ||
                 state == EXAMPLE_STATE_REGISTER_PENDING ||
                 state == EXAMPLE_STATE_PUBLISH_PENDING ||
                 state == EXAMPLE_STATE_DISCONNECT_PENDING ||
                 state == EXAMPLE_STATE_DEINIT_PENDING;

    return waits;
}

static int example_run(example_ctx_t *ctx, const actrust_config_t *config)
{
    const char *payload = "{\"temperature\":25.3,\"location\":\"Shanghai\","
                          "\"latitude\":31.233333,"
                          "\"longitude\":121.483333}";

    ctx->last_result      = ACTRUST_OK;
    ctx->last_state       = ACTRUST_CORE_UNINIT;
    ctx->state            = EXAMPLE_STATE_INIT;
    ctx->callback_pending = false;
    ctx->deinit_required  = false;
    ctx->connected        = false;
    ctx->exit_success     = false;

    while (ctx->state != EXAMPLE_STATE_DONE) {
        bool waits_callback =
            example_state_waits_callback((example_state_t) ctx->state);
        bool has_callback = example_has_callback(ctx);
        if (waits_callback && !has_callback) {
            (void) sleep(EXAMPLE_POLL_SEC);
            continue;
        }

        actrust_err_t        result        = ACTRUST_OK;
        actrust_core_state_t core_state    = ACTRUST_CORE_UNINIT;
        actrust_err_t        err           = ACTRUST_OK;
        bool                 failed        = false;
        bool                 submit_failed = false;
        bool                 succeeded     = false;
        bool                 ready         = false;

        switch ((example_state_t) ctx->state) {
            case EXAMPLE_STATE_INIT:
                ctx->state    = EXAMPLE_STATE_INIT_PENDING;
                err           = actrust_init(config);
                submit_failed = ACTRUST_IS_ERR(err);
                if (submit_failed) {
                    example_log_err("init submit", err);
                    example_finish(ctx, false);
                } else {
                    ctx->deinit_required = true;
                }
                break;

            case EXAMPLE_STATE_INIT_PENDING:
                ready = example_take_callback(ctx, &result, &core_state);
                if (!ready) {
                    break;
                }
                (void) example_log_result("init", result);
                failed = ACTRUST_IS_ERR(result);
                if (failed) {
                    ctx->state = EXAMPLE_STATE_DEINIT;
                } else {
                    ctx->state = EXAMPLE_STATE_CONNECT;
                }
                break;

            case EXAMPLE_STATE_CONNECT:
                ctx->state    = EXAMPLE_STATE_CONNECT_PENDING;
                err           = actrust_connect();
                submit_failed = ACTRUST_IS_ERR(err);
                if (submit_failed) {
                    example_log_err("connect submit", err);
                    ctx->state = EXAMPLE_STATE_DEINIT;
                }
                break;

            case EXAMPLE_STATE_CONNECT_PENDING:
                ready = example_take_callback(ctx, &result, &core_state);
                if (!ready) {
                    break;
                }

                (void) example_log_result("connect", result);
                failed = ACTRUST_IS_ERR(result);
                if (failed) {
                    ctx->state = EXAMPLE_STATE_DEINIT;
                } else if (core_state == ACTRUST_CORE_UNREGISTERED) {
                    ctx->connected = true;
                    ctx->state     = EXAMPLE_STATE_REGISTER;
                } else if (core_state == ACTRUST_CORE_REGISTERED) {
                    ctx->connected = true;
                    printf("[register] skipped; stored credentials are already "
                           "active\n");
                    ctx->state = EXAMPLE_STATE_PUBLISH;
                } else {
                    ctx->connected = true;
                    fprintf(stderr, "unexpected state after connect: %d\n",
                            (int) core_state);
                    ctx->state = EXAMPLE_STATE_DISCONNECT;
                }
                break;

            case EXAMPLE_STATE_REGISTER:
                ctx->state    = EXAMPLE_STATE_REGISTER_PENDING;
                err           = actrust_register();
                submit_failed = ACTRUST_IS_ERR(err);
                if (submit_failed) {
                    example_log_err("register submit", err);
                    ctx->state = EXAMPLE_STATE_DISCONNECT;
                }
                break;

            case EXAMPLE_STATE_REGISTER_PENDING:
                ready = example_take_callback(ctx, &result, &core_state);
                if (!ready) {
                    break;
                }

                (void) example_log_result("register", result);
                failed = ACTRUST_IS_ERR(result);
                ctx->state =
                    failed ? EXAMPLE_STATE_DISCONNECT : EXAMPLE_STATE_PUBLISH;
                break;

            case EXAMPLE_STATE_PUBLISH:
                ctx->state    = EXAMPLE_STATE_PUBLISH_PENDING;
                err           = actrust_data_publish(payload, strlen(payload));
                submit_failed = ACTRUST_IS_ERR(err);
                if (submit_failed) {
                    example_log_err("publish submit", err);
                    ctx->state = EXAMPLE_STATE_DISCONNECT;
                }
                break;

            case EXAMPLE_STATE_PUBLISH_PENDING:
                ready = example_take_callback(ctx, &result, &core_state);
                if (!ready) {
                    break;
                }

                (void) example_log_result("publish", result);
                succeeded = ACTRUST_IS_OK(result);
                if (succeeded) {
                    ctx->exit_success = true;
                }
                ctx->state = EXAMPLE_STATE_DISCONNECT;
                break;

            case EXAMPLE_STATE_DISCONNECT:
                if (!ctx->connected) {
                    ctx->state = EXAMPLE_STATE_DEINIT;
                    break;
                }

                ctx->state    = EXAMPLE_STATE_DISCONNECT_PENDING;
                err           = actrust_disconnect();
                submit_failed = ACTRUST_IS_ERR(err);
                if (submit_failed) {
                    example_log_err("disconnect submit", err);
                    ctx->connected = false;
                    ctx->state     = EXAMPLE_STATE_DEINIT;
                }
                break;

            case EXAMPLE_STATE_DISCONNECT_PENDING:
                ready = example_take_callback(ctx, &result, &core_state);
                if (!ready) {
                    break;
                }

                (void) example_log_result("disconnect", result);
                ctx->connected = false;
                ctx->state     = EXAMPLE_STATE_DEINIT;
                break;

            case EXAMPLE_STATE_DEINIT:
                if (!ctx->deinit_required) {
                    example_finish(ctx, ctx->exit_success);
                    break;
                }

                ctx->state    = EXAMPLE_STATE_DEINIT_PENDING;
                err           = actrust_deinit();
                submit_failed = ACTRUST_IS_ERR(err);
                if (submit_failed) {
                    example_log_err("deinit submit", err);
                    example_finish(ctx, false);
                }
                break;

            case EXAMPLE_STATE_DEINIT_PENDING:
                ready = example_take_callback(ctx, &result, &core_state);
                if (!ready) {
                    break;
                }

                (void) example_log_result("deinit", result);
                ctx->deinit_required = false;
                succeeded            = ACTRUST_IS_OK(result);
                example_finish(ctx, succeeded && ctx->exit_success);
                break;

            case EXAMPLE_STATE_DONE:
            default:
                example_finish(ctx, false);
                break;
        }
    }

    return ctx->exit_success ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    int exit_code = EXIT_FAILURE;

    if (pthread_mutex_init(&g_example.callback_lock, NULL) != 0) {
        fprintf(stderr, "callback mutex init failed\n");
        return EXIT_FAILURE;
    }

    actrust_err_t err    = actrust_set_callback(example_callback, &g_example);
    bool          failed = ACTRUST_IS_ERR(err);
    if (failed) {
        fprintf(stderr, "actrust_set_callback failed\n");
        (void) pthread_mutex_destroy(&g_example.callback_lock);
        return EXIT_FAILURE;
    }

    /* Optional first-boot provisioning material. */
    char              claim_cert[EXAMPLE_PEM_BUF_MAX];
    char              claim_key[EXAMPLE_PEM_BUF_MAX];
    actrust_config_t  config     = { 0 };
    actrust_config_t *config_ptr = NULL;

    const char *cert_path = getenv("ACTRUST_CLAIM_CERT");
    const char *key_path  = getenv("ACTRUST_CLAIM_KEY");
    if ((cert_path == NULL) != (key_path == NULL)) {
        fprintf(stderr, "ACTRUST_CLAIM_CERT and ACTRUST_CLAIM_KEY must be "
                        "supplied together\n");
        (void) pthread_mutex_destroy(&g_example.callback_lock);
        return EXIT_FAILURE;
    }

    if (cert_path != NULL) {
        size_t cert_len = 0u;
        size_t key_len  = 0u;

        err    = example_read_pem(cert_path, claim_cert, sizeof(claim_cert),
                                  &cert_len);
        failed = ACTRUST_IS_ERR(err);
        if (failed) {
            (void) pthread_mutex_destroy(&g_example.callback_lock);
            return EXIT_FAILURE;
        }

        err =
            example_read_pem(key_path, claim_key, sizeof(claim_key), &key_len);
        failed = ACTRUST_IS_ERR(err);
        if (failed) {
            (void) pthread_mutex_destroy(&g_example.callback_lock);
            return EXIT_FAILURE;
        }

        config.claim_cert     = claim_cert;
        config.claim_cert_len = cert_len;
        config.claim_key      = claim_key;
        config.claim_key_len  = key_len;
        config_ptr            = &config;
        printf("using claim credentials from environment\n");
    } else {
        printf(
            "no claim credentials supplied; relying on stored credentials\n");
    }

    exit_code = example_run(&g_example, config_ptr);
    (void) pthread_mutex_destroy(&g_example.callback_lock);
    return exit_code;
}
