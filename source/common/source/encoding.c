// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file encoding.c
 * @brief Common encoding helper implementation.
 */

/* Common */
#include "common/common.h"

size_t actrust_hex_encode(const uint8_t *in, size_t in_len, char *out)
{
    if (out == NULL || (in == NULL && in_len > 0u)) {
        return 0u;
    }

    static const char hex[] = "0123456789abcdef";
    char             *p     = out;

    for (size_t i = 0u; i < in_len; i++) {
        *p++ = hex[in[i] >> 4];
        *p++ = hex[in[i] & 0x0Fu];
    }
    *p = '\0';
    return in_len * 2u;
}
