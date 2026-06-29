// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file json.c
 * @brief JSON component implementation based on coreJSON.
 *
 * This module provides a wrapper around coreJSON for parsing and a lightweight
 * builder for constructing JSON strings.
 */

/* C standard */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Third-party */
#include "core_json.h"

/* Common */
#include "common/common.h"

/* JSON */
#include "json/json.h"

/* Unified error macro for JSON component */
#ifndef JSON_ERR
#define JSON_ERR(reason)                                                       \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_JSON, (reason))
#endif

/* ========================================================================
 * Macros & Constants
 * ======================================================================== */

#define JSON_BOOL_TRUE_STR  "true"
#define JSON_BOOL_TRUE_LEN  4
#define JSON_BOOL_FALSE_STR "false"
#define JSON_BOOL_FALSE_LEN 5

/* Internal buffer constraint check */
#define REMAINING_CAP(b) ((b)->cap - (b)->len)

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

/**
 * @brief Maps coreJSON types to internal ACTRUST types.
 */
static actrust_json_type_t map_corejson_type(JSONTypes_t t)
{
    switch (t) {
        case JSONString:
            return ACTRUST_JSON_TYPE_STRING;
        case JSONNumber:
            return ACTRUST_JSON_TYPE_NUMBER;
        case JSONTrue:
            return ACTRUST_JSON_TYPE_BOOL;
        case JSONFalse:
            return ACTRUST_JSON_TYPE_BOOL;
        case JSONNull:
            return ACTRUST_JSON_TYPE_NULL;
        case JSONArray:
            return ACTRUST_JSON_TYPE_ARRAY;
        case JSONObject:
            return ACTRUST_JSON_TYPE_OBJECT;
        default:
            return ACTRUST_JSON_TYPE_NULL;
    }
}

/**
 * @brief Low-level append function with strict bounds checking.
 *
 * Appends data to the builder buffer and ensures null-termination.
 */
