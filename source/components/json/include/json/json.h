// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file json.h
 * @brief Cross-platform JSON component for AntChainTrustSDK.
 *
 * This module provides a thread-safe (reentrant), zero-copy JSON parser and
 * a lightweight JSON builder.
 */

#ifndef ACTRUST_JSON_H
#define ACTRUST_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_config.h"
#include "actrust_errno.h"

/* ========================================================================
 * Type Definitions
 * ======================================================================== */

/**
 * @brief JSON value types.
 */
typedef enum {
    ACTRUST_JSON_TYPE_NULL,   /**< JSON null */
    ACTRUST_JSON_TYPE_BOOL,   /**< JSON boolean (true/false) */
    ACTRUST_JSON_TYPE_NUMBER, /**< JSON number (integer or float) */
    ACTRUST_JSON_TYPE_STRING, /**< JSON string */
    ACTRUST_JSON_TYPE_ARRAY,  /**< JSON array [...]  */
    ACTRUST_JSON_TYPE_OBJECT  /**< JSON object {...} */
} actrust_json_type_t;

/**
 * @brief Represents a reference to a JSON value within the source buffer.
 *
 * @note This structure does not own the memory it points to. The source
 * buffer must remain valid as long as this value is used.
 */
typedef struct {
    const char
          *value; /**< Pointer to the start of the value in the source buffer */
    size_t value_len;         /**< Length of the value in bytes */
    actrust_json_type_t type; /**< Type of this value */
} actrust_json_value_t;

/**
 * @brief JSON parser view context.
 */
typedef struct {
    const char *buf;     /**< Pointer to the JSON document */
    size_t      buf_len; /**< Total length of the document */
} actrust_json_view_t;

/**
 * @brief JSON builder context.
 * * Maintains state for incremental JSON construction.
 */
typedef struct {
    char  *buf; /**< Output buffer */
    size_t cap; /**< Total capacity of buffer */
    size_t len; /**< Current length of data in buffer */
} actrust_json_builder_t;

/* ========================================================================
 * Parser API
 * ======================================================================== */

/**
 * @brief Initialize a JSON document view and validate its format.
 *
 * Performs a full validation scan of the provided buffer.
 *
 * @param[out] view      JSON document view to initialize.
 * @param[in]  buf       Pointer to the JSON string.
 * @param[in]  len       Length of the buffer.
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG if the JSON format is invalid.
 */
actrust_err_t actrust_json_init(actrust_json_view_t *view, const char *buf,
                                size_t len);

/**
 * @brief Query a JSON value using a dot-notation path.
 *
 * Example paths: "config.wifi", "user.name".
 *
 * @param[in]  view      Parsed JSON document view.
 * @param[in]  path      Null-terminated query path string.
 * @param[out] value     Structure to receive the value reference.
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG if path is not found or arguments are
 * invalid.
 */
actrust_err_t actrust_json_query(const actrust_json_view_t *view,
                                 const char *path, actrust_json_value_t *value);

/**
 * @brief Copy a string value to a destination buffer.
 *
 * Note: JSON escape sequences are decoded before copying to @p out.
 * Use @ref actrust_json_unescape_string directly when decoding a raw string
 * slice.
 *
 * @param[in]  value      JSON value (must be ACTRUST_JSON_TYPE_STRING).
 * @param[out] out        Destination buffer.
 * @param[in]  out_len    Size of the destination buffer.
 * @param[out] actual_len Returns the number of bytes written (excluding
 * null-terminator).
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_BUF_TOO_SMALL if the output buffer is insufficient.
 * @return ACTRUST_ERR_INVALID_ARG if the value is not a string.
 */
actrust_err_t actrust_json_get_string(const actrust_json_value_t *value,
                                      char *out, size_t out_len,
                                      size_t *actual_len);

/**
 * @brief Parse an integer value.
 *
 * @param[in]  value     JSON value (must be ACTRUST_JSON_TYPE_NUMBER).
 * @param[out] out       Pointer to store the result.
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG if parsing fails.
 */
actrust_err_t actrust_json_get_int(const actrust_json_value_t *value,
                                   int64_t                    *out);

/**
 * @brief Parse a boolean value.
 *
 * @param[in]  value     JSON value (must be ACTRUST_JSON_TYPE_BOOL).
 * @param[out] out       Pointer to store the result.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments, a non-boolean
 * value, or a value that is neither "true" nor "false".
 */
actrust_err_t actrust_json_get_bool(const actrust_json_value_t *value,
                                    bool                       *out);

/* ========================================================================
 * String Utility API
 * ======================================================================== */

/**
 * @brief Escape a raw string for JSON string context.
 *
 * This function escapes characters such as quote, backslash and control
 * characters. The output does not include surrounding quotes.
 *
 * @param[in]  input    Null-terminated source string.
 * @param[out] out      Destination buffer.
 * @param[in]  out_cap  Capacity of @p out in bytes.
 * @param[out] out_len  Receives escaped length (excluding null terminator).
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap is insufficient.
 */
