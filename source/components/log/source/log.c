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

/* ========================================================================
 * Private Types and Constants
 * ======================================================================== */

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

/** Mutex for thread-safe logging */
static actrust_mutex_t s_log_mutex = NULL;

/** Initialization flag */
static volatile int s_log_inited = 0;

/* ========================================================================
 * Private Helper Functions
 * ======================================================================== */

/**
 * @brief Acquire log mutex for thread safety
 * @return ACTRUST_OK on success, error code otherwise
 */
static actrust_err_t log_lock(void)
{
    if (!s_log_inited || s_log_mutex == NULL) {
        return LOG_ERR(ACTRUST_ERR_NOT_READY);
    }

    return actrust_mutex_lock(s_log_mutex);
}

/**
 * @brief Release log mutex
 * @return ACTRUST_OK on success, error code otherwise
 */
static actrust_err_t log_unlock(void)
{
    if (s_log_mutex == NULL) {
        return LOG_ERR(ACTRUST_ERR_NOT_READY);
    }

    return actrust_mutex_unlock(s_log_mutex);
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
    /*
     * Safe to call multiple times, but NOT thread-safe against concurrent
     */
    if (s_log_inited) {
        return ACTRUST_OK;
    }

    actrust_err_t err = actrust_mutex_create(&s_log_mutex);
    if (err != ACTRUST_OK) {
        s_log_mutex = NULL;
        return err;
    }

    s_log_inited = 1;

    return ACTRUST_OK;
}

actrust_err_t actrust_log_deinit(void)
{
    if (!s_log_inited) {
        return LOG_ERR(ACTRUST_ERR_NOT_READY);
    }

    actrust_err_t err = ACTRUST_OK;

    if (s_log_mutex != NULL) {
        err         = actrust_mutex_destroy(s_log_mutex);
        s_log_mutex = NULL;
    }

    s_log_inited = 0;
    return err;
}

actrust_err_t actrust_log_write(actrust_log_level_t level, const char *file,
                                int line, const char *func, const char *fmt,
                                ...)
{
    actrust_err_t err = ACTRUST_OK;
    char          line_buf[ACTRUST_LOG_LINE_MAX];
    size_t        len = 0;

    /* Validate initialization */
    if (!s_log_inited) {
        return LOG_ERR(ACTRUST_ERR_NOT_READY);
    }

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
        return LOG_ERR(ACTRUST_ERR_IO);
    }

    size_t used = (size_t) n;

    /* Check if header already filled the buffer */
    if (used >= sizeof(line_buf)) {
        /* Truncate and add newline */
        if (sizeof(line_buf) >= 2) {
            line_buf[sizeof(line_buf) - 2] = '\n';
            line_buf[sizeof(line_buf) - 1] = '\0';
            len                            = sizeof(line_buf) - 1;
            goto output;
        }
        return LOG_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
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
            size_t     fallback_len = sizeof(fallback) - 1;

            if (remaining > fallback_len) {
                memcpy(line_buf + used, fallback, fallback_len);
                line_buf[used + fallback_len] = '\0';
            }
        } else if ((size_t) m >= sizeof(line_buf) - used) {
            /* User message was truncated - buffer is full */
            line_buf[sizeof(line_buf) - 1] = '\0';
        }
    }

    /* Ensure final length is valid */
    len = strlen(line_buf);
    if (len == 0) {
        return LOG_ERR(ACTRUST_ERR_IO);
    }

    /* Ensure newline termination */
    if (line_buf[len - 1] != '\n') {
        if (len + 1 < sizeof(line_buf)) {
            line_buf[len]     = '\n';
            line_buf[len + 1] = '\0';
            len += 1;
        } else {
            /* Buffer full - replace last char with newline */
            line_buf[sizeof(line_buf) - 2] = '\n';
            line_buf[sizeof(line_buf) - 1] = '\0';
            len                            = sizeof(line_buf) - 1;
        }
    }

output:
    /* Thread-safe output */
    err = log_lock();
    if (err != ACTRUST_OK) {
        return err;
    }

    /* Output via platform adapter */
    err = actrust_log_out(line_buf, len);

    /* Always unlock, even if output failed */
    (void) log_unlock();

    return err;
}
