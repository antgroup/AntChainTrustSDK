// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file log.c
 * @brief Implementation of cross-platform logging component
 *
 * This module provides thread-safe logging with:
 * - Compile-time level filtering (via Kconfig)
 * - Formatted output with timestamps and metadata
 * - Platform-independent output via adapter layer
 */

/* C standard */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Log */
#include "log/log.h"

/* Adapter */
#include "adapter/system.h"

/* Unified error macro for LOG component */
#ifndef LOG_ERR
#define LOG_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_LOG, (reason))
#endif

#define LOG_DEINIT_WAIT_MS 10u

/* ========================================================================
 * Private Types and Constants
 * ======================================================================== */

typedef enum {
    LOG_STATE_UNINIT = 0,
    LOG_STATE_INITING,
    LOG_STATE_READY,
    LOG_STATE_CLOSING,
} log_state_t;

/** Level name strings */
static const char *const s_level_names[] = {
    "ERROR", /* ACTRUST_LOG_LEVEL_ERROR */
    "WARN",  /* ACTRUST_LOG_LEVEL_WARN */
    "INFO",  /* ACTRUST_LOG_LEVEL_INFO */
    "DEBUG", /* ACTRUST_LOG_LEVEL_DEBUG */
};

/* ========================================================================
 * Private State
 * ======================================================================== */

/** Mutex for serialized log output */
static actrust_mutex_t s_log_mutex;

/** Lifecycle state protected by the permanent lifecycle gate */
static log_state_t s_log_state = LOG_STATE_UNINIT;

/** Number of writers admitted before a close began */
static uint32_t s_log_active_writers;

/** Unlock failure that makes the mutex unsafe to destroy automatically */
static actrust_err_t s_log_teardown_error;

/* ========================================================================
 * Private Helper Functions
 * ======================================================================== */

/**
 * @brief Map the current lifecycle state to a public log error
 * @return Log lifecycle error
 */
static actrust_err_t log_state_error(void)
{
    return LOG_ERR(s_log_state == LOG_STATE_UNINIT ? ACTRUST_ERR_NOT_READY
                                                   : ACTRUST_ERR_BAD_STATE);
}

/**
 * @brief Acquire an active-writer reference and snapshot the output mutex
 * @param[out] mutex Output mutex valid for the admitted writer lifetime
 * @return ACTRUST_OK on success, error code otherwise
 */
static actrust_err_t log_writer_acquire(actrust_mutex_t *mutex)
{
    actrust_err_t err = actrust_lifecycle_lock();
    if (err != ACTRUST_OK) {
        return err;
    }

    if (s_log_state != LOG_STATE_READY || s_log_mutex == NULL) {
        err                  = log_state_error();
        actrust_err_t unlock = actrust_lifecycle_unlock();
        return err != ACTRUST_OK ? err : unlock;
    }
    if (s_log_active_writers == UINT32_MAX) {
        err                  = LOG_ERR(ACTRUST_ERR_NO_RESOURCE);
        actrust_err_t unlock = actrust_lifecycle_unlock();
        return err != ACTRUST_OK ? err : unlock;
    }

    s_log_active_writers++;
    *mutex = s_log_mutex;

    err = actrust_lifecycle_unlock();
    return err;
}

/**
 * @brief Release an active-writer reference
 * @param result Primary write result to preserve
 * @param unlock_error Output mutex unlock error, if any
 * @return Primary result, or lifecycle error if the write succeeded
 */
static actrust_err_t log_writer_release(actrust_err_t result,
                                        actrust_err_t unlock_error)
{
    actrust_err_t err = actrust_lifecycle_lock();
    if (err != ACTRUST_OK) {
        return result != ACTRUST_OK ? result : err;
    }

    if (s_log_active_writers > 0u) {
        s_log_active_writers--;
    }
    if (unlock_error != ACTRUST_OK && s_log_teardown_error == ACTRUST_OK) {
        s_log_teardown_error = unlock_error;
    }

    err = actrust_lifecycle_unlock();
    return result != ACTRUST_OK ? result : err;
}

/**
 * @brief Get level name string with bounds checking
 * @param level Log level
 * @return Level name string or "UNKNOWN"
 */