actrust_err_t actrust_json_escape_string(const char *input, char *out,
                                         size_t out_cap, size_t *out_len);

/**
 * @brief Unescape JSON string content into plain string.
 *
 * Input must be the string body without surrounding quotes.
 * Supported escape sequences include:
 * - \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\uXXXX
 *
 * @param[in]  input      Source buffer containing escaped content.
 * @param[in]  input_len  Length of @p input in bytes.
 * @param[out] out        Destination buffer.
 * @param[in]  out_cap    Capacity of @p out in bytes.
 * @param[out] out_len    Receives decoded length (excluding null terminator).
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG for invalid arguments or malformed escapes.
 * @return ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap is insufficient.
 */
actrust_err_t actrust_json_unescape_string(const char *input, size_t input_len,
                                           char *out, size_t out_cap,
                                           size_t *out_len);

/* ========================================================================
 * Builder API
 * ======================================================================== */

/**
 * @brief Initialize the JSON builder.
 *
 * Prepares the buffer and writes the opening brace '{'.
 *
 * @param[out] b         Builder context.
 * @param[in]  buf       User-supplied buffer for JSON output.
 * @param[in]  cap       Capacity of the buffer.
 *
 * @return ACTRUST_OK on success.
 */
actrust_err_t actrust_json_builder_init(actrust_json_builder_t *b, char *buf,
                                        size_t cap);

/**
 * @brief Add a key-string pair to the object.
 *
 * Automatically handles comma separation and string escaping.
 *
 * @param[in,out] b      Builder context.
 * @param[in]     key    Key name.
 * @param[in]     val    NUL-terminated string value.
 * @param[in]     val_len String length in bytes, excluding the trailing NUL.
 *                         Must equal @c strlen(val).
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG if @p val_len does not match @c strlen(val).
 * @return ACTRUST_ERR_BUF_TOO_SMALL if buffer is full.
 */
actrust_err_t actrust_json_builder_add_string(actrust_json_builder_t *b,
                                              const char *key, const char *val,
                                              size_t val_len);

/**
 * @brief Add a key-object pair to the object.
 *
 * The object value is inserted as raw JSON (not quoted). Input must be a
 * valid JSON object text (for example: {"k":"v"}).
 *
 * @param[in,out] b         Builder context.
 * @param[in]     key       Key name.
 * @param[in]     obj_json  Null-terminated JSON object text.
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG for invalid arguments or non-object JSON.
 * @return ACTRUST_ERR_BUF_TOO_SMALL if buffer is full.
 */
actrust_err_t actrust_json_builder_add_object(actrust_json_builder_t *b,
                                              const char             *key,
                                              const char             *obj_json);

/**
 * @brief Open a nested object under the given key.
 *
 * Writes `"key":{` (with a leading comma if needed). Subsequent
 * @c add_* calls write fields into the nested scope. Caller MUST balance
 * each open with @ref actrust_json_builder_close_object before calling
 * @ref actrust_json_builder_finish.
 *
 * @param[in,out] b    Builder context.
 * @param[in]     key  Key name for the nested object.
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return ACTRUST_ERR_BUF_TOO_SMALL if buffer is full.
 */
actrust_err_t actrust_json_builder_open_object(actrust_json_builder_t *b,
                                               const char             *key);

/**
 * @brief Close the most recently opened nested object.
 *
 * Writes `}`. Pairs with @ref actrust_json_builder_open_object.
 *
 * @param[in,out] b  Builder context.
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_INVALID_ARG if @p b is NULL.
 * @return ACTRUST_ERR_BUF_TOO_SMALL if buffer is full.
 */
actrust_err_t actrust_json_builder_close_object(actrust_json_builder_t *b);

/**
 * @brief Add a key-integer pair to the object.
 *
 * @param[in,out] b      Builder context.
 * @param[in]     key    Key name.
 * @param[in]     val    Integer value.
 *
 * @return ACTRUST_OK on success.
 */
actrust_err_t actrust_json_builder_add_int(actrust_json_builder_t *b,
                                           const char *key, int64_t val);

/**
 * @brief Add a key-boolean pair to the object.
 *
 * @param[in,out] b      Builder context.
 * @param[in]     key    Key name.
 * @param[in]     val    Boolean value.
 *
 * @return ACTRUST_OK on success.
 */
actrust_err_t actrust_json_builder_add_bool(actrust_json_builder_t *b,
                                            const char *key, bool val);

/**
 * @brief Finalize the JSON string.
 *
 * Appends the closing brace '}' and null-terminator.
 *
 * @param[in,out] b        Builder context.
 * @param[out]    out_json (Optional) Pointer to the start of the valid JSON
 * string.
 * @param[out]    out_len  (Optional) Length of the generated JSON.
 *
 * @return ACTRUST_OK on success.
 * @return ACTRUST_ERR_BUF_TOO_SMALL if there is no space for the closing brace.
 */
actrust_err_t actrust_json_builder_finish(actrust_json_builder_t *b,
                                          char **out_json, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_JSON_H */
