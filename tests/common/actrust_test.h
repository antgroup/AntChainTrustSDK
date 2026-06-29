// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file actrust_test.h
 * @brief Shared test helpers for the AntChainTrustSDK test suite.
 *
 * Exposes common test constants and @ref actrust_test_load_file, used by smoke
 * tests that load PKI material from disk (core, tls, mqtt, cloud).
 */

#ifndef ACTRUST_TEST_H
#define ACTRUST_TEST_H

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read an entire file into @p buf.
 *
 * @param path     File path (relative to the test's WORKING_DIRECTORY).
 * @param buf      Destination buffer.
 * @param buf_cap  Capacity of @p buf in bytes. When @p text is true, one
 *                 byte is reserved for the trailing NUL.
 * @param out_len  On success, set to the number of bytes read (NUL not
 *                 counted).
 * @param text     When true, NUL-terminate the buffer after reading.
 *
 * @return 0 on success, 1 on any error (missing file, empty read, buffer
 *         too small to hold a NUL terminator).
 */
static inline int actrust_test_load_file(const char *path, void *buf,
                                         size_t buf_cap, size_t *out_len,
                                         bool text)
{
    if (path == NULL || buf == NULL || out_len == NULL || buf_cap == 0u) {
        return 1;
    }

    size_t read_cap = text ? buf_cap - 1u : buf_cap;
    if (text && buf_cap < 1u) {
        return 1;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 1;
    }

    size_t read_len = fread(buf, 1u, read_cap, file);
    int    err      = ferror(file);
    fclose(file);

    if (err != 0 || read_len == 0u) {
        return 1;
    }

    if (text) {
        ((char *) buf)[read_len] = '\0';
    }

    *out_len = read_len;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_TEST_H */
