// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file memory.c
 * @brief Common memory helper implementation.
 */

/* C standard */
#include <stdint.h>
#include <stdlib.h>

/* Common */
#include "common/common.h"

void actrust_secure_zeroize(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *) ptr;

    if (ptr == NULL) {
        return;
    }

    while (len-- > 0u) {
        *p++ = 0u;
    }
}

void actrust_secure_free(void *ptr, size_t len)
{
    if (ptr != NULL) {
        actrust_secure_zeroize(ptr, len);
        ACTRUST_FREE(ptr);
    }
}