static const char *get_level_name(actrust_log_level_t level)
{
    const int32_t lvl = (int32_t) level;
    const int32_t max =
        (int32_t) (sizeof(s_level_names) / sizeof(s_level_names[0]));

    if (lvl >= 0 && lvl < max) {
        return s_level_names[lvl];
    }
    return "UNKNOWN";
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

actrust_err_t actrust_log_init(void)
{
    if (s_log_state == LOG_STATE_READY) {
        return ACTRUST_OK;
    }
    if (s_log_state != LOG_STATE_UNINIT) {
        return LOG_ERR(ACTRUST_ERR_BAD_STATE);
    }

    s_log_state          = LOG_STATE_INITING;
    s_log_teardown_error = ACTRUST_OK;
    actrust_err_t err    = actrust_mutex_create(&s_log_mutex);
    if (err == ACTRUST_OK) {
        s_log_state = LOG_STATE_READY;
    } else {
        s_log_mutex = NULL;
        s_log_state = LOG_STATE_UNINIT;
    }

    return err;
}

actrust_err_t actrust_log_deinit(void)
{
    for (;;) {
        if (s_log_state == LOG_STATE_UNINIT) {
            return LOG_ERR(ACTRUST_ERR_NOT_READY);
        }
        if (s_log_state == LOG_STATE_INITING) {
            return LOG_ERR(ACTRUST_ERR_BAD_STATE);
        }

        s_log_state = LOG_STATE_CLOSING;
        if (s_log_active_writers > 0u) {
            actrust_err_t err = actrust_lifecycle_unlock();
            if (err != ACTRUST_OK) {
                return err;
            }
            actrust_sleep_ms(LOG_DEINIT_WAIT_MS);
            err = actrust_lifecycle_lock();
            if (err != ACTRUST_OK) {
                return err;
            }
            continue;
        }

        if (s_log_teardown_error != ACTRUST_OK) {
            return s_log_teardown_error;
        }

        if (s_log_mutex != NULL) {
            actrust_err_t err = actrust_mutex_destroy(s_log_mutex);
            if (err != ACTRUST_OK) {
                return err;
            }
            s_log_mutex = NULL;
        }

        s_log_state = LOG_STATE_UNINIT;
        return ACTRUST_OK;
    }
}

actrust_err_t actrust_log_write(actrust_log_level_t level, const char *file,
                                int line, const char *func, const char *fmt,
                                ...)
{
    actrust_mutex_t mutex        = NULL;
    actrust_err_t   err          = log_writer_acquire(&mutex);
    actrust_err_t   unlock_error = ACTRUST_OK;
    if (err != ACTRUST_OK) {
        return err;
    }

    char   line_buf[ACTRUST_LOG_LINE_MAX];
    size_t len = 0u;

    /* Safe defaults for NULL parameters */
    const char *safe_file =
        (file != NULL && file[0] != '\0') ? file : "<unknown>";
    const char *safe_func =
        (func != NULL && func[0] != '\0') ? func : "<unknown>";

    /* Extract basename from file path for cleaner output */
    const char *basename = safe_file;
    for (const char *p = safe_file; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            basename = p + 1;
        }
    }

    /* Get timestamp */
    const uint64_t ts_ms = actrust_wall_time_ms();

    /* Get level name */
    const char *level_name = get_level_name(level);

    /* Format header: [timestamp][level] file:line function: */
    int32_t n = snprintf(line_buf, sizeof(line_buf),
                         "[%llu][%-5s] %s:%d %s: ", (unsigned long long) ts_ms,
                         level_name, basename, line, safe_func);

    if (n < 0) {
        /* snprintf error - should not happen with valid inputs */
        err = LOG_ERR(ACTRUST_ERR_IO);
        goto release;
    }

    size_t used = (size_t) n;

    /* Check if header already filled the buffer */
    if (used >= sizeof(line_buf)) {
        /* Truncate and add newline */
        if (sizeof(line_buf) >= 2u) {
            line_buf[sizeof(line_buf) - 2u] = '\n';
            line_buf[sizeof(line_buf) - 1u] = '\0';
            len                             = sizeof(line_buf) - 1u;
            goto output;
        }
        err = LOG_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
        goto release;
    }

    /* Append user format message */
    if (fmt != NULL && fmt[0] != '\0') {
        va_list args;
        va_start(args, fmt);
        int32_t m =
            vsnprintf(line_buf + used, sizeof(line_buf) - used, fmt, args);
        va_end(args);

        if (m < 0) {
            /* Format error - append fallback message */
            const char fallback[]   = "<format error>";
            size_t     remaining    = sizeof(line_buf) - used;
            size_t     fallback_len = sizeof(fallback) - 1u;

            if (remaining > fallback_len) {
                memcpy(line_buf + used, fallback, fallback_len);
                line_buf[used + fallback_len] = '\0';
            }
        } else if ((size_t) m >= sizeof(line_buf) - used) {
            /* User message was truncated - buffer is full */
            line_buf[sizeof(line_buf) - 1u] = '\0';
        }
    }

    /* Ensure final length is valid */
    len = strlen(line_buf);
    if (len == 0u) {
        err = LOG_ERR(ACTRUST_ERR_IO);
        goto release;
    }

    /* Ensure newline termination */
    if (line_buf[len - 1u] != '\n') {
        if (len + 1u < sizeof(line_buf)) {
            line_buf[len]      = '\n';
            line_buf[len + 1u] = '\0';
            len++;
        } else {
            /* Buffer full - replace last char with newline */
            line_buf[sizeof(line_buf) - 2u] = '\n';
            line_buf[sizeof(line_buf) - 1u] = '\0';
            len                             = sizeof(line_buf) - 1u;
        }
    }

output:
    /* Thread-safe output */
    err = actrust_mutex_lock(mutex);
    if (err != ACTRUST_OK) {
        goto release;
    }

    /* Output via platform adapter */
    err = actrust_log_out(line_buf, len);

    /* Always unlock, even if output failed */
    unlock_error = actrust_mutex_unlock(mutex);
    if (unlock_error != ACTRUST_OK && err == ACTRUST_OK) {
        err = unlock_error;
    }

release:
    return log_writer_release(err, unlock_error);
}
