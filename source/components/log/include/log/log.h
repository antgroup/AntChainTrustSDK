// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file log.h
 * @brief Cross-platform logging component for AntChainTrustSDK framework
 *
 * This module provides a thread-safe, configurable logging system with
 * compile-time level filtering and platform-independent output.
 *
 * Features:
 * - Thread-safe operation with mutex protection
 * - Compile-time log level filtering (via Kconfig) to reduce binary size
 * - Configurable message and line buffer sizes
 * - Platform-agnostic output via adapter layer
 * - Millisecond-precision timestamps
 * - File, line, and function tracking
 *
 * @note The actual output mechanism is provided by the platform adapter layer
 *       via actrust_log_out() function.
 * @note Log level is determined at compile-time only via Kconfig settings.
 */

#ifndef ACTRUST_LOG_H
#define ACTRUST_LOG_H

/* Project */
#include "actrust_config.h"
#include "actrust_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log severity levels
 *
 * Higher numerical values indicate more verbose logging.
 * Use compile-time configuration to set the maximum level.
 */
#define ACTRUST_LOG_LEVEL_ERROR_VALUE 0
#define ACTRUST_LOG_LEVEL_WARN_VALUE  1
#define ACTRUST_LOG_LEVEL_INFO_VALUE  2
#define ACTRUST_LOG_LEVEL_DEBUG_VALUE 3

typedef enum {
    /** Error conditions */
    ACTRUST_LOG_LEVEL_ERROR = ACTRUST_LOG_LEVEL_ERROR_VALUE,
    /** Warning conditions */
    ACTRUST_LOG_LEVEL_WARN = ACTRUST_LOG_LEVEL_WARN_VALUE,
    /** Informational messages */
    ACTRUST_LOG_LEVEL_INFO = ACTRUST_LOG_LEVEL_INFO_VALUE,
    /** Debug-level messages */
    ACTRUST_LOG_LEVEL_DEBUG = ACTRUST_LOG_LEVEL_DEBUG_VALUE
} actrust_log_level_t;

/* Legacy compatibility */
#define ACTRUST_LOG_ERROR ACTRUST_LOG_LEVEL_ERROR
#define ACTRUST_LOG_WARN  ACTRUST_LOG_LEVEL_WARN
#define ACTRUST_LOG_INFO  ACTRUST_LOG_LEVEL_INFO
#define ACTRUST_LOG_DEBUG ACTRUST_LOG_LEVEL_DEBUG

/* ========================================================================
 * Compile-time Configuration
 * ======================================================================== */

/**
 * @brief Compile-time maximum log verbosity
 *
 * Messages more verbose than this level are completely removed from the
 * binary, reducing code size and improving performance.
 */
#if defined(CONFIG_ACTRUST_LOG_LEVEL_DEBUG)
#define ACTRUST_LOG_COMPILED_LEVEL ACTRUST_LOG_LEVEL_DEBUG_VALUE
#elif defined(CONFIG_ACTRUST_LOG_LEVEL_INFO)
#define ACTRUST_LOG_COMPILED_LEVEL ACTRUST_LOG_LEVEL_INFO_VALUE
#elif defined(CONFIG_ACTRUST_LOG_LEVEL_WARN)
#define ACTRUST_LOG_COMPILED_LEVEL ACTRUST_LOG_LEVEL_WARN_VALUE
#elif defined(CONFIG_ACTRUST_LOG_LEVEL_ERROR)
#define ACTRUST_LOG_COMPILED_LEVEL ACTRUST_LOG_LEVEL_ERROR_VALUE
#else
/** Default to INFO level if not specified */
#define ACTRUST_LOG_COMPILED_LEVEL ACTRUST_LOG_LEVEL_INFO_VALUE
#endif

/**
 * @brief Maximum length of complete log line (metadata + message)
 */
#ifndef CONFIG_ACTRUST_LOG_LINE_LEN
#define CONFIG_ACTRUST_LOG_LINE_LEN 4096
#endif

#define ACTRUST_LOG_LINE_MAX CONFIG_ACTRUST_LOG_LINE_LEN

/* ========================================================================
 * Public API Functions
 * ======================================================================== */

/**
 * @brief Initialize the logging subsystem
 *
 * @return ACTRUST_OK on success
 * @return Error code from actrust_mutex_create() if mutex creation fails
 *
 * @note Component lifecycle is managed by Core. The caller must hold the
 *       permanent lifecycle gate.
 */
actrust_err_t actrust_log_init(void);