static actrust_err_t json_builder_write(actrust_json_builder_t *b,
                                        const char *data, size_t len)
{
    if (b == NULL || b->buf == NULL || b->cap == 0) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (len == 0) {
        return ACTRUST_OK;
    }

    if (len + 1 > REMAINING_CAP(b)) {
        return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    memcpy(b->buf + b->len, data, len);
    b->len += len;
    b->buf[b->len] = '\0';

    return ACTRUST_OK;
}

static inline actrust_err_t json_builder_write_char(actrust_json_builder_t *b,
                                                    char                    c)
{
    return json_builder_write(b, &c, 1);
}

/**
 * @brief Appends a string processing JSON escape sequences.
 *
 * Handles quotes, backslashes, control characters, etc.
 */
static actrust_err_t json_builder_append_escaped_len(actrust_json_builder_t *b,
                                                     const char *s, size_t len)
{
    if (s == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const unsigned char *p   = (const unsigned char *) s;
    actrust_err_t        err = ACTRUST_OK;

    for (size_t idx = 0u; idx < len && err == ACTRUST_OK; idx++) {
        const char *esc_seq = NULL;
        size_t      esc_len = 0;
        char        hex_buf[7];

        switch (*p) {
            case '"':
                esc_seq = "\\\"";
                esc_len = 2;
                break;
            case '\\':
                esc_seq = "\\\\";
                esc_len = 2;
                break;
            case '\b':
                esc_seq = "\\b";
                esc_len = 2;
                break;
            case '\f':
                esc_seq = "\\f";
                esc_len = 2;
                break;
            case '\n':
                esc_seq = "\\n";
                esc_len = 2;
                break;
            case '\r':
                esc_seq = "\\r";
                esc_len = 2;
                break;
            case '\t':
                esc_seq = "\\t";
                esc_len = 2;
                break;
            default:
                if (*p < 0x20) {
                    /* Control characters must be escaped as unicode */
                    int32_t n = snprintf(hex_buf, sizeof(hex_buf), "\\u%04x",
                                         (unsigned int) *p);
                    if (n < 0 || (size_t) n >= sizeof(hex_buf)) {
                        return JSON_ERR(ACTRUST_ERR_HW_FAILURE);
                    }
                    esc_seq = hex_buf;
                    esc_len = (size_t) n;
                }
                break;
        }

        if (esc_seq) {
            err = json_builder_write(b, esc_seq, esc_len);
        } else {
            err = json_builder_write_char(b, (char) *p);
        }

        p++;
    }

    return err;
}

static actrust_err_t json_builder_append_escaped(actrust_json_builder_t *b,
                                                 const char             *s)
{
    if (s == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return json_builder_append_escaped_len(b, s, strlen(s));
}

/**
 * @brief Internal helper to prepare a key-value pair.
 *
 * Adds a comma if necessary, adds the key (quoted and escaped), and the colon.
 */
static actrust_err_t json_builder_append_key(actrust_json_builder_t *b,
                                             const char             *key)
{
    actrust_err_t err;

    /* Add comma if the previous char was not an opening brace or bracket */
    if (b->len > 0) {
        char last_char = b->buf[b->len - 1];
        if (last_char != '{' && last_char != '[') {
            err = json_builder_write_char(b, ',');
            if (err != ACTRUST_OK) {
                return err;
            }
        }
    }

    /* Key: "key": */
    if ((err = json_builder_write_char(b, '"')) != ACTRUST_OK) {
        return err;
    }
    if ((err = json_builder_append_escaped(b, key)) != ACTRUST_OK) {
        return err;
    }
    if ((err = json_builder_write(b, "\":", 2)) != ACTRUST_OK) {
        return err;
    }

    return ACTRUST_OK;
}

static bool json_is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool json_is_object_text(const char *json_text, size_t json_len)
{
    if (json_text == NULL || json_len == 0u) {
        return false;
    }

    size_t start = 0u;
    while (start < json_len && json_is_space_char(json_text[start])) {
        start++;
    }
    if (start >= json_len) {
        return false;
    }

    size_t end = json_len;
    while (end > start && json_is_space_char(json_text[end - 1u])) {
        end--;
    }

    return json_text[start] == '{' && json_text[end - 1u] == '}';
}

/* ========================================================================
 * Parser API
 * ======================================================================== */

actrust_err_t actrust_json_init(actrust_json_view_t *view, const char *buf,
                                size_t len)
{
    if (view == NULL || buf == NULL || len == 0) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (JSON_Validate(buf, len) != JSONSuccess) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    view->buf     = buf;
    view->buf_len = len;
    return ACTRUST_OK;
}

actrust_err_t actrust_json_query(const actrust_json_view_t *view,
                                 const char *path, actrust_json_value_t *value)
{
    if (view == NULL || view->buf == NULL || path == NULL || value == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const char *out_value = NULL;
    size_t      out_len   = 0;
    JSONTypes_t out_type  = JSONInvalid;

    JSONStatus_t st =
        JSON_SearchConst(view->buf, view->buf_len, path, strlen(path),
                         &out_value, &out_len, &out_type);

    if (st != JSONSuccess) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    value->value     = out_value;
    value->value_len = out_len;
    value->type      = map_corejson_type(out_type);

    return ACTRUST_OK;
}

actrust_err_t actrust_json_get_string(const actrust_json_value_t *value,
                                      char *out, size_t out_len,
                                      size_t *actual_len)
{
    if (value == NULL || out == NULL || actual_len == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (value->type != ACTRUST_JSON_TYPE_STRING || value->value == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (value->value_len > SIZE_MAX - 1u) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char *decoded = (char *) ACTRUST_MALLOC(value->value_len + 1u);
    if (decoded == NULL) {
        return JSON_ERR(ACTRUST_ERR_NO_MEM);
    }

    size_t        decoded_len = 0u;
    actrust_err_t err =
        actrust_json_unescape_string(value->value, value->value_len, decoded,
                                     value->value_len + 1u, &decoded_len);
    if (err != ACTRUST_OK) {
        ACTRUST_FREE(decoded);
        return err;
    }

    if (decoded_len >= out_len) {
        *actual_len = decoded_len;
        ACTRUST_FREE(decoded);
        return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    memcpy(out, decoded, decoded_len + 1u);
    *actual_len = decoded_len;
    ACTRUST_FREE(decoded);

    return ACTRUST_OK;
}

actrust_err_t actrust_json_get_int(const actrust_json_value_t *value,
                                   int64_t                    *out)
{
    if (value == NULL || out == NULL || value->value == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (value->type != ACTRUST_JSON_TYPE_NUMBER) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* Create a temporary null-terminated string for strtoll */
    char tmp[32];
    if (value->value_len >= sizeof(tmp)) {
        return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    memcpy(tmp, value->value, value->value_len);
    tmp[value->value_len] = '\0';

    errno       = 0;
    char   *end = NULL;
    int64_t v   = strtoll(tmp, &end, 10);

    if (end == tmp || *end != '\0' || errno == ERANGE) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *out = (int64_t) v;
    return ACTRUST_OK;
}

actrust_err_t actrust_json_get_bool(const actrust_json_value_t *value,
                                    bool                       *out)
{
    if (value == NULL || out == NULL || value->value == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (value->type != ACTRUST_JSON_TYPE_BOOL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (value->value_len == JSON_BOOL_TRUE_LEN &&
        strncmp(value->value, JSON_BOOL_TRUE_STR, JSON_BOOL_TRUE_LEN) == 0) {
        *out = true;
        return ACTRUST_OK;
    }

    if (value->value_len == JSON_BOOL_FALSE_LEN &&
        strncmp(value->value, JSON_BOOL_FALSE_STR, JSON_BOOL_FALSE_LEN) == 0) {
        *out = false;
        return ACTRUST_OK;
    }

    return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
}

/* ========================================================================
 * String Utility API
 * ======================================================================== */

static int json_hex_digit_to_int(char c)
{
    if (c >= '0' && c <= '9') {
        return (int) (c - '0');
    }

    if (c >= 'a' && c <= 'f') {
        return (int) (c - 'a') + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return (int) (c - 'A') + 10;
    }

    return -1;
}

actrust_err_t actrust_json_escape_string(const char *input, char *out,
                                         size_t out_cap, size_t *out_len)
{
    if (input == NULL || out == NULL || out_len == NULL || out_cap == 0u) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t               dst_idx = 0u;
    const unsigned char *p       = (const unsigned char *) input;

    while (*p != '\0') {
        const char *esc_seq = NULL;
        size_t      esc_len = 0u;
        char        hex_buf[7];

        switch (*p) {
            case '"':
                esc_seq = "\\\"";
                esc_len = 2u;
                break;
            case '\\':
                esc_seq = "\\\\";
                esc_len = 2u;
                break;
            case '\b':
                esc_seq = "\\b";
                esc_len = 2u;
                break;
            case '\f':
                esc_seq = "\\f";
                esc_len = 2u;
                break;
            case '\n':
                esc_seq = "\\n";
                esc_len = 2u;
                break;
            case '\r':
                esc_seq = "\\r";
                esc_len = 2u;
                break;
            case '\t':
                esc_seq = "\\t";
                esc_len = 2u;
                break;
            default:
                if (*p < 0x20u) {
                    int32_t n = snprintf(hex_buf, sizeof(hex_buf), "\\u%04x",
                                         (unsigned int) *p);
                    if (n < 0 || (size_t) n >= sizeof(hex_buf)) {
                        return JSON_ERR(ACTRUST_ERR_HW_FAILURE);
                    }
                    esc_seq = hex_buf;
                    esc_len = (size_t) n;
                }
                break;
        }

        if (esc_seq != NULL) {
            if (dst_idx + esc_len + 1u > out_cap) {
                return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
            }
            memcpy(out + dst_idx, esc_seq, esc_len);
            dst_idx += esc_len;
        } else {
            if (dst_idx + 2u > out_cap) {
                return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
            }
            out[dst_idx++] = (char) *p;
        }

        p++;
    }

    out[dst_idx] = '\0';
    *out_len     = dst_idx;
    return ACTRUST_OK;
}

actrust_err_t actrust_json_unescape_string(const char *input, size_t input_len,
                                           char *out, size_t out_cap,
                                           size_t *out_len)
{
    if (input == NULL || out == NULL || out_len == NULL || out_cap == 0u) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t src_idx = 0u;
    size_t dst_idx = 0u;

    while (src_idx < input_len) {
        char ch = input[src_idx++];
        if (ch != '\\') {
            if (dst_idx + 2u > out_cap) {
                return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
            }
            out[dst_idx++] = ch;
            continue;
        }

        if (src_idx >= input_len) {
            return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
        }

        char esc     = input[src_idx++];
        char out_ch  = '\0';
        bool has_out = true;

        switch (esc) {
            case '"':
                out_ch = '"';
                break;
            case '\\':
                out_ch = '\\';
                break;
            case '/':
                out_ch = '/';
                break;
            case 'b':
                out_ch = '\b';
                break;
            case 'f':
                out_ch = '\f';
                break;
            case 'n':
                out_ch = '\n';
                break;
            case 'r':
                out_ch = '\r';
                break;
            case 't':
                out_ch = '\t';
                break;
            case 'u': {
                if (src_idx + 4u > input_len) {
                    return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
                }

                int h1 = json_hex_digit_to_int(input[src_idx + 0u]);
                int h2 = json_hex_digit_to_int(input[src_idx + 1u]);
                int h3 = json_hex_digit_to_int(input[src_idx + 2u]);
                int h4 = json_hex_digit_to_int(input[src_idx + 3u]);
                if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
                    return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
                }

                uint32_t cp =
                    (uint32_t) ((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
                src_idx += 4u;

                /* Handle UTF-16 surrogate pairs */
                if (cp >= 0xD800u && cp <= 0xDBFFu) {
                    if (src_idx + 6u > input_len || input[src_idx] != '\\' ||
                        input[src_idx + 1u] != 'u') {
                        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
                    }
                    src_idx += 2u;
                    int l1 = json_hex_digit_to_int(input[src_idx + 0u]);
                    int l2 = json_hex_digit_to_int(input[src_idx + 1u]);
                    int l3 = json_hex_digit_to_int(input[src_idx + 2u]);
                    int l4 = json_hex_digit_to_int(input[src_idx + 3u]);
                    if (l1 < 0 || l2 < 0 || l3 < 0 || l4 < 0) {
                        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
                    }
                    uint32_t low =
                        (uint32_t) ((l1 << 12) | (l2 << 8) | (l3 << 4) | l4);
                    if (low < 0xDC00u || low > 0xDFFFu) {
                        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
                    }
                    src_idx += 4u;
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                    return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
                }

                /* Encode codepoint as UTF-8 */
                uint8_t enc[4];
                size_t  enc_len;
                if (cp <= 0x7Fu) {
                    enc[0]  = (uint8_t) cp;
                    enc_len = 1u;
                } else if (cp <= 0x7FFu) {
                    enc[0]  = (uint8_t) (0xC0u | (cp >> 6));
                    enc[1]  = (uint8_t) (0x80u | (cp & 0x3Fu));
                    enc_len = 2u;
                } else if (cp <= 0xFFFFu) {
                    enc[0]  = (uint8_t) (0xE0u | (cp >> 12));
                    enc[1]  = (uint8_t) (0x80u | ((cp >> 6) & 0x3Fu));
                    enc[2]  = (uint8_t) (0x80u | (cp & 0x3Fu));
                    enc_len = 3u;
                } else {
                    enc[0]  = (uint8_t) (0xF0u | (cp >> 18));
                    enc[1]  = (uint8_t) (0x80u | ((cp >> 12) & 0x3Fu));
                    enc[2]  = (uint8_t) (0x80u | ((cp >> 6) & 0x3Fu));
                    enc[3]  = (uint8_t) (0x80u | (cp & 0x3Fu));
                    enc_len = 4u;
                }

                if (dst_idx + enc_len + 1u > out_cap) {
                    return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
                }
                memcpy(out + dst_idx, enc, enc_len);
                dst_idx += enc_len;
                continue;
            }
            default:
                has_out = false;
                break;
        }

        if (has_out == false) {
            return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
        }

        if (dst_idx + 2u > out_cap) {
            return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
        }
        out[dst_idx++] = out_ch;
    }

    out[dst_idx] = '\0';
    *out_len     = dst_idx;
    return ACTRUST_OK;
}

/* ========================================================================
 * Builder API
 * ======================================================================== */

actrust_err_t actrust_json_builder_init(actrust_json_builder_t *b, char *buf,
                                        size_t cap)
{
    if (b == NULL || buf == NULL || cap < 2) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    b->buf = buf;
    b->cap = cap;
    b->len = 0;

    return json_builder_write_char(b, '{');
}

actrust_err_t actrust_json_builder_add_string(actrust_json_builder_t *b,
                                              const char *key, const char *val,
                                              size_t val_len)
{
    if (b == NULL || key == NULL || val == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (strlen(val) != val_len) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err;

    if ((err = json_builder_append_key(b, key)) != ACTRUST_OK) {
        return err;
    }

    if ((err = json_builder_write_char(b, '"')) != ACTRUST_OK) {
        return err;
    }

    if ((err = json_builder_append_escaped_len(b, val, val_len)) !=
        ACTRUST_OK) {
        return err;
    }

    if ((err = json_builder_write_char(b, '"')) != ACTRUST_OK) {
        return err;
    }

    return ACTRUST_OK;
}

actrust_err_t actrust_json_builder_add_object(actrust_json_builder_t *b,
                                              const char             *key,
                                              const char             *obj_json)
{
    if (b == NULL || key == NULL || obj_json == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t obj_len = strlen(obj_json);
    if (obj_len == 0u) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (JSON_Validate(obj_json, obj_len) != JSONSuccess) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (!json_is_object_text(obj_json, obj_len)) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = json_builder_append_key(b, key);
    if (err != ACTRUST_OK) {
        return err;
    }

    return json_builder_write(b, obj_json, obj_len);
}

actrust_err_t actrust_json_builder_open_object(actrust_json_builder_t *b,
                                               const char             *key)
{
    if (b == NULL || key == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = json_builder_append_key(b, key);
    if (err != ACTRUST_OK) {
        return err;
    }

    return json_builder_write_char(b, '{');
}

actrust_err_t actrust_json_builder_close_object(actrust_json_builder_t *b)
{
    if (b == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    return json_builder_write_char(b, '}');
}

actrust_err_t actrust_json_builder_add_int(actrust_json_builder_t *b,
                                           const char *key, int64_t val)
{
    if (b == NULL || key == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err;
    char          num_buf[32];

    if ((err = json_builder_append_key(b, key)) != ACTRUST_OK) {
        return err;
    }

    int32_t n = snprintf(num_buf, sizeof(num_buf), "%" PRId64, val);
    if (n < 0 || (size_t) n >= sizeof(num_buf)) {
        return JSON_ERR(ACTRUST_ERR_HW_FAILURE);
    }

    return json_builder_write(b, num_buf, (size_t) n);
}

actrust_err_t actrust_json_builder_add_bool(actrust_json_builder_t *b,
                                            const char *key, bool val)
{
    if (b == NULL || key == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err;

    if ((err = json_builder_append_key(b, key)) != ACTRUST_OK) {
        return err;
    }

    if (val) {
        return json_builder_write(b, JSON_BOOL_TRUE_STR, JSON_BOOL_TRUE_LEN);
    } else {
        return json_builder_write(b, JSON_BOOL_FALSE_STR, JSON_BOOL_FALSE_LEN);
    }
}

actrust_err_t actrust_json_builder_finish(actrust_json_builder_t *b,
                                          char **out_json, size_t *out_len)
{
    if (b == NULL || b->buf == NULL) {
        return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (JSON_Validate(b->buf, b->len) != JSONSuccess) {
        if (json_builder_write_char(b, '}') != ACTRUST_OK) {
            return JSON_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
        }

        if (JSON_Validate(b->buf, b->len) != JSONSuccess) {
            return JSON_ERR(ACTRUST_ERR_INVALID_ARG);
        }
    }

    if (out_json != NULL) {
        *out_json = b->buf;
    }
    if (out_len != NULL) {
        *out_len = b->len;
    }

    return ACTRUST_OK;
}
