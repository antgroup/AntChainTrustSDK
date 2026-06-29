// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file common/common.h
 * @brief Common dependency-free helpers.
 */

#ifndef ACTRUST_COMMON_H
#define ACTRUST_COMMON_H

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Adapter */
#include "adapter/system.h"

#define ACTRUST_MALLOC(size)        actrust_malloc((size))
#define ACTRUST_CALLOC(nmemb, size) actrust_calloc((nmemb), (size))
#define ACTRUST_FREE(ptr)           actrust_free((ptr))

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clear a buffer in a way that must not be optimized away.
 *
 * Use this before releasing buffers that may contain keys, certificates,
 * plaintexts, tokens, or other sensitive material.
 *
 * @param[in,out] ptr Buffer to clear. NULL is accepted.
 * @param[in] len Number of bytes to clear.
 */
void actrust_secure_zeroize(void *ptr, size_t len);

/**
 * @brief Clear a sensitive buffer and release it with free().
 *
 * @param[in,out] ptr Heap buffer to clear and free. NULL is accepted.
 * @param[in] len Number of bytes to clear before free().
 */
void actrust_secure_free(void *ptr, size_t len);

/**
 * @brief Encode bytes as lowercase hexadecimal text.
 *
 * The caller must provide an output buffer of at least @p in_len * 2 + 1
 * bytes. The output is always NUL-terminated.
 *
 * @param[in] in Input bytes to encode.
 * @param[in] in_len Number of input bytes.
 * @param[out] out Output buffer for lowercase hex text.
 *
 * @return Number of hex characters written, excluding the trailing NUL.
 */
size_t actrust_hex_encode(const uint8_t *in, size_t in_len, char *out);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_COMMON_H */