/**
 * @brief Deinitialize the logging subsystem
 *
 * @return ACTRUST_OK on success
 * @return Error code if teardown cannot complete
 *
 * @note Component lifecycle is managed by Core. The caller must hold the
 *       permanent lifecycle gate; the function returns with the gate held.
 */
actrust_err_t actrust_log_deinit(void);

/**
 * @brief Write a formatted log message
 *
 * This is the core logging function. It formats the message with metadata
 * (timestamp, level, file, line, function) and outputs it via the platform
 * adapter layer. The function is thread-safe.
 *
 * @param[in] level    Log severity level
 * @param[in] file     Source filename (typically __FILE__)
 * @param[in] line     Source line number (typically __LINE__)
 * @param[in] func     Function name (typically __func__)
 * @param[in] fmt      Printf-style format string (can be NULL)
 * @param[in] ...      Variable arguments for format string
 *
 * @return ACTRUST_OK on success
 * @return ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_LOG, ACTRUST_ERR_NOT_READY)
 *         if logging not initialized
 * @return ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_LOG, ACTRUST_ERR_IO)
 *         if format error or output fails
 * @return ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_LOG,
 * ACTRUST_ERR_BUF_TOO_SMALL) if buffer is too small even for minimal output
 * @return Error code from actrust_mutex_lock() if lock acquisition fails
 * @return Error code from actrust_log_out() if output fails
 *
 * @note Messages are truncated if they exceed ACTRUST_LOG_LINE_MAX
 * @note This function automatically appends a newline if not present
 * @note NULL or empty format strings are handled gracefully
 * @note A write admitted before deinitialization begins remains valid until it
 *       returns; writes started after closing begins are rejected.
 */
actrust_err_t actrust_log_write(actrust_log_level_t level, const char *file,
                                int line, const char *func, const char *fmt,
                                ...) __attribute__((format(printf, 5, 6)));

/* ========================================================================
 * Convenience Macros
 * ======================================================================== */

/**
 * @brief Helper macro to pass source location metadata
 * @private
 */
#define ACTRUST_LOG_META_ARGS __FILE__, __LINE__, __func__

/**
 * @brief Internal macro for conditional log output
 * @private
 */
#define ACTRUST_LOG_DO(_level, ...)                                            \
    do {                                                                       \
        (void) actrust_log_write((_level), ACTRUST_LOG_META_ARGS,              \
                                 __VA_ARGS__);                                 \
    } while (0)

/* ------------------------------------------------------------------------
 * Level-Specific Logging Macros
 * ------------------------------------------------------------------------ */

/**
 * @brief Log an error message (highest priority)
 *
 * Use for error conditions that prevent normal operation.
 *
 * @param ... Format string and optional variable arguments
 */
#if ACTRUST_LOG_COMPILED_LEVEL >= ACTRUST_LOG_LEVEL_ERROR_VALUE
#define LOG_ERROR(...) ACTRUST_LOG_DO(ACTRUST_LOG_LEVEL_ERROR, __VA_ARGS__)
#else
#define LOG_ERROR(...) ((void) 0)
#endif

/**
 * @brief Log a warning message
 *
 * Use for potentially harmful situations that don't prevent operation.
 *
 * @param ... Format string and optional variable arguments
 */
#if ACTRUST_LOG_COMPILED_LEVEL >= ACTRUST_LOG_LEVEL_WARN_VALUE
#define LOG_WARN(...) ACTRUST_LOG_DO(ACTRUST_LOG_LEVEL_WARN, __VA_ARGS__)
#else
#define LOG_WARN(...) ((void) 0)
#endif

/**
 * @brief Log an informational message
 *
 * Use for important runtime information.
 *
 * @param ... Format string and optional variable arguments
 */
#if ACTRUST_LOG_COMPILED_LEVEL >= ACTRUST_LOG_LEVEL_INFO_VALUE
#define LOG_INFO(...) ACTRUST_LOG_DO(ACTRUST_LOG_LEVEL_INFO, __VA_ARGS__)
#else
#define LOG_INFO(...) ((void) 0)
#endif

/**
 * @brief Log a debug message
 *
 * Use for detailed debugging information.
 *
 * @param ... Format string and optional variable arguments
 */
#if ACTRUST_LOG_COMPILED_LEVEL >= ACTRUST_LOG_LEVEL_DEBUG_VALUE
#define LOG_DEBUG(...) ACTRUST_LOG_DO(ACTRUST_LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void) 0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_LOG_H */
